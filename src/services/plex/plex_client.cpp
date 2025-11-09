#include "presence_for_plex/services/plex/plex_client.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/json_helper.hpp"
#include "presence_for_plex/utils/plex_headers_builder.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace presence_for_plex {
namespace services {

using json = nlohmann::json;

// Helper function to convert NetworkError to string
static std::string network_error_to_string(NetworkError error) {
    switch (error) {
        case NetworkError::ConnectionFailed:
            return "Connection failed";
        case NetworkError::Timeout:
            return "Timeout";
        case NetworkError::DNSResolutionFailed:
            return "DNS resolution failed";
        case NetworkError::SSLError:
            return "SSL error";
        case NetworkError::InvalidUrl:
            return "Invalid URL";
        case NetworkError::TooManyRedirects:
            return "Too many redirects";
        case NetworkError::BadResponse:
            return "Bad response";
        case NetworkError::Cancelled:
            return "Cancelled";
        default:
            return "Unknown error";
    }
}

// Helper function to extract media information based on type
static void extract_type_specific_info_helper(const nlohmann::json& metadata, core::MediaInfo& info) {
    std::string type_str = metadata.value("type", "");

    if (type_str == "movie") {
        info.type = core::MediaType::Movie;

        // Extract GUIDs (IMDB, TMDB)
        if (metadata.contains("Guid") && metadata["Guid"].is_array()) {
            for (const auto& guid : metadata["Guid"]) {
                std::string id = guid.value("id", "");
                if (id.find("imdb://") == 0) {
                    info.imdb_id = id.substr(7);
                    LOG_DEBUG("PlexClient", "Found IMDB ID: " + info.imdb_id);
                } else if (id.find("tmdb://") == 0) {
                    info.tmdb_id = id.substr(7);
                    LOG_DEBUG("PlexClient", "Found TMDB ID: " + info.tmdb_id);
                }
            }
        }

        // Extract genres
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

        LOG_DEBUG("PlexClient", "Extracted show: " + info.grandparent_title +
                       " S" + std::to_string(info.season) + "E" + std::to_string(info.episode));
    }
    else if (type_str == "track") {
        info.type = core::MediaType::Music;
        info.album = metadata.value("parentTitle", "");
        info.artist = metadata.value("grandparentTitle", "");
        info.track = metadata.value("index", 0);

        LOG_DEBUG("PlexClient", "Extracted track: " + info.artist + " - " + info.album + " - " + info.title);
    }
    else {
        info.type = core::MediaType::Unknown;
        LOG_WARNING("PlexClient", "Unknown media type: " + type_str);
    }
}

// TMDBService implementation
TMDBService::TMDBService(std::shared_ptr<HttpClient> http_client, const std::string& access_token)
    : m_http_client(std::move(http_client))
    , m_access_token(access_token) {
}

std::expected<std::string, core::PlexError> TMDBService::fetch_artwork_url(
    const std::string& tmdb_id,
    core::MediaType type) {

    LOG_DEBUG("TMDB", "fetch_artwork_url() called for ID: " + tmdb_id);

    if (m_access_token.empty()) {
        LOG_DEBUG("TMDB", "No TMDB access token available");
        return std::unexpected<core::PlexError>(core::PlexError::AuthenticationError);
    }

    std::string url;
    if (type == core::MediaType::Movie) {
        url = "https://api.themoviedb.org/3/movie/" + tmdb_id + "/images";
    } else {
        url = "https://api.themoviedb.org/3/tv/" + tmdb_id + "/images";
    }

    HttpHeaders headers = {
        {"Authorization", "Bearer " + m_access_token},
        {"Accept", "application/json"}
    };

    auto response = m_http_client->get(url, headers);
    if (!response.has_value()) {
        LOG_ERROR("TMDB", "Failed to fetch TMDB images for ID: " + tmdb_id + " - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    if (!response.value().is_success()) {
        LOG_ERROR("TMDB", "Failed to fetch TMDB images for ID: " + tmdb_id);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response.value().body);
    if (!json_result) {
        LOG_ERROR("TMDB", "Error parsing TMDB response: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    // First try to get a poster
    if (utils::JsonHelper::has_array(json_response, "posters")) {
        std::string poster_path = json_response["posters"][0]["file_path"];
        std::string full_url = std::string(TMDB_IMAGE_BASE_URL) + poster_path;
        LOG_INFO("TMDB", "Found poster for ID " + tmdb_id + ": " + full_url);
        return full_url;
    }
    // Fallback to backdrops
    else if (utils::JsonHelper::has_array(json_response, "backdrops")) {
        std::string backdrop_path = json_response["backdrops"][0]["file_path"];
        std::string full_url = std::string(TMDB_IMAGE_BASE_URL) + backdrop_path;
        LOG_INFO("TMDB", "Found backdrop for ID " + tmdb_id + ": " + full_url);
        return full_url;
    }

    return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
}

std::expected<void, core::PlexError> TMDBService::enrich_media_info(core::MediaInfo& info) {
    LOG_DEBUG("TMDB", "enrich_media_info() called for: " + info.title);
    if (info.tmdb_id.empty()) {
        LOG_DEBUG("TMDB", "No TMDB ID available for enrichment");
        return {}; // Nothing to enrich
    }

    auto artwork_result = fetch_artwork_url(info.tmdb_id, info.type);
    if (artwork_result.has_value()) {
        info.art_path = artwork_result.value();
        LOG_DEBUG("TMDB", "Set art_path from TMDB: " + info.art_path);
    } else {
        LOG_WARNING("TMDB", "Failed to fetch artwork for TMDB ID: " + info.tmdb_id);
    }

    return {};
}

// JikanService implementation
JikanService::JikanService(std::shared_ptr<HttpClient> http_client)
    : m_http_client(std::move(http_client)) {
}

std::expected<std::string, core::PlexError> JikanService::fetch_artwork_url(
    const std::string& mal_id,
    core::MediaType type) {
	(void)type;
    LOG_DEBUG("Jikan", "fetch_artwork_url() called for MAL ID: " + mal_id);

    // MAL ID directly provides artwork through the anime detail endpoint
    std::string url = std::string(JIKAN_API_URL) + "/" + mal_id;

    auto response = m_http_client->get(url, {});
    if (!response.has_value()) {
        LOG_ERROR("Jikan", "Failed to fetch MAL data for ID: " + mal_id + " - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    if (!response.value().is_success()) {
        LOG_ERROR("Jikan", "Failed to fetch MAL data for ID: " + mal_id);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response.value().body);
    if (!json_result) {
        LOG_ERROR("Jikan", "Error parsing Jikan response: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();
    if (utils::JsonHelper::has_field(json_response, "data")) {
        auto data = json_response["data"];
        if (utils::JsonHelper::has_field(data, "images")) {
            auto images = data["images"];
            if (utils::JsonHelper::has_field(images, "jpg")) {
                auto jpg = images["jpg"];
                if (utils::JsonHelper::has_field(jpg, "large_image_url")) {
                    std::string artwork_url = jpg["large_image_url"];
                    LOG_INFO("Jikan", "Found artwork for MAL ID " + mal_id + ": " + artwork_url);
                    return artwork_url;
                }
            }
        }
    }

    return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
}

std::expected<void, core::PlexError> JikanService::enrich_media_info(core::MediaInfo& info) {
    LOG_DEBUG("Jikan", "enrich_media_info() called for: " + info.title);
    // Check if this is anime content
    bool is_anime = std::any_of(info.genres.begin(), info.genres.end(),
        [](const std::string& genre) { return genre == "Anime"; });
    LOG_DEBUG("Jikan", std::string("Is anime content: ") + (is_anime ? "true" : "false"));

    if (!is_anime) {
        return {}; // Not anime, nothing to enrich
    }

    // If we don't have MAL ID, try to search for it
    if (info.mal_id.empty()) {
        // For TV shows, use the show title (grandparent_title) instead of episode title
        std::string search_title = (info.type == core::MediaType::TVShow && !info.grandparent_title.empty())
            ? info.grandparent_title
            : info.title;

        LOG_DEBUG("Jikan", "Searching for anime with title: " + search_title);
        auto search_result = search_anime_by_title(search_title, info.year);
        if (search_result.has_value()) {
            info.mal_id = search_result.value();
        }
    }

    // Fetch artwork if we have MAL ID
    if (!info.mal_id.empty()) {
        auto artwork_result = fetch_artwork_url(info.mal_id, info.type);
        if (artwork_result.has_value()) {
            info.art_path = artwork_result.value();
            LOG_DEBUG("Jikan", "Set art_path from Jikan: " + info.art_path);
        } else {
            LOG_WARNING("Jikan", "Failed to fetch artwork for MAL ID: " + info.mal_id);
        }
    }

    return {};
}

std::expected<std::string, core::PlexError> JikanService::search_anime_by_title(
    const std::string& title,
    int year) {

    std::string encoded_title = url_encode(title);
    std::string url = std::string(JIKAN_API_URL) + "?q=" + encoded_title;

    if (year > 0) {
        url += "&start_date=" + std::to_string(year) + "-01-01";
        url += "&end_date=" + std::to_string(year) + "-12-31";
    }

    auto response = m_http_client->get(url, {});
    if (!response.has_value()) {
        LOG_ERROR("Jikan", "Failed to search anime: " + title + " - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    if (!response.value().is_success()) {
        LOG_ERROR("Jikan", "Failed to search anime: " + title);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response.value().body);
    if (!json_result) {
        LOG_ERROR("Jikan", "Error parsing search response: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();
    if (utils::JsonHelper::has_array(json_response, "data")) {
        auto first_result = json_response["data"][0];
        auto mal_id_result = utils::JsonHelper::get_required<int>(first_result, "mal_id");
        if (mal_id_result) {
            std::string mal_id = std::to_string(mal_id_result.value());
            LOG_INFO("Jikan", "Found MAL ID for " + title + ": " + mal_id);
            return mal_id;
        }
    }

    return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
}

std::string JikanService::url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

// PlexClient implementation
PlexClient::PlexClient(
    std::shared_ptr<HttpClient> http_client,
    std::string username)
    : m_http_client(std::move(http_client))
    , m_target_username(std::move(username)) {

    LOG_DEBUG("PlexClient", "Creating Plex client");
}

void PlexClient::add_metadata_service(std::unique_ptr<IExternalMetadataService> service) {
    LOG_DEBUG("PlexClient", "Adding external metadata service");
    m_external_services.push_back(std::move(service));
}

// Media fetching methods
std::expected<core::MediaInfo, core::PlexError> PlexClient::fetch_media_details(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    const std::string& media_key) {

    LOG_DEBUG("PlexClient", "Fetching media details for key: " + media_key);

    // Check cache first
    std::string cache_key = server_uri + media_key;
    auto cached_info = get_cached_media_info(cache_key);
    if (cached_info.has_value()) {
        LOG_DEBUG("PlexClient", "Using cached media info for: " + media_key);
        return cached_info.value();
    }

    // Fetch from server
    std::string url = server_uri + media_key;
    auto headers_map = get_standard_headers(access_token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get(url, headers);
    if (!response.has_value()) {
        LOG_ERROR("PlexClient", "Failed to fetch media details from: " + url + " - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    if (!response.value().is_success()) {
        LOG_ERROR("PlexClient", "Failed to fetch media details from: " + url);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response.value().body);
    if (!json_result) {
        LOG_ERROR("PlexClient", "Failed to parse media details: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    if (!utils::JsonHelper::has_field(json_response, "MediaContainer")) {
        LOG_ERROR("PlexClient", "Invalid media details response: missing MediaContainer");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto container = json_response["MediaContainer"];
    if (!utils::JsonHelper::has_array(container, "Metadata")) {
        LOG_ERROR("PlexClient", "Invalid media details response: missing or empty Metadata");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto metadata = container["Metadata"][0];

    core::MediaInfo info;
    extract_basic_media_info(metadata, info);

    // Extract type-specific information
    extract_type_specific_info(metadata, info);

    // For TV shows, fetch grandparent metadata before enrichment to get TMDB/IMDB IDs
    if (info.type == core::MediaType::TVShow && !info.grandparent_key.empty()) {
        LOG_DEBUG("PlexClient", "Fetching grandparent metadata before enrichment");
        auto grandparent_result = fetch_grandparent_metadata(server_uri, access_token, info);
        if (!grandparent_result.has_value()) {
            LOG_WARNING("PlexClient", "Failed to fetch grandparent metadata, continuing without it");
        }
    }

    // Enrich with external services
    enrich_with_external_services(info);

    // Cache the result
    cache_media_info(cache_key, info);

    LOG_DEBUG("PlexClient", "Successfully fetched media: " + info.title);
    return info;
}

std::expected<void, core::PlexError> PlexClient::fetch_grandparent_metadata(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    core::MediaInfo& info) {

    LOG_DEBUG("PlexClient", "fetch_grandparent_metadata() called for: " + info.title + " (grandparent key: " + info.grandparent_key + ")");

    if (info.grandparent_key.empty()) {
        LOG_ERROR("PlexClient", "No grandparent key available");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    LOG_DEBUG("PlexClient", "Fetching grandparent metadata for: " + info.grandparent_key);

    std::string url = server_uri + info.grandparent_key;
    auto headers_map = get_standard_headers(access_token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get(url, headers);
    if (!response.has_value()) {
        LOG_ERROR("PlexClient", "Failed to fetch grandparent metadata - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    if (!response.value().is_success()) {
        LOG_ERROR("PlexClient", "Failed to fetch grandparent metadata");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response.value().body);
    if (!json_result) {
        LOG_ERROR("PlexClient", "Failed to parse grandparent metadata: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    if (!utils::JsonHelper::has_field(json_response, "MediaContainer")) {
        LOG_ERROR("PlexClient", "Invalid grandparent metadata response: missing MediaContainer");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto container = json_response["MediaContainer"];
    if (!utils::JsonHelper::has_array(container, "Metadata")) {
        LOG_ERROR("PlexClient", "Invalid grandparent metadata response: missing or empty Metadata");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto metadata = container["Metadata"][0];

    // Extract GUIDs from show metadata
    utils::JsonHelper::for_each_in_array(metadata, "Guid", [&info](const auto& guid) {
        std::string id = utils::JsonHelper::get_optional<std::string>(guid, "id", "");
        if (id.find("imdb://") == 0) {
            info.imdb_id = id.substr(7);
        } else if (id.find("tmdb://") == 0) {
            info.tmdb_id = id.substr(7);
        }
    });

    // Extract genres
    if (utils::JsonHelper::has_array(metadata, "Genre")) {
        info.genres.clear();
        utils::JsonHelper::for_each_in_array(metadata, "Genre", [&info](const auto& genre) {
            info.genres.push_back(utils::JsonHelper::get_optional<std::string>(genre, "tag", ""));
        });
    }

    return {};
}

void PlexClient::extract_basic_media_info(const nlohmann::json& metadata, core::MediaInfo& info) const {
    info.title = metadata.value("title", "Unknown");
    info.original_title = metadata.value("originalTitle", info.title);
    info.duration = metadata.value("duration", 0) / 1000.0; // Convert from milliseconds to seconds
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

void PlexClient::extract_type_specific_info(const nlohmann::json& metadata, core::MediaInfo& info) const {
    extract_type_specific_info_helper(metadata, info);
}

void PlexClient::enrich_with_external_services(core::MediaInfo& info) {
    for (const auto& service : m_external_services) {
        auto result = service->enrich_media_info(info);
        if (!result.has_value()) {
            LOG_DEBUG("PlexClient", "External service enrichment failed");
        }
    }
}

std::map<std::string, std::string> PlexClient::get_standard_headers(const core::PlexToken& token) const {
    return utils::PlexHeadersBuilder::create_authenticated_headers("presence-for-plex", token);
}

// Session management methods
void PlexClient::update_server_connection(
    const core::ServerId& server_id,
    const ServerConnectionInfo& connection_info) {

    LOG_DEBUG("PlexClient", "update_server_connection() called for server: " + server_id.get() + ", URI: " + connection_info.preferred_uri);

    std::lock_guard<std::mutex> lock(m_sessions_mutex);
    m_server_connections[server_id] = connection_info;

    LOG_DEBUG("PlexClient", "Updated connection info for server: " + server_id.get());
}

void PlexClient::process_session_event(
    const core::ServerId& server_id,
    const nlohmann::json& notification) {

    LOG_DEBUG("PlexClient", "Processing PlaySessionStateNotification");

    // Extract essential session information
    std::string session_key_str = notification.value("sessionKey", "");
    std::string state = notification.value("state", "");
    std::string media_key = notification.value("key", "");
    int64_t view_offset = notification.value("viewOffset", 0);

    if (session_key_str.empty()) {
        LOG_WARNING("PlexClient", "Session notification missing sessionKey");
        return;
    }

    core::SessionKey session_key(session_key_str);
    LOG_DEBUG("PlexClient", "Processing session " + session_key.get() + " state: " + state);

    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    if (state == "playing" || state == "paused" || state == "buffering") {
        update_session_info(server_id, session_key, state, media_key, view_offset);
    } else if (state == "stopped") {
        // Remove the session if it exists
        auto session_it = m_active_sessions.find(session_key);
        if (session_it != m_active_sessions.end()) {
            LOG_DEBUG("PlexClient", "Removing stopped session: " + session_key.get());
            m_active_sessions.erase(session_it);

            // Notify callback about state change if this was the current session
            if (m_session_callback) {
                auto current = find_most_recent_session();
                m_session_callback(current);
            }
        }
    }
}

std::optional<core::MediaInfo> PlexClient::get_current_playback() const {
    LOG_DEBUG("PlexClient", "get_current_playback() called");
    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    if (m_active_sessions.empty()) {
        LOG_DEBUG("PlexClient", "No active sessions");
        return std::nullopt;
    }

    auto current = find_most_recent_session();
    if (current.state == core::PlaybackState::Stopped) {
        return std::nullopt;
    }

    LOG_DEBUG("PlexClient", "Current playback: " + current.title +
                   " (state: " + std::to_string(static_cast<int>(current.state)) + ")");

    return current;
}

std::expected<std::vector<core::MediaInfo>, core::PlexError> PlexClient::get_active_sessions() const {
    LOG_DEBUG("PlexClient", "get_active_sessions() called, total sessions in map: " + std::to_string(m_active_sessions.size()));
    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    std::vector<core::MediaInfo> sessions;
    sessions.reserve(m_active_sessions.size());

    for (const auto& [session_key, info] : m_active_sessions) {
        if (info.state != core::PlaybackState::Stopped) {
            sessions.push_back(info);
        }
    }

    return sessions;
}

void PlexClient::set_target_username(const std::string& username) {
    LOG_DEBUG("PlexClient", "set_target_username() called with: " + username);
    m_target_username = username;
    LOG_DEBUG("PlexClient", "Target username set to: " + username);
}

std::string PlexClient::get_target_username() const {
    return m_target_username;
}

void PlexClient::set_session_state_callback(SessionStateCallback callback) {
    m_session_callback = std::move(callback);
}

void PlexClient::clear_all() {
    std::lock_guard<std::mutex> sessions_lock(m_sessions_mutex);
    std::lock_guard<std::mutex> state_lock(m_state_mutex);
    std::lock_guard<std::mutex> cache_lock(m_cache_mutex);

    m_active_sessions.clear();
    m_server_connections.clear();
    m_last_reported_state.reset();

    // Clear all caches
    size_t media_count = m_media_cache.size();
    size_t session_count = m_session_user_cache.size();

    m_media_cache.clear();
    m_session_user_cache.clear();

    LOG_INFO("PlexClient", "All sessions and caches cleared - Media: " +
             std::to_string(media_count) + ", Sessions: " + std::to_string(session_count));
}

void PlexClient::remove_sessions_for_server(const core::ServerId& server_id) {
    LOG_DEBUG("PlexClient", "remove_sessions_for_server() called for: " + server_id.get());
    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    auto it = m_active_sessions.begin();
    while (it != m_active_sessions.end()) {
        if (it->second.server_id.get() == server_id.get()) {
            LOG_DEBUG("PlexClient", "Removing session for server " + server_id.get() + ": " + it->first.get());
            it = m_active_sessions.erase(it);
        } else {
            ++it;
        }
    }

    m_server_connections.erase(server_id);
}

void PlexClient::update_session_info(
    const core::ServerId& server_id,
    const core::SessionKey& session_key,
    const std::string& state,
    const std::string& media_key,
    int64_t view_offset) {

    // Check if we should process this session (user filtering)
    if (!should_process_session(server_id, session_key)) {
        LOG_DEBUG("PlexClient", "Skipping session (user filter): " + session_key.get());
        return;
    }

    // Get server connection info
    auto server_it = m_server_connections.find(server_id);
    if (server_it == m_server_connections.end()) {
        LOG_ERROR("PlexClient", "No connection info for server: " + server_id.get());
        return;
    }

    const auto& connection_info = server_it->second;

    // Fetch or use cached media info
    core::MediaInfo info;
    bool has_existing_session = m_active_sessions.find(session_key) != m_active_sessions.end();

    if (has_existing_session) {
        // Update existing session
        info = m_active_sessions[session_key];
        LOG_DEBUG("PlexClient", "Updating existing session: " + session_key.get());
    } else {
        // Fetch new media details
        auto fetch_result = fetch_media_details(
            connection_info.preferred_uri,
            connection_info.access_token,
            media_key
        );

        if (!fetch_result.has_value()) {
            LOG_ERROR("PlexClient", "Failed to fetch media details for session: " + session_key.get());
            return;
        }

        info = fetch_result.value();
        // Set session creation time for new sessions
        info.session_created_at = std::chrono::system_clock::now();
        LOG_DEBUG("PlexClient", "Fetched new media info for session: " + session_key.get());
    }

    // Update playback state
    update_playback_state(info, state, view_offset);

    // Update session and server information
    info.session_key = session_key;
    info.server_id = server_id;

    // Store the updated info
    bool is_new_session = !has_existing_session;
    m_active_sessions[session_key] = info;

    LOG_DEBUG("PlexClient",
                  (is_new_session ? "Added" : "Updated") + std::string(" session ") + session_key.get() +
                  ": " + info.title + " (" + std::to_string(info.progress) + "/" +
                  std::to_string(info.duration) + "s)");

    // Notify callback about state change
    if (m_session_callback) {
        m_session_callback(info);
    }
}

void PlexClient::update_playback_state(
    core::MediaInfo& info,
    const std::string& state,
    int64_t view_offset) {

    if (state == "playing") {
        info.state = core::PlaybackState::Playing;
    } else if (state == "paused") {
        info.state = core::PlaybackState::Paused;
    } else if (state == "buffering") {
        info.state = core::PlaybackState::Buffering;
    } else {
        info.state = core::PlaybackState::Stopped;
    }

    info.progress = static_cast<double>(view_offset) / 1000.0; // Convert from milliseconds to seconds
    info.start_time = std::chrono::system_clock::now() -
                     std::chrono::seconds(static_cast<long>(info.progress));
}

bool PlexClient::should_process_session(
    const core::ServerId& server_id,
    const core::SessionKey& session_key) {

    // Get server connection info
    auto server_it = m_server_connections.find(server_id);
    if (server_it == m_server_connections.end()) {
        LOG_WARNING("PlexClient", "No connection info found for server: " + server_id.get());
        return false;
    }

    const auto& connection_info = server_it->second;

    LOG_DEBUG("PlexClient", "Checking session " + session_key.get() +
                   " for server " + server_id.get() +
                   " (owned: " + std::to_string(connection_info.owned) +
                   ", target_user: " + m_target_username + ")");

    // For owned servers, validate the user
    if (connection_info.owned) {
        LOG_DEBUG("PlexClient", "Validating session user for owned server");
        auto validation_result = validate_session_user(
            connection_info.preferred_uri,
            connection_info.access_token,
            session_key
        );

        if (!validation_result.has_value()) {
            LOG_DEBUG("PlexClient", "Session validation failed: " + session_key.get());
            return false;
        }

        bool should_process = validation_result.value();
        return should_process;
    }

    // For shared servers, process all sessions
    LOG_DEBUG("PlexClient", "Processing all sessions for shared server");
    return true;
}

core::MediaInfo PlexClient::find_most_recent_session() const {
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

std::expected<bool, core::PlexError> PlexClient::validate_session_user(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    const core::SessionKey& session_key) {

    LOG_DEBUG("PlexClient", "validate_session_user() called for session: " + session_key.get() + ", target: " + m_target_username);

    if (m_target_username.empty()) {
        LOG_DEBUG("PlexClient", "No target username specified, allowing all sessions");
        return true; // No filtering required
    }

    auto username_result = fetch_session_username(server_uri, access_token, session_key);
    if (!username_result.has_value()) {
        return std::unexpected<core::PlexError>(username_result.error());
    }

    bool is_valid = username_result.value() == m_target_username;
    LOG_DEBUG("PlexClient", "Session " + session_key.get() +
                   " user validation: " + (is_valid ? "PASS" : "FAIL") +
                   " (user: " + username_result.value() + ", target: " + m_target_username + ")");

    return is_valid;
}

std::expected<std::string, core::PlexError> PlexClient::fetch_session_username(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    const core::SessionKey& session_key) {

    // Check cache first
    std::string cache_key = server_uri + session_key.get();
    auto cached_username = get_cached_session_user(cache_key);
    if (cached_username.has_value()) {
        LOG_DEBUG("PlexClient", "Using cached username for session: " + session_key.get());
        return cached_username.value();
    }

    LOG_DEBUG("PlexClient", "Fetching username for session: " + session_key.get());

    std::string url = server_uri + SESSION_ENDPOINT;
    auto headers_map = get_standard_headers(access_token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get(url, headers);
    if (!response || !response->is_success()) {
        LOG_ERROR("PlexClient", "Failed to fetch session information");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response->body);
    if (!json_result) {
        LOG_ERROR("PlexClient", "Failed to parse session data: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    if (!utils::JsonHelper::has_field(json_response, "MediaContainer")) {
        LOG_ERROR("PlexClient", "Invalid session response format: missing MediaContainer");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    auto container = json_response["MediaContainer"];

    int size = utils::JsonHelper::get_optional<int>(container, "size", 0);
    if (size == 0) {
        LOG_DEBUG("PlexClient", "No active sessions found");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    if (!utils::JsonHelper::has_array(container, "Metadata")) {
        LOG_DEBUG("PlexClient", "No session metadata found");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    // Find the matching session by sessionKey
    std::string found_username;
    bool found = false;

    utils::JsonHelper::for_each_in_array(container, "Metadata", [&](const auto& session) {
        if (found) return;

        std::string current_session_key = utils::JsonHelper::get_optional<std::string>(session, "sessionKey", "");
        if (current_session_key == session_key.get()) {
            // Extract user info
            if (utils::JsonHelper::has_field(session, "User")) {
                auto user = session["User"];
                if (utils::JsonHelper::has_field(user, "title")) {
                    found_username = user["title"].get<std::string>();
                    found = true;
                    LOG_DEBUG("PlexClient", "Found user for session " + session_key.get() + ": " + found_username);
                }
            }
        }
    });

    if (found) {
        // Cache the result
        cache_session_user(cache_key, found_username);
        return found_username;
    }

    LOG_WARNING("PlexClient", "Session not found or no user info: " + session_key.get());
    return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
}

// Cache management methods
void PlexClient::cache_media_info(const std::string& key, const core::MediaInfo& info) {
    LOG_DEBUG("PlexClient", "cache_media_info() called for: " + info.title);
    std::lock_guard<std::mutex> lock(m_cache_mutex);

    CacheEntry<core::MediaInfo> entry;
    entry.data = info;
    entry.timestamp = std::chrono::system_clock::now();
    entry.ttl = MEDIA_CACHE_TIMEOUT;

    m_media_cache[key] = entry;

    LOG_DEBUG("PlexClient", "Cached media info for key: " + key);
}

std::optional<core::MediaInfo> PlexClient::get_cached_media_info(const std::string& key) {
    LOG_DEBUG("PlexClient", "get_cached_media_info() called for key: " + key.substr(0, 50) + "...");
    std::lock_guard<std::mutex> lock(m_cache_mutex);

    auto it = m_media_cache.find(key);
    if (it != m_media_cache.end() && it->second.is_valid()) {
        LOG_DEBUG("PlexClient", "Cache hit for media info: " + key);
        return it->second.data;
    }

    LOG_DEBUG("PlexClient", "Cache miss for media info: " + key);
    return std::nullopt;
}

void PlexClient::cache_session_user(const std::string& key, const std::string& username) {
    std::lock_guard<std::mutex> lock(m_cache_mutex);

    CacheEntry<std::string> entry;
    entry.data = username;
    entry.timestamp = std::chrono::system_clock::now();
    entry.ttl = SESSION_CACHE_TIMEOUT;

    m_session_user_cache[key] = entry;

    LOG_DEBUG("PlexClient", "Cached session user for key: " + key);
}

std::optional<std::string> PlexClient::get_cached_session_user(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_cache_mutex);

    auto it = m_session_user_cache.find(key);
    if (it != m_session_user_cache.end() && it->second.is_valid()) {
        LOG_DEBUG("PlexClient", "Cache hit for session user: " + key);
        return it->second.data;
    }

    LOG_DEBUG("PlexClient", "Cache miss for session user: " + key);
    return std::nullopt;
}

} // namespace services
} // namespace presence_for_plex
