#include "presence_for_plex/services/plex/plex_service.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <cassert>
#include <stdexcept>
#include <unordered_set>
#include <chrono>

namespace presence_for_plex {
namespace services {

using json = nlohmann::json;

// Constructor with dependency injection
PlexService::PlexService(
    std::shared_ptr<PlexAuthenticator> authenticator,
    std::shared_ptr<PlexConnectionManager> connection_manager,
    std::shared_ptr<PlexClient> client,
    std::shared_ptr<HttpClient> http_client,
    std::shared_ptr<core::ConfigManager> config_service,
    std::shared_ptr<PlexAuthStorage> auth_service)
    : m_authenticator(std::move(authenticator))
    , m_connection_manager(std::move(connection_manager))
    , m_client(std::move(client))
    , m_http_client(std::move(http_client))
    , m_config_service(std::move(config_service))
    , m_auth_service(std::move(auth_service)) {

    LOG_INFO("PlexService", "Creating Plex service with simplified dependencies");

    // Set up SSE event callback
    m_connection_manager->set_sse_event_callback(
        [this](const core::ServerId& server_id, const std::string& event) {
            handle_sse_event(server_id, event);
        }
    );

    // Set up connection state callback
    m_connection_manager->set_connection_state_callback(
        [this](const core::ServerId& server_id, bool connected, const std::string& uri) {
            on_connection_state_changed(server_id, connected);
            if (connected && m_client) {
                ServerConnectionInfo conn_info;
                conn_info.preferred_uri = uri;
                conn_info.access_token = "";  // Will be filled below
                conn_info.owned = false;  // Will be filled below

                // Get server details from connection manager
                std::lock_guard<std::mutex> lock(m_server_tokens_mutex);
                auto token_it = m_server_tokens.find(server_id);
                if (token_it != m_server_tokens.end()) {
                    conn_info.access_token = token_it->second.token;
                    conn_info.owned = token_it->second.owned;
                }

                m_client->update_server_connection(server_id, conn_info);
                LOG_DEBUG("PlexService", "Updated client with connected URI for server: " + server_id.get());
            }
        }
    );

    // Set up session state callback
    m_client->set_session_state_callback(
        [this](const core::MediaInfo& info) {
            if (m_event_bus) {
                // Track and pass previous state for proper transitions
                const auto previous_state = m_last_media_state;
                publish_media_updated(previous_state, info);
                m_last_media_state = info;
            }
        }
    );
}

PlexService::~PlexService() {
    if (m_running) {
        stop();
    }
    LOG_INFO("PlexService", "Plex service destroyed");
}

std::expected<void, core::PlexError> PlexService::start() {
    LOG_INFO("PlexService", "Starting Plex service");

    if (m_running) {
        LOG_WARNING("PlexService", "Service already running");
        return {};
    }

    // Ensure authentication before starting connections
    auto auth_result = m_authenticator->ensure_authenticated();
    if (!auth_result) {
        LOG_ERROR("PlexService", "Failed to authenticate with Plex");
        return std::unexpected<core::PlexError>(auth_result.error());
    }

    auto token = auth_result.value();

    // Fetch and store username
    auto username_result = m_authenticator->fetch_username(token);
    if (username_result) {
        m_plex_username = username_result.value();
        LOG_INFO("PlexService", "Logged in as: " + m_plex_username);

        // Set the target username in the client for filtering
        if (m_client) {
            m_client->set_target_username(m_plex_username);
            LOG_DEBUG("PlexService", "Set target username for session filtering: " + m_plex_username);
        }
    }

    // Discover servers from Plex API if auto-discovery is enabled
    if (m_config_service && m_config_service->get().media_services.plex.auto_discover) {
        LOG_INFO("PlexService", "Auto-discovery enabled, discovering servers from Plex API");
        auto discovery_result = discover_servers(token);
        if (!discovery_result) {
            LOG_WARNING("PlexService", "Failed to discover servers, continuing anyway");
        }
    } else {
        LOG_INFO("PlexService", "Auto-discovery disabled, skipping server discovery");
    }

    // Add manual servers from config
    if (m_config_service) {
        const auto& manual_urls = m_config_service->get().media_services.plex.server_urls;
        if (!manual_urls.empty()) {
            LOG_INFO("PlexService", "Adding " + std::to_string(manual_urls.size()) + " manual server(s)");
            for (const auto& url : manual_urls) {
                auto add_result = add_manual_server(url, token);
                if (!add_result) {
                    LOG_WARNING("PlexService", "Failed to add manual server: " + url);
                }
            }
        }
    }

    // Start all connections - servers will self-manage and connect asynchronously
    m_connection_manager->start_all_connections();
    LOG_INFO("PlexService", "Server connections initiated - they will connect asynchronously");

    m_running = true;
    return {};
}

void PlexService::stop() {
    LOG_INFO("PlexService", "Stopping Plex service");

    m_running = false;

    // Shutdown authenticator to abort any ongoing operations
    if (m_authenticator) {
        m_authenticator->shutdown();
    }

    // Stop all connections
    m_connection_manager->stop_all_connections();

    // Clear caches
    m_client->clear_all();

    // Clear last media state
    m_last_media_state = core::MediaInfo{};
}

bool PlexService::is_running() const {
    return m_running;
}

void PlexService::set_poll_interval(std::chrono::seconds interval) {
    m_poll_interval = interval;
}

std::chrono::seconds PlexService::get_poll_interval() const {
    return m_poll_interval;
}

void PlexService::set_event_bus(std::shared_ptr<core::EventBus> bus) {
    m_event_bus = std::move(bus);
}


std::expected<core::MediaInfo, core::PlexError> PlexService::get_current_media() const {
    LOG_DEBUG("PlexService", "get_current_media() called");

    if (!m_running) {
        LOG_DEBUG("PlexService", "get_current_media() failed - service not running");
        return std::unexpected<core::PlexError>(core::PlexError::NotInitialized);
    }

    auto current = m_client->get_current_playback();
    if (current.has_value()) {
        // Check if this media type is enabled in configuration
        if (!is_media_type_enabled(current.value().type)) {
            LOG_DEBUG("PlexService", "get_current_media() media type filtered out: " + current.value().title);
            core::MediaInfo stopped_info;
            stopped_info.state = core::PlaybackState::Stopped;
            return stopped_info;
        }

        LOG_DEBUG("PlexService", "get_current_media() returning current playback: " + current.value().title);
        return current.value();
    }

    LOG_DEBUG("PlexService", "get_current_media() no active playback, returning stopped state");
    core::MediaInfo stopped_info;
    stopped_info.state = core::PlaybackState::Stopped;
    return stopped_info;
}

std::expected<std::vector<core::MediaInfo>, core::PlexError> PlexService::get_active_sessions() const {
    LOG_DEBUG("PlexService", "get_active_sessions() called");

    if (!m_running) {
        LOG_DEBUG("PlexService", "get_active_sessions() failed - service not running");
        return std::unexpected<core::PlexError>(core::PlexError::NotInitialized);
    }

    auto sessions = m_client->get_active_sessions();
    if (sessions.has_value()) {
        // Filter sessions based on media type configuration
        std::vector<core::MediaInfo> filtered_sessions;
        for (const auto& session : sessions.value()) {
            if (is_media_type_enabled(session.type)) {
                filtered_sessions.push_back(session);
            } else {
                LOG_DEBUG("PlexService", "get_active_sessions() filtered out session: " + session.title);
            }
        }

        LOG_DEBUG("PlexService", "get_active_sessions() returning " +
                      std::to_string(filtered_sessions.size()) + " sessions (filtered from " +
                      std::to_string(sessions.value().size()) + ")");
        return filtered_sessions;
    } else {
        LOG_DEBUG("PlexService", "get_active_sessions() failed to get sessions from session manager");
    }
    return sessions;
}

std::expected<void, core::PlexError> PlexService::add_server(std::unique_ptr<core::PlexServer> server) {
    if (!server) {
        LOG_ERROR("PlexService", "add_server() called with null server");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    LOG_DEBUG("PlexService", "add_server() called for server: " + server->name + " (" + server->client_identifier + ")");

    // Store server details before moving ownership
    core::ServerId server_id(server->client_identifier);
    core::PlexToken server_token = server->access_token;
    bool is_owned = server->owned;

    // Store server token and ownership for later use in connection callbacks
    {
        std::lock_guard<std::mutex> lock(m_server_tokens_mutex);
        m_server_tokens[server_id] = {server_token, is_owned};
    }

    auto result = m_connection_manager->add_server(std::move(server));

    if (result) {
        LOG_DEBUG("PlexService", "add_server() succeeded");
    } else {
        LOG_DEBUG("PlexService", "add_server() failed");
    }

    return result;
}

void PlexService::remove_server(const core::ServerId& server_id) {
    LOG_DEBUG("PlexService", "remove_server() called for server: " + server_id.get());
    m_connection_manager->remove_server(server_id);
    LOG_DEBUG("PlexService", "remove_server() completed for server: " + server_id.get());
}

std::vector<core::ServerId> PlexService::get_connected_servers() const {
    return m_connection_manager->get_connected_servers();
}

bool PlexService::is_server_connected(const core::ServerId& server_id) const {
    return m_connection_manager->is_server_connected(server_id);
}

void PlexService::on_media_state_changed(const core::MediaInfo& old_state, const core::MediaInfo& new_state) {
    LOG_INFO("PlexService", "Media state changed: " + new_state.title);

    if (m_event_bus) {
        publish_media_updated(old_state, new_state);
        // Keep internal state in sync
        m_last_media_state = new_state;
    }
}

void PlexService::on_connection_state_changed(const core::ServerId& server_id, bool connected) {
    LOG_INFO("PlexService", "Server " + server_id.get() + " connection state: " +
                  (connected ? "connected" : "disconnected"));

    if (m_event_bus) {
        if (connected) {
            publish_server_connected(server_id, server_id.value);
        } else {
            publish_server_disconnected(server_id, "Connection lost");
        }
    }
}

void PlexService::on_error_occurred(core::PlexError error, const std::string& message) {
    LOG_ERROR("PlexService", "Error occurred: " + message);

    if (m_event_bus) {
        publish_media_error(error, message);
    }
}

void PlexService::handle_sse_event(const core::ServerId& server_id, const std::string& event) {
    try {
        auto json_event = json::parse(event);

        LOG_DEBUG("PlexService", "Received event from server " + server_id.get());

        if (json_event.contains("PlaySessionStateNotification")) {
            m_client->process_session_event(server_id, json_event["PlaySessionStateNotification"]);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("PlexService", "Error parsing SSE event: " + std::string(e.what()));
        on_error_occurred(core::PlexError::ParseError, "Failed to parse SSE event");
    }
}

std::expected<void, core::PlexError> PlexService::discover_servers(const std::string& auth_token) {
    LOG_INFO("PlexService", "Discovering Plex servers");

    if (!m_http_client) {
        LOG_ERROR("PlexService", "HTTP client not available for server discovery");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    // Use authenticator's standard headers which include the correct client identifier
    auto std_headers = m_authenticator->get_standard_headers(auth_token);
    HttpHeaders headers(std_headers.begin(), std_headers.end());

    // Make request to Plex resources endpoint
    const std::string resources_url = "https://plex.tv/api/v2/resources?includeHttps=1";
    auto response = m_http_client->get(resources_url, headers);

    if (!response || !response->is_success()) {
        LOG_ERROR("PlexService", "Failed to fetch servers from Plex.tv");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    LOG_DEBUG("PlexService", "Received server response from Plex.tv");

    // Parse the JSON response and add servers
    auto parse_result = parse_server_json(response->body, auth_token);
    if (!parse_result) {
        LOG_ERROR("PlexService", "Failed to parse server response");
        return std::unexpected<core::PlexError>(parse_result.error());
    }

    return {};
}

std::expected<void, core::PlexError> PlexService::parse_server_json(const std::string& json_response, const std::string& auth_token) {
    (void)auth_token;
    LOG_INFO("PlexService", "Parsing server JSON response");

    try {
        auto json = json::parse(json_response);

        int server_count = 0;

        // Process each resource (server)
        for (const auto& resource : json) {
            // Check if this is a Plex Media Server
            std::string provides = resource.value("provides", "");
            if (provides != "server") {
                continue;
            }

            auto server = std::make_unique<core::PlexServer>();
            server->name = resource.value("name", "Unknown");
            server->client_identifier = resource.value("clientIdentifier", "");
            server->access_token = resource.value("accessToken", "");
            server->owned = resource.value("owned", false);

            LOG_INFO("PlexService", "Found server: " + server->name +
                         " (" + server->client_identifier + ")" +
                         (server->owned ? " [owned]" : " [shared]"));

            // Process connections (we want both local and remote)
            if (resource.contains("connections") && resource["connections"].is_array()) {
                for (const auto& connection : resource["connections"]) {
                    std::string uri = connection.value("uri", "");
                    bool is_local = connection.value("local", false);

                    if (is_local && !uri.empty()) {
                        server->local_uri = uri;
                        LOG_INFO("PlexService", "  Local URI: " + uri);
                    } else if (!is_local && !uri.empty()) {
                        server->public_uri = uri;
                        LOG_INFO("PlexService", "  Public URI: " + uri);
                    }
                }
            }

            // Add server if it has at least one valid URI
            if (!server->local_uri.empty() || !server->public_uri.empty()) {
                auto add_result = add_server(std::move(server));
                if (add_result) {
                    server_count++;
                } else {
                    LOG_WARNING("PlexService", "Failed to add server: " + server->name);
                }
            }
        }

        LOG_INFO("PlexService", "Successfully discovered and added " + std::to_string(server_count) + " Plex servers");
        return {};

    } catch (const std::exception& e) {
        LOG_ERROR("PlexService", "Failed to parse server JSON: " + std::string(e.what()));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }
}

std::expected<void, core::PlexError> PlexService::add_manual_server(const std::string& server_url, const core::PlexToken& auth_token) {
    LOG_INFO("PlexService", "Adding manual server: " + server_url);

    if (!m_http_client) {
        LOG_ERROR("PlexService", "HTTP client not available for manual server");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    // Fetch server identity from /identity endpoint
    std::string identity_url = server_url;
    if (identity_url.back() != '/') {
        identity_url += '/';
    }
    identity_url += "identity";

    // Use authenticator's standard headers
    auto std_headers = m_authenticator->get_standard_headers(auth_token);
    HttpHeaders headers(std_headers.begin(), std_headers.end());

    LOG_DEBUG("PlexService", "Fetching server identity from: " + identity_url);
    auto response = m_http_client->get(identity_url, headers);

    if (!response || !response->is_success()) {
        LOG_ERROR("PlexService", "Failed to fetch server identity from: " + identity_url);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    try {
        auto json_response = json::parse(response->body);

        if (!json_response.contains("MediaContainer")) {
            LOG_ERROR("PlexService", "Invalid identity response from manual server");
            return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
        }

        auto container = json_response["MediaContainer"];

        std::string client_id = container.value("machineIdentifier", "");
        std::string friendly_name = container.value("friendlyName", "Manual Server");

        if (client_id.empty()) {
            LOG_ERROR("PlexService", "Server did not provide machineIdentifier");
            return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
        }

        LOG_INFO("PlexService", "Found manual server: " + friendly_name + " (" + client_id + ")");

        // Create PlexServer object
        auto server = std::make_unique<core::PlexServer>();
        server->name = friendly_name;
        server->client_identifier = client_id;
        server->access_token = auth_token;
        server->owned = true; // Assume owned for manual servers

        // Determine if this is a local or public URL based on address
        if (server_url.find("127.0.0.1") != std::string::npos ||
            server_url.find("localhost") != std::string::npos ||
            server_url.find("192.168.") != std::string::npos ||
            server_url.find("10.") != std::string::npos) {
            server->local_uri = server_url;
            LOG_DEBUG("PlexService", "Added as local URI");
        } else {
            server->public_uri = server_url;
            LOG_DEBUG("PlexService", "Added as public URI");
        }

        // Add the server
        return add_server(std::move(server));

    } catch (const std::exception& e) {
        LOG_ERROR("PlexService", "Error parsing identity response: " + std::string(e.what()));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }
}

bool PlexService::is_media_type_enabled(core::MediaType type) const {
    if (!m_config_service) {
        return true;
    }

    const auto& config = m_config_service->get();

    switch (type) {
        case core::MediaType::Movie:
            return config.media_services.plex.enable_movies;
        case core::MediaType::TVShow:
            return config.media_services.plex.enable_tv_shows;
        case core::MediaType::Music:
            return config.media_services.plex.enable_music;
        case core::MediaType::Unknown:
        default:
            return true;
    }
}


// Server loading from config has been removed - servers are always discovered fresh

// Server validation and saving to config has been removed

// Event publishing helpers
void PlexService::publish_media_started(const core::MediaInfo& info, const core::ServerId& server_id) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::MediaSessionStateChanged::started(info, server_id));
    }
}

void PlexService::publish_media_updated(const core::MediaInfo& old_info, const core::MediaInfo& new_info) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::MediaSessionStateChanged::updated(old_info, new_info));
    }
}

void PlexService::publish_media_ended(const core::SessionKey& key, const core::ServerId& server_id) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::MediaSessionStateChanged::ended(key, server_id));
    }
}

void PlexService::publish_server_connected(const core::ServerId& server_id, const std::string& name) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::ServerConnectionStateChanged::established(server_id, name));
    }
}

void PlexService::publish_server_disconnected(const core::ServerId& server_id, const std::string& reason) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::ServerConnectionStateChanged::lost(server_id, reason));
    }
}

void PlexService::publish_media_error(core::PlexError error, const std::string& message, const std::optional<core::ServerId>& server_id) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::MediaError{error, message, server_id});
    }
}

} // namespace services
} // namespace presence_for_plex
