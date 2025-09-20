#include "presence_for_plex/services/plex/plex_service_impl.hpp"
#include "presence_for_plex/services/network_service.hpp"
#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_set>
#include <thread>
#include <chrono>

namespace presence_for_plex {
namespace services {

using json = nlohmann::json;

// Constructor with dependency injection
PlexServiceImpl::PlexServiceImpl(
    std::shared_ptr<IPlexAuthenticator> authenticator,
    std::shared_ptr<IPlexCacheManager> cache_manager,
    std::shared_ptr<IPlexConnectionManager> connection_manager,
    std::shared_ptr<IPlexMediaFetcher> media_fetcher,
    std::shared_ptr<IPlexSessionManager> session_manager,
    std::shared_ptr<HttpClient> http_client,
    std::shared_ptr<core::ConfigurationService> config_service)
    : m_authenticator(std::move(authenticator))
    , m_cache_manager(std::move(cache_manager))
    , m_connection_manager(std::move(connection_manager))
    , m_media_fetcher(std::move(media_fetcher))
    , m_session_manager(std::move(session_manager))
    , m_http_client(std::move(http_client))
    , m_config_service(std::move(config_service)) {

    PLEX_LOG_INFO("PlexService", "Creating refactored Plex service with injected dependencies");

    // Set up SSE event callback
    m_connection_manager->set_sse_event_callback(
        [this](const core::ServerId& server_id, const std::string& event) {
            handle_sse_event(server_id, event);
        }
    );

    // Set up session state callback
    m_session_manager->set_session_state_callback(
        [this](const core::MediaInfo& info) {
            if (m_state_callback) {
                m_state_callback(info);
            }
        }
    );
}

PlexServiceImpl::~PlexServiceImpl() {
    if (m_running) {
        stop();
    }
    PLEX_LOG_INFO("PlexService", "Plex service destroyed");
}

std::expected<void, core::PlexError> PlexServiceImpl::start() {
    PLEX_LOG_INFO("PlexService", "Starting Plex service");

    if (m_running) {
        PLEX_LOG_WARNING("PlexService", "Service already running");
        return {};
    }

    // Ensure authentication before starting connections
    auto auth_result = m_authenticator->ensure_authenticated();
    if (!auth_result) {
        PLEX_LOG_ERROR("PlexService", "Failed to authenticate with Plex");
        return std::unexpected<core::PlexError>(auth_result.error());
    }

    auto token = auth_result.value();

    // Fetch and store username
    auto username_result = m_authenticator->fetch_username(token);
    if (username_result) {
        m_plex_username = username_result.value();
        PLEX_LOG_INFO("PlexService", "Logged in as: " + m_plex_username);

        // Set the target username in the session manager for filtering
        if (m_session_manager) {
            m_session_manager->set_target_username(m_plex_username);
            PLEX_LOG_DEBUG("PlexService", "Set target username for session filtering: " + m_plex_username);
        }
    }

    // Always discover servers fresh from Plex API
    // (no longer loading from config)
    auto discovery_result = discover_servers(token.get());
    if (!discovery_result) {
        PLEX_LOG_WARNING("PlexService", "Failed to discover servers, continuing anyway");
    }

    // No longer saving server configurations

    // Start all connections
    m_connection_manager->start_all_connections();

    // Wait for connections to establish (up to 15 seconds)
    // This gives SSE clients time to connect, especially for remote servers
    int wait_attempts = 30;  // 30 * 500ms = 15 seconds
    size_t connected_count = 0;

    for (int i = 0; i < wait_attempts; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto connected_servers = m_connection_manager->get_connected_servers();
        connected_count = connected_servers.size();

        // If we have at least one connection, that's good enough to proceed
        if (connected_count > 0) {
            PLEX_LOG_INFO("PlexService", "Successfully connected to " + std::to_string(connected_count) + " server(s)");
            break;
        }

        // Log progress every 2 seconds
        if ((i + 1) % 4 == 0) {
            PLEX_LOG_DEBUG("PlexService", "Waiting for server connections... (" +
                          std::to_string((i + 1) * 500 / 1000) + "s elapsed)");
        }
    }

    // Final check and warning if no connections
    if (connected_count == 0) {
        auto connected_servers = m_connection_manager->get_connected_servers();
        connected_count = connected_servers.size();

        if (connected_count == 0) {
            PLEX_LOG_WARNING("PlexService", "No servers successfully connected after waiting 15 seconds");
        } else {
            PLEX_LOG_INFO("PlexService", "Successfully connected to " + std::to_string(connected_count) + " server(s)");
        }
    }

    m_running = true;
    return {};
}

void PlexServiceImpl::stop() {
    PLEX_LOG_INFO("PlexService", "Stopping Plex service");

    m_running = false;

    // Shutdown authenticator to abort any ongoing operations
    if (m_authenticator) {
        m_authenticator->shutdown();
    }

    // Stop all connections
    m_connection_manager->stop_all_connections();

    // Clear caches
    m_cache_manager->clear_all();
    m_session_manager->clear_all();
}

bool PlexServiceImpl::is_running() const {
    return m_running;
}

void PlexServiceImpl::set_poll_interval(std::chrono::seconds interval) {
    m_poll_interval = interval;
}

std::chrono::seconds PlexServiceImpl::get_poll_interval() const {
    return m_poll_interval;
}

void PlexServiceImpl::set_media_state_callback(MediaStateCallback callback) {
    m_state_callback = std::move(callback);
}

void PlexServiceImpl::set_error_callback(MediaErrorCallback callback) {
    m_error_callback = std::move(callback);
}

void PlexServiceImpl::set_connection_callback(MediaConnectionStateCallback callback) {
    m_connection_callback = std::move(callback);
}

std::expected<core::MediaInfo, core::PlexError> PlexServiceImpl::get_current_media() const {
    PLEX_LOG_DEBUG("PlexService", "get_current_media() called");

    if (!m_running) {
        PLEX_LOG_DEBUG("PlexService", "get_current_media() failed - service not running");
        return std::unexpected<core::PlexError>(core::PlexError::NotInitialized);
    }

    auto current = m_session_manager->get_current_playback();
    if (current.has_value()) {
        PLEX_LOG_DEBUG("PlexService", "get_current_media() returning current playback: " + current.value().title);
        return current.value();
    }

    PLEX_LOG_DEBUG("PlexService", "get_current_media() no active playback, returning stopped state");
    core::MediaInfo stopped_info;
    stopped_info.state = core::PlaybackState::Stopped;
    return stopped_info;
}

std::expected<std::vector<core::MediaInfo>, core::PlexError> PlexServiceImpl::get_active_sessions() const {
    PLEX_LOG_DEBUG("PlexService", "get_active_sessions() called");

    if (!m_running) {
        PLEX_LOG_DEBUG("PlexService", "get_active_sessions() failed - service not running");
        return std::unexpected<core::PlexError>(core::PlexError::NotInitialized);
    }

    auto sessions = m_session_manager->get_active_sessions();
    if (sessions.has_value()) {
        PLEX_LOG_DEBUG("PlexService", "get_active_sessions() returning " + std::to_string(sessions.value().size()) + " sessions");
    } else {
        PLEX_LOG_DEBUG("PlexService", "get_active_sessions() failed to get sessions from session manager");
    }
    return sessions;
}

std::expected<void, core::PlexError> PlexServiceImpl::add_server(std::unique_ptr<core::PlexServer> server) {
    if (!server) {
        PLEX_LOG_ERROR("PlexService", "add_server() called with null server");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    PLEX_LOG_DEBUG("PlexService", "add_server() called for server: " + server->name + " (" + server->client_identifier.get() + ")");

    // Store server details before moving ownership
    core::ServerId server_id(server->client_identifier.get());
    core::PlexToken server_token = server->access_token;
    bool is_owned = server->owned;  // Store owned flag before moving

    auto result = m_connection_manager->add_server(std::move(server));

    if (result) {
        PLEX_LOG_DEBUG("PlexService", "add_server() succeeded");

        // Update session manager with server connection info
        std::string preferred_uri = m_connection_manager->get_preferred_server_uri(server_id);
        if (!preferred_uri.empty()) {
            ServerConnectionInfo conn_info;
            conn_info.preferred_uri = preferred_uri;
            conn_info.access_token = server_token;
            conn_info.owned = is_owned;  // Set the owned flag

            m_session_manager->update_server_connection(server_id, conn_info);
            PLEX_LOG_DEBUG("PlexService", "Updated session manager with connection info for server: " + server_id.get() + " (owned: " + std::to_string(is_owned) + ")");
        }
    } else {
        PLEX_LOG_DEBUG("PlexService", "add_server() failed");
    }

    return result;
}

void PlexServiceImpl::remove_server(const core::ServerId& server_id) {
    PLEX_LOG_DEBUG("PlexService", "remove_server() called for server: " + server_id.get());
    m_connection_manager->remove_server(server_id);
    PLEX_LOG_DEBUG("PlexService", "remove_server() completed for server: " + server_id.get());
}

std::vector<core::ServerId> PlexServiceImpl::get_connected_servers() const {
    return m_connection_manager->get_connected_servers();
}

bool PlexServiceImpl::is_server_connected(const core::ServerId& server_id) const {
    return m_connection_manager->is_server_connected(server_id);
}

std::expected<void, core::PlexError> PlexServiceImpl::test_connection(const core::ServerId& server_id) {
    return m_connection_manager->test_connection(server_id);
}

void PlexServiceImpl::on_media_state_changed(const core::MediaInfo& old_state, const core::MediaInfo& new_state) {
	(void)old_state;
    PLEX_LOG_INFO("PlexService", "Media state changed: " + new_state.title);
}

void PlexServiceImpl::on_connection_state_changed(const core::ServerId& server_id, bool connected) {
    PLEX_LOG_INFO("PlexService", "Server " + server_id.get() + " connection state: " +
                  (connected ? "connected" : "disconnected"));

    if (m_connection_callback) {
        m_connection_callback(server_id, connected);
    }
}

void PlexServiceImpl::on_error_occurred(core::PlexError error, const std::string& message) {
    PLEX_LOG_ERROR("PlexService", "Error occurred: " + message);

    if (m_error_callback) {
        m_error_callback(error, message);
    }
}

void PlexServiceImpl::handle_sse_event(const core::ServerId& server_id, const std::string& event) {
    try {
        auto json_event = json::parse(event);

        PLEX_LOG_DEBUG("PlexService", "Received event from server " + server_id.get());

        if (json_event.contains("PlaySessionStateNotification")) {
            m_session_manager->process_play_session_notification(server_id, json_event["PlaySessionStateNotification"]);
        }
    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("PlexService", "Error parsing SSE event: " + std::string(e.what()));
        on_error_occurred(core::PlexError::ParseError, "Failed to parse SSE event");
    }
}

std::expected<void, core::PlexError> PlexServiceImpl::discover_servers(const std::string& auth_token) {
    PLEX_LOG_INFO("PlexService", "Discovering Plex servers");

    if (!m_http_client) {
        PLEX_LOG_ERROR("PlexService", "HTTP client not available for server discovery");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    // Use authenticator's standard headers which include the correct client identifier
    auto std_headers = m_authenticator->get_standard_headers(core::PlexToken(auth_token));
    HttpHeaders headers(std_headers.begin(), std_headers.end());

    // Make request to Plex resources endpoint
    const std::string resources_url = "https://plex.tv/api/v2/resources?includeHttps=1";
    auto response = m_http_client->get(resources_url, headers);

    if (!response || !response->is_success()) {
        PLEX_LOG_ERROR("PlexService", "Failed to fetch servers from Plex.tv");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    PLEX_LOG_DEBUG("PlexService", "Received server response from Plex.tv");

    // Parse the JSON response and add servers
    if (!parse_server_json(response->body, auth_token)) {
        PLEX_LOG_ERROR("PlexService", "Failed to parse server response");
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    return {};
}

bool PlexServiceImpl::parse_server_json(const std::string& json_response, const std::string& auth_token) {
    (void)auth_token;
    PLEX_LOG_INFO("PlexService", "Parsing server JSON response");

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
            server->client_identifier = core::ClientId(resource.value("clientIdentifier", ""));
            server->access_token = core::PlexToken(resource.value("accessToken", ""));
            server->owned = resource.value("owned", false);

            PLEX_LOG_INFO("PlexService", "Found server: " + server->name +
                         " (" + server->client_identifier.get() + ")" +
                         (server->owned ? " [owned]" : " [shared]"));

            // Process connections (we want both local and remote)
            if (resource.contains("connections") && resource["connections"].is_array()) {
                for (const auto& connection : resource["connections"]) {
                    std::string uri = connection.value("uri", "");
                    bool is_local = connection.value("local", false);

                    if (is_local && !uri.empty()) {
                        server->local_uri = uri;
                        PLEX_LOG_INFO("PlexService", "  Local URI: " + uri);
                    } else if (!is_local && !uri.empty()) {
                        server->public_uri = uri;
                        PLEX_LOG_INFO("PlexService", "  Public URI: " + uri);
                    }
                }
            }

            // Add server if it has at least one valid URI
            if (!server->local_uri.empty() || !server->public_uri.empty()) {
                auto add_result = add_server(std::move(server));
                if (add_result) {
                    server_count++;
                } else {
                    PLEX_LOG_WARNING("PlexService", "Failed to add server: " + server->name);
                }
            }
        }

        PLEX_LOG_INFO("PlexService", "Successfully discovered and added " + std::to_string(server_count) + " Plex servers");
        return server_count > 0;

    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("PlexService", "Failed to parse server JSON: " + std::string(e.what()));
        return false;
    }
}

// PlexServiceBuilder implementation
PlexServiceBuilder::PlexServiceBuilder() {
    // Initialize with null pointers
}

PlexServiceBuilder& PlexServiceBuilder::with_authenticator(std::shared_ptr<IPlexAuthenticator> auth) {
    m_authenticator = std::move(auth);
    return *this;
}

PlexServiceBuilder& PlexServiceBuilder::with_cache_manager(std::shared_ptr<IPlexCacheManager> cache) {
    m_cache_manager = std::move(cache);
    return *this;
}

PlexServiceBuilder& PlexServiceBuilder::with_connection_manager(std::shared_ptr<IPlexConnectionManager> conn) {
    m_connection_manager = std::move(conn);
    return *this;
}

PlexServiceBuilder& PlexServiceBuilder::with_media_fetcher(std::shared_ptr<IPlexMediaFetcher> fetcher) {
    m_media_fetcher = std::move(fetcher);
    return *this;
}

PlexServiceBuilder& PlexServiceBuilder::with_session_manager(std::shared_ptr<IPlexSessionManager> session) {
    m_session_manager = std::move(session);
    return *this;
}

PlexServiceBuilder& PlexServiceBuilder::with_http_client(std::shared_ptr<HttpClient> client) {
    m_http_client = std::move(client);
    return *this;
}

PlexServiceBuilder& PlexServiceBuilder::with_configuration_service(std::shared_ptr<core::ConfigurationService> config) {
    m_config_service = std::move(config);
    return *this;
}

std::unique_ptr<PlexServiceImpl> PlexServiceBuilder::build() {
    // Create default implementations if not provided
    if (!m_http_client) {
        auto factory = HttpClientFactory::create_default_factory();
        m_http_client = factory->create_client(HttpClientFactory::ClientType::Curl);
    }

    if (!m_authenticator) {
        if (!m_config_service) {
            throw std::runtime_error("Configuration service is required to create PlexAuthenticator");
        }
        // Browser launcher will be created inside PlexAuthenticator if not provided
        m_authenticator = std::make_shared<PlexAuthenticator>(m_http_client, m_config_service, nullptr);
    }

    if (!m_cache_manager) {
        m_cache_manager = std::make_shared<PlexCacheManager>();
    }

    if (!m_connection_manager) {
        if (!m_config_service) {
            throw std::runtime_error("Configuration service is required to create PlexConnectionManager");
        }
        m_connection_manager = std::make_shared<PlexConnectionManager>(m_http_client, m_config_service);
    }

    if (!m_media_fetcher) {
        m_media_fetcher = std::make_shared<PlexMediaFetcher>(m_http_client, m_cache_manager);

        // Add media extractors (Strategy pattern)
        m_media_fetcher->add_media_extractor(std::make_unique<MovieExtractor>());
        m_media_fetcher->add_media_extractor(std::make_unique<TVShowExtractor>());
        m_media_fetcher->add_media_extractor(std::make_unique<MusicExtractor>());
    }

    if (!m_session_manager) {
        m_session_manager = std::make_shared<PlexSessionManager>();
        // Inject dependencies into session manager
        std::static_pointer_cast<PlexSessionManager>(m_session_manager)->set_dependencies(
            m_http_client, m_cache_manager, m_media_fetcher);
    }

    return std::make_unique<PlexServiceImpl>(
        m_authenticator,
        m_cache_manager,
        m_connection_manager,
        m_media_fetcher,
        m_session_manager,
        m_http_client,
        m_config_service
    );
}

// Server loading from config has been removed - servers are always discovered fresh

// Server validation and saving to config has been removed

} // namespace services
} // namespace presence_for_plex
