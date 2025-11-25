#include "presence_for_plex/services/plex/plex.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/json_helper.hpp"
#include "presence_for_plex/utils/plex_headers_builder.hpp"
#include <nlohmann/json.hpp>
#include <thread>

namespace presence_for_plex {
namespace services {

using json = nlohmann::json;

static std::string network_error_to_string(NetworkError error) {
    switch (error) {
        case NetworkError::ConnectionFailed: return "Connection failed";
        case NetworkError::Timeout: return "Timeout";
        case NetworkError::DNSResolutionFailed: return "DNS resolution failed";
        case NetworkError::SSLError: return "SSL error";
        case NetworkError::InvalidUrl: return "Invalid URL";
        case NetworkError::TooManyRedirects: return "Too many redirects";
        case NetworkError::BadResponse: return "Bad response";
        case NetworkError::Cancelled: return "Cancelled";
        default: return "Unknown error";
    }
}

static void extract_type_specific_info_helper(const nlohmann::json& metadata, core::MediaInfo& info) {
    std::string type_str = metadata.value("type", "");

    if (type_str == "movie") {
        info.type = core::MediaType::Movie;

        if (metadata.contains("Guid") && metadata["Guid"].is_array()) {
            for (const auto& guid : metadata["Guid"]) {
                std::string id = guid.value("id", "");
                if (id.find("imdb://") == 0) {
                    info.imdb_id = id.substr(7);
                } else if (id.find("tmdb://") == 0) {
                    info.tmdb_id = id.substr(7);
                }
            }
        }

        if (metadata.contains("Genre") && metadata["Genre"].is_array()) {
            for (const auto& genre : metadata["Genre"]) {
                info.genres.push_back(genre.value("tag", ""));
            }
        }
    }
    else if (type_str == "episode") {
        info.type = core::MediaType::TVShow;
        info.grandparent_title = metadata.value("grandparentTitle", "Unknown");
        info.show_title = info.grandparent_title;
        info.season = metadata.value("parentIndex", 0);
        info.episode = metadata.value("index", 0);

        if (metadata.contains("grandparentKey")) {
            info.grandparent_key = metadata.value("grandparentKey", "");
        }
    }
    else if (type_str == "track") {
        info.type = core::MediaType::Music;
        info.album = metadata.value("parentTitle", "");
        info.artist = metadata.value("grandparentTitle", "");
        info.track = metadata.value("index", 0);
    }
    else {
        info.type = core::MediaType::Unknown;
    }
}

Plex::Plex(
    std::shared_ptr<PlexAuth> auth,
    std::shared_ptr<PlexSSE> sse,
    std::shared_ptr<HttpClient> http_client,
    std::shared_ptr<core::ConfigManager> config_service)
    : m_auth(std::move(auth))
    , m_sse(std::move(sse))
    , m_http_client(std::move(http_client))
    , m_config_service(std::move(config_service)) {

    LOG_DEBUG("Plex", "Creating Plex");

    m_sse->set_sse_event_callback(
        [this](const core::ServerId& server_id, const std::string& event) {
            handle_sse_event(server_id, event);
        }
    );

    m_sse->set_connection_state_callback(
        [this](const core::ServerId& server_id, bool connected, const std::string& uri) {
            if (m_event_bus) {
                if (connected) {
                    publish_server_connected(server_id, server_id.value);
                } else {
                    publish_server_disconnected(server_id, "Connection lost");
                }
            }

            if (connected) {
                ServerConnectionInfo conn_info;
                conn_info.preferred_uri = uri;
                conn_info.access_token = "";
                conn_info.owned = false;

                std::lock_guard<std::mutex> lock(m_server_tokens_mutex);
                auto token_it = m_server_tokens.find(server_id);
                if (token_it != m_server_tokens.end()) {
                    conn_info.access_token = token_it->second.token;
                    conn_info.owned = token_it->second.owned;
                }

                update_server_connection(server_id, conn_info);
            }
        }
    );
}

Plex::~Plex() {
    if (m_running) {
        stop();
    }
    LOG_INFO("Plex", "Plex destroyed");
}

std::expected<void, core::PlexError> Plex::start() {
    LOG_DEBUG("Plex", "Starting");

    if (m_running) {
        return {};
    }

    auto auth_result = m_auth->ensure_authenticated(true);
    if (!auth_result) {
        LOG_ERROR("Plex", "Failed to load authentication token");
        return std::unexpected<core::PlexError>(auth_result.error());
    }

    auto [token, username] = auth_result.value();
    m_running = true;

    std::thread discovery_thread([this, token]() {
        auto username_result = m_auth->fetch_username(token);
        if (username_result) {
            m_target_username = username_result.value();
            LOG_INFO("Plex", "Logged in as: " + m_target_username);
        }

        if (m_config_service && m_config_service->get().plex.auto_discover) {
            LOG_INFO("Plex", "Auto-discovery enabled");
            auto discovery_result = discover_servers(token);
            if (!discovery_result) {
                LOG_WARNING("Plex", "Failed to discover servers");
            }
        }

        if (m_config_service) {
            const auto& manual_urls = m_config_service->get().plex.server_urls;
            for (const auto& url : manual_urls) {
                auto add_result = add_manual_server(url, token);
                if (!add_result) {
                    LOG_WARNING("Plex", "Failed to add manual server: " + url);
                }
            }
        }

        m_sse->start_all_connections();
    });
    discovery_thread.detach();

    return {};
}

void Plex::stop() {
    LOG_INFO("Plex", "Stopping");

    m_running = false;
    m_auth->shutdown();
    m_sse->stop_all_connections();

    {
        std::lock_guard<std::mutex> sessions_lock(m_sessions_mutex);
        std::lock_guard<std::mutex> cache_lock(m_cache_mutex);
        m_active_sessions.clear();
        m_server_connections.clear();
        m_media_cache.clear();
        m_session_user_cache.clear();
    }

    m_last_media_state = core::MediaInfo{};
}

bool Plex::is_running() const {
    return m_running;
}

void Plex::set_poll_interval(std::chrono::seconds interval) {
    m_poll_interval = interval;
}

std::chrono::seconds Plex::get_poll_interval() const {
    return m_poll_interval;
}

void Plex::set_event_bus(std::shared_ptr<core::EventBus> bus) {
    m_event_bus = std::move(bus);
}

void Plex::add_metadata_service(std::unique_ptr<IMetadataService> service) {
    m_metadata_services.push_back(std::move(service));
}

std::expected<core::MediaInfo, core::PlexError> Plex::get_current_media() const {
    if (!m_running) {
        return std::unexpected<core::PlexError>(core::PlexError::NotInitialized);
    }

    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    if (m_active_sessions.empty()) {
        core::MediaInfo stopped_info;
        stopped_info.state = core::PlaybackState::Stopped;
        return stopped_info;
    }

    auto current = find_most_recent_session();
    if (current.state == core::PlaybackState::Stopped) {
        return current;
    }

    if (!is_media_type_enabled(current.type)) {
        core::MediaInfo stopped_info;
        stopped_info.state = core::PlaybackState::Stopped;
        return stopped_info;
    }

    return current;
}

std::expected<std::vector<core::MediaInfo>, core::PlexError> Plex::get_active_sessions() const {
    if (!m_running) {
        return std::unexpected<core::PlexError>(core::PlexError::NotInitialized);
    }

    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    std::vector<core::MediaInfo> sessions;
    sessions.reserve(m_active_sessions.size());

    for (const auto& [session_key, info] : m_active_sessions) {
        if (info.state != core::PlaybackState::Stopped && is_media_type_enabled(info.type)) {
            sessions.push_back(info);
        }
    }

    return sessions;
}

std::expected<void, core::PlexError> Plex::add_server(std::unique_ptr<core::PlexServer> server) {
    if (!server) {
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    core::ServerId server_id(server->client_identifier);
    core::PlexToken server_token = server->access_token;
    bool is_owned = server->owned;

    {
        std::lock_guard<std::mutex> lock(m_server_tokens_mutex);
        m_server_tokens[server_id] = {server_token, is_owned};
    }

    return m_sse->add_server(std::move(server));
}

void Plex::remove_server(const core::ServerId& server_id) {
    m_sse->remove_server(server_id);

    std::lock_guard<std::mutex> lock(m_sessions_mutex);
    auto it = m_active_sessions.begin();
    while (it != m_active_sessions.end()) {
        if (it->second.server_id.get() == server_id.get()) {
            it = m_active_sessions.erase(it);
        } else {
            ++it;
        }
    }
    m_server_connections.erase(server_id);
}

std::vector<core::ServerId> Plex::get_connected_servers() const {
    return m_sse->get_connected_servers();
}

bool Plex::is_server_connected(const core::ServerId& server_id) const {
    return m_sse->is_server_connected(server_id);
}

std::expected<void, core::PlexError> Plex::discover_servers(const std::string& auth_token) {
    LOG_DEBUG("Plex", "Discovering servers");

    auto std_headers = m_auth->get_standard_headers(auth_token);
    HttpHeaders headers(std_headers.begin(), std_headers.end());

    const std::string resources_url = "https://plex.tv/api/v2/resources?includeHttps=1";
    auto response = m_http_client->get(resources_url, headers);

    if (!response || !response->is_success()) {
        LOG_ERROR("Plex", "Failed to fetch servers from Plex.tv");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    return parse_server_json(response->body);
}

std::expected<void, core::PlexError> Plex::parse_server_json(const std::string& json_response) {
    auto json_result = utils::JsonHelper::safe_parse(json_response);
    if (!json_result) {
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json = json_result.value();
    int server_count = 0;

    for (const auto& resource : json) {
        std::string provides = utils::JsonHelper::get_optional<std::string>(resource, "provides", "");
        if (provides != "server") {
            continue;
        }

        auto server = std::make_unique<core::PlexServer>();
        server->name = utils::JsonHelper::get_optional<std::string>(resource, "name", "Unknown");
        server->client_identifier = utils::JsonHelper::get_optional<std::string>(resource, "clientIdentifier", "");
        server->access_token = utils::JsonHelper::get_optional<std::string>(resource, "accessToken", "");
        server->owned = utils::JsonHelper::get_optional<bool>(resource, "owned", false);

        LOG_INFO("Plex", "Found server: " + server->name + (server->owned ? " [owned]" : " [shared]"));

        utils::JsonHelper::for_each_in_array(resource, "connections", [&](const auto& connection) {
            std::string uri = utils::JsonHelper::get_optional<std::string>(connection, "uri", "");
            bool is_local = utils::JsonHelper::get_optional<bool>(connection, "local", false);

            if (!uri.empty()) {
                if (is_local) {
                    server->local_uris.push_back(uri);
                } else {
                    server->public_uris.push_back(uri);
                }
            }
        });

        if (!server->local_uris.empty() || !server->public_uris.empty()) {
            auto add_result = add_server(std::move(server));
            if (add_result) {
                server_count++;
            }
        }
    }

    LOG_INFO("Plex", "Discovered " + std::to_string(server_count) + " servers");
    return {};
}

std::expected<void, core::PlexError> Plex::add_manual_server(const std::string& server_url, const core::PlexToken& auth_token) {
    LOG_INFO("Plex", "Adding manual server: " + server_url);

    std::string identity_url = server_url;
    if (identity_url.back() != '/') {
        identity_url += '/';
    }
    identity_url += "identity";

    auto std_headers = m_auth->get_standard_headers(auth_token);
    HttpHeaders headers(std_headers.begin(), std_headers.end());

    auto response = m_http_client->get(identity_url, headers);

    if (!response || !response->is_success()) {
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response->body);
    if (!json_result) {
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    if (!utils::JsonHelper::has_field(json_response, "MediaContainer")) {
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto container = json_response["MediaContainer"];

    auto client_id_result = utils::JsonHelper::get_required<std::string>(container, "machineIdentifier");
    if (!client_id_result) {
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    std::string client_id = client_id_result.value();
    std::string friendly_name = utils::JsonHelper::get_optional<std::string>(container, "friendlyName", "Manual Server");

    auto server = std::make_unique<core::PlexServer>();
    server->name = friendly_name;
    server->client_identifier = client_id;
    server->access_token = auth_token;
    server->owned = true;

    if (server_url.find("127.0.0.1") != std::string::npos ||
        server_url.find("localhost") != std::string::npos ||
        server_url.find("192.168.") != std::string::npos ||
        server_url.find("10.") != std::string::npos) {
        server->local_uris.push_back(server_url);
    } else {
        server->public_uris.push_back(server_url);
    }

    return add_server(std::move(server));
}

void Plex::handle_sse_event(const core::ServerId& server_id, const std::string& event) {
    auto json_result = utils::JsonHelper::safe_parse(event);
    if (!json_result) {
        return;
    }

    if (utils::JsonHelper::has_field(json_result.value(), "PlaySessionStateNotification")) {
        process_session_event(server_id, json_result.value()["PlaySessionStateNotification"]);
    }
}

void Plex::update_server_connection(const core::ServerId& server_id, const ServerConnectionInfo& connection_info) {
    std::lock_guard<std::mutex> lock(m_sessions_mutex);
    m_server_connections[server_id] = connection_info;
}

void Plex::process_session_event(const core::ServerId& server_id, const nlohmann::json& notification) {
    std::string session_key_str = notification.value("sessionKey", "");
    std::string state = notification.value("state", "");
    std::string media_key = notification.value("key", "");
    int64_t view_offset = notification.value("viewOffset", 0);

    if (session_key_str.empty()) {
        return;
    }

    core::SessionKey session_key(session_key_str);

    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    if (state == "playing" || state == "paused" || state == "buffering") {
        update_session_info(server_id, session_key, state, media_key, view_offset);
    } else if (state == "stopped") {
        auto session_it = m_active_sessions.find(session_key);
        if (session_it != m_active_sessions.end()) {
            m_active_sessions.erase(session_it);

            if (m_event_bus) {
                auto current = find_most_recent_session();
                publish_media_updated(m_last_media_state, current);
                m_last_media_state = current;
            }
        }
    }
}

void Plex::update_session_info(const core::ServerId& server_id, const core::SessionKey& session_key,
                               const std::string& state, const std::string& media_key, int64_t view_offset) {
    if (!should_process_session(server_id, session_key)) {
        return;
    }

    auto server_it = m_server_connections.find(server_id);
    if (server_it == m_server_connections.end()) {
        return;
    }

    const auto& connection_info = server_it->second;
    core::MediaInfo info;
    bool has_existing_session = m_active_sessions.find(session_key) != m_active_sessions.end();

    if (has_existing_session) {
        info = m_active_sessions[session_key];
    } else {
        auto fetch_result = fetch_media_details(
            connection_info.preferred_uri,
            connection_info.access_token,
            media_key
        );

        if (!fetch_result.has_value()) {
            return;
        }

        info = fetch_result.value();
        info.session_created_at = std::chrono::system_clock::now();
    }

    update_playback_state(info, state, view_offset);
    info.session_key = session_key;
    info.server_id = server_id;

    m_active_sessions[session_key] = info;

    if (m_event_bus) {
        publish_media_updated(m_last_media_state, info);
        m_last_media_state = info;
    }
}

void Plex::update_playback_state(core::MediaInfo& info, const std::string& state, int64_t view_offset) {
    if (state == "playing") {
        info.state = core::PlaybackState::Playing;
    } else if (state == "paused") {
        info.state = core::PlaybackState::Paused;
    } else if (state == "buffering") {
        info.state = core::PlaybackState::Buffering;
    } else {
        info.state = core::PlaybackState::Stopped;
    }

    info.progress = static_cast<double>(view_offset) / 1000.0;
    info.start_time = std::chrono::system_clock::now() -
                     std::chrono::seconds(static_cast<long>(info.progress));
}

bool Plex::should_process_session(const core::ServerId& server_id, const core::SessionKey& session_key) {
    auto server_it = m_server_connections.find(server_id);
    if (server_it == m_server_connections.end()) {
        return false;
    }

    const auto& connection_info = server_it->second;

    if (connection_info.owned) {
        auto validation_result = validate_session_user(
            connection_info.preferred_uri,
            connection_info.access_token,
            session_key
        );

        if (!validation_result.has_value()) {
            return false;
        }

        return validation_result.value();
    }

    return true;
}

core::MediaInfo Plex::find_most_recent_session() const {
    core::MediaInfo newest;
    newest.state = core::PlaybackState::Stopped;

    auto newest_time = std::chrono::system_clock::time_point::min();

    for (const auto& [session_key, info] : m_active_sessions) {
        if (info.state == core::PlaybackState::Playing ||
            info.state == core::PlaybackState::Paused ||
            info.state == core::PlaybackState::Buffering) {

            if (info.start_time > newest_time) {
                newest = info;
                newest_time = info.start_time;
            }
        }
    }

    return newest;
}

std::expected<bool, core::PlexError> Plex::validate_session_user(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    const core::SessionKey& session_key) {

    if (m_target_username.empty()) {
        return true;
    }

    auto username_result = fetch_session_username(server_uri, access_token, session_key);
    if (!username_result.has_value()) {
        return std::unexpected<core::PlexError>(username_result.error());
    }

    return username_result.value() == m_target_username;
}

std::expected<std::string, core::PlexError> Plex::fetch_session_username(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    const core::SessionKey& session_key) {

    std::string cache_key = server_uri + session_key.get();
    auto cached_username = get_cached_session_user(cache_key);
    if (cached_username.has_value()) {
        return cached_username.value();
    }

    std::string url = server_uri + SESSION_ENDPOINT;
    auto headers_map = get_standard_headers(access_token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get(url, headers);
    if (!response || !response->is_success()) {
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response->body);
    if (!json_result) {
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    if (!utils::JsonHelper::has_field(json_response, "MediaContainer")) {
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto container = json_response["MediaContainer"];

    if (!utils::JsonHelper::has_array(container, "Metadata")) {
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    std::string found_username;
    bool found = false;

    utils::JsonHelper::for_each_in_array(container, "Metadata", [&](const auto& session) {
        if (found) return;

        std::string current_session_key = utils::JsonHelper::get_optional<std::string>(session, "sessionKey", "");
        if (current_session_key == session_key.get()) {
            if (utils::JsonHelper::has_field(session, "User")) {
                auto user = session["User"];
                if (utils::JsonHelper::has_field(user, "title")) {
                    found_username = user["title"].get<std::string>();
                    found = true;
                }
            }
        }
    });

    if (found) {
        cache_session_user(cache_key, found_username);
        return found_username;
    }

    return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
}

std::expected<core::MediaInfo, core::PlexError> Plex::fetch_media_details(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    const std::string& media_key) {

    std::string cache_key = server_uri + media_key;
    auto cached_info = get_cached_media_info(cache_key);
    if (cached_info.has_value()) {
        return cached_info.value();
    }

    std::string url = server_uri + media_key;
    auto headers_map = get_standard_headers(access_token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get(url, headers);
    if (!response.has_value()) {
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    if (!response.value().is_success()) {
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response.value().body);
    if (!json_result) {
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    if (!utils::JsonHelper::has_field(json_response, "MediaContainer")) {
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto container = json_response["MediaContainer"];
    if (!utils::JsonHelper::has_array(container, "Metadata")) {
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto metadata = container["Metadata"][0];

    core::MediaInfo info;
    extract_basic_media_info(metadata, info);
    extract_type_specific_info(metadata, info);

    if (info.type == core::MediaType::TVShow && !info.grandparent_key.empty()) {
        auto grandparent_result = fetch_grandparent_metadata(server_uri, access_token, info);
        (void)grandparent_result;
    }

    enrich_with_external_services(info);
    cache_media_info(cache_key, info);

    return info;
}

std::expected<void, core::PlexError> Plex::fetch_grandparent_metadata(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    core::MediaInfo& info) {

    if (info.grandparent_key.empty()) {
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    std::string url = server_uri + info.grandparent_key;
    auto headers_map = get_standard_headers(access_token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get(url, headers);
    if (!response.has_value()) {
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    if (!response.value().is_success()) {
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response.value().body);
    if (!json_result) {
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    if (!utils::JsonHelper::has_field(json_response, "MediaContainer")) {
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto container = json_response["MediaContainer"];
    if (!utils::JsonHelper::has_array(container, "Metadata")) {
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto metadata = container["Metadata"][0];

    utils::JsonHelper::for_each_in_array(metadata, "Guid", [&info](const auto& guid) {
        std::string id = utils::JsonHelper::get_optional<std::string>(guid, "id", "");
        if (id.find("imdb://") == 0) {
            info.imdb_id = id.substr(7);
        } else if (id.find("tmdb://") == 0) {
            info.tmdb_id = id.substr(7);
        }
    });

    if (utils::JsonHelper::has_array(metadata, "Genre")) {
        info.genres.clear();
        utils::JsonHelper::for_each_in_array(metadata, "Genre", [&info](const auto& genre) {
            info.genres.push_back(utils::JsonHelper::get_optional<std::string>(genre, "tag", ""));
        });
    }

    return {};
}

void Plex::extract_basic_media_info(const nlohmann::json& metadata, core::MediaInfo& info) const {
    info.title = metadata.value("title", "Unknown");
    info.original_title = metadata.value("originalTitle", info.title);
    info.duration = metadata.value("duration", 0) / 1000.0;
    info.summary = metadata.value("summary", "No summary available");
    info.year = metadata.value("year", 0);
    info.rating = metadata.value("rating", 0.0);
    info.studio = metadata.value("studio", "");

    if (metadata.contains("thumb")) {
        info.thumb = metadata["thumb"];
    }
    if (metadata.contains("art")) {
        info.art = metadata["art"];
    }
}

void Plex::extract_type_specific_info(const nlohmann::json& metadata, core::MediaInfo& info) const {
    extract_type_specific_info_helper(metadata, info);
}

void Plex::enrich_with_external_services(core::MediaInfo& info) {
    for (const auto& service : m_metadata_services) {
        service->enrich_media_info(info);
    }
}

std::map<std::string, std::string> Plex::get_standard_headers(const core::PlexToken& token) const {
    return utils::PlexHeadersBuilder::create_authenticated_headers("presence-for-plex", token);
}

bool Plex::is_media_type_enabled(core::MediaType type) const {
    if (!m_config_service) {
        return true;
    }

    const auto& config = m_config_service->get();

    switch (type) {
        case core::MediaType::Movie:
            return config.plex.enable_movies;
        case core::MediaType::TVShow:
            return config.plex.enable_tv_shows;
        case core::MediaType::Music:
            return config.plex.enable_music;
        case core::MediaType::Unknown:
        default:
            return true;
    }
}

void Plex::cache_media_info(const std::string& key, const core::MediaInfo& info) {
    std::lock_guard<std::mutex> lock(m_cache_mutex);

    CacheEntry<core::MediaInfo> entry;
    entry.data = info;
    entry.timestamp = std::chrono::system_clock::now();
    entry.ttl = MEDIA_CACHE_TIMEOUT;

    m_media_cache[key] = entry;
}

std::optional<core::MediaInfo> Plex::get_cached_media_info(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_cache_mutex);

    auto it = m_media_cache.find(key);
    if (it != m_media_cache.end() && it->second.is_valid()) {
        return it->second.data;
    }

    return std::nullopt;
}

void Plex::cache_session_user(const std::string& key, const std::string& username) {
    std::lock_guard<std::mutex> lock(m_cache_mutex);

    CacheEntry<std::string> entry;
    entry.data = username;
    entry.timestamp = std::chrono::system_clock::now();
    entry.ttl = SESSION_CACHE_TIMEOUT;

    m_session_user_cache[key] = entry;
}

std::optional<std::string> Plex::get_cached_session_user(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_cache_mutex);

    auto it = m_session_user_cache.find(key);
    if (it != m_session_user_cache.end() && it->second.is_valid()) {
        return it->second.data;
    }

    return std::nullopt;
}

void Plex::publish_media_updated(const core::MediaInfo& old_info, const core::MediaInfo& new_info) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::MediaSessionStateChanged::updated(old_info, new_info));
    }
}

void Plex::publish_server_connected(const core::ServerId& server_id, const std::string& name) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::ServerConnectionStateChanged::established(server_id, name));
    }
}

void Plex::publish_server_disconnected(const core::ServerId& server_id, const std::string& reason) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::ServerConnectionStateChanged::lost(server_id, reason));
    }
}

void Plex::publish_media_error(core::PlexError error, const std::string& message,
                               const std::optional<core::ServerId>& server_id) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::MediaError{error, message, server_id});
    }
}

} // namespace services
} // namespace presence_for_plex
