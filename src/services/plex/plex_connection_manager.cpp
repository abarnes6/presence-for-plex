#include "presence_for_plex/services/plex/plex_connection_manager.hpp"
#include "presence_for_plex/services/network/sse_client.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/services/plex/plex_auth_storage.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <thread>
#include <chrono>

using namespace presence_for_plex::utils; // For expected/unexpected
using presence_for_plex::services::SSEBasicEventCallback;

namespace presence_for_plex {
namespace services {

// PlexConnectionManager implementation
PlexConnectionManager::PlexConnectionManager(std::shared_ptr<HttpClient> http_client, std::shared_ptr<PlexAuthStorage> auth_service)
    : m_http_client(std::move(http_client))
    , m_auth_service(std::move(auth_service)) {

    LOG_DEBUG("PlexConnectionManager", "Creating connection manager");
}

PlexConnectionManager::~PlexConnectionManager() {
    stop_all_connections();
    LOG_INFO("PlexConnectionManager", "Connection manager destroyed");
}

std::expected<void, core::PlexError> PlexConnectionManager::add_server(std::unique_ptr<core::PlexServer> server) {
    LOG_DEBUG("PlexConnectionManager", "add_server() called");

    if (!server) {
        LOG_ERROR("PlexConnectionManager", "Cannot add null server");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    core::ServerId server_id(server->client_identifier);

    LOG_DEBUG("PlexConnectionManager", "Adding server: " + server->name + " (" + server_id.get() + ")");
    LOG_DEBUG("PlexConnectionManager", "Server details - " +
              std::to_string(server->local_uris.size()) + " local URI(s), " +
              std::to_string(server->public_uris.size()) + " public URI(s), Owned: " +
              (server->owned ? "true" : "false"));

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    // Create runtime info
    auto runtime = std::make_shared<PlexServerRuntime>();
    runtime->server = std::move(server);
    runtime->active_connection_index = -1;

    m_servers[server_id] = runtime;

    LOG_DEBUG("PlexConnectionManager", "Server added successfully");
    return {};
}

void PlexConnectionManager::remove_server(const core::ServerId& server_id) {
    LOG_INFO("PlexConnectionManager", "Removing server: " + server_id.get());

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it != m_servers.end()) {
        // Disconnect if connected
        disconnect_from_server(server_id);
        m_servers.erase(it);
        LOG_DEBUG("PlexConnectionManager", "Server removed successfully");
    } else {
        LOG_WARNING("PlexConnectionManager", "Server not found: " + server_id.get());
    }
}

std::vector<core::ServerId> PlexConnectionManager::get_connected_servers() const {
    LOG_DEBUG("PlexConnectionManager", "get_connected_servers() called");
    std::lock_guard<std::mutex> lock(m_servers_mutex);

    std::vector<core::ServerId> connected_servers;
    for (const auto& [server_id, runtime] : m_servers) {
        // Check if there's an active connection
        int active_idx = runtime->active_connection_index.load();
        if (active_idx >= 0 && active_idx < static_cast<int>(runtime->connection_attempts.size())) {
            if (runtime->connection_attempts[active_idx]->sse_client &&
                runtime->connection_attempts[active_idx]->sse_client->is_connected()) {
                connected_servers.push_back(server_id);
            }
        }
    }

    LOG_DEBUG("PlexConnectionManager", "Returning " + std::to_string(connected_servers.size()) + " connected servers out of " + std::to_string(m_servers.size()) + " total");
    return connected_servers;
}

std::expected<void, core::PlexError> PlexConnectionManager::connect_to_server(const core::ServerId& server_id) {
    LOG_INFO("PlexConnectionManager", "Connecting to server: " + server_id.get());

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it == m_servers.end()) {
        LOG_ERROR("PlexConnectionManager", "Server not found: " + server_id.get());
        return std::unexpected<core::PlexError>(core::PlexError::ServerNotFound);
    }

    auto runtime = it->second;

    // Check if already connected
    int active_idx = runtime->active_connection_index.load();
    if (active_idx >= 0 && active_idx < static_cast<int>(runtime->connection_attempts.size())) {
        if (runtime->connection_attempts[active_idx]->sse_client &&
            runtime->connection_attempts[active_idx]->sse_client->is_connected()) {
            LOG_DEBUG("PlexConnectionManager", "Server already connected: " + server_id.get());
            return {};
        }
    }

    setup_server_sse_connection(runtime);
    return {};
}

void PlexConnectionManager::disconnect_from_server(const core::ServerId& server_id) {
    LOG_INFO("PlexConnectionManager", "Disconnecting from server: " + server_id.get());

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it != m_servers.end()) {
        auto& runtime = *it->second;

        // Disconnect all connection attempts
        for (auto& attempt : runtime.connection_attempts) {
            if (attempt->sse_client) {
                attempt->sse_client->disconnect();
            }
        }

        runtime.active_connection_index = -1;
        runtime.should_restart_race = true;

        LOG_DEBUG("PlexConnectionManager", "Server disconnected: " + server_id.get());
    }
}

bool PlexConnectionManager::is_server_connected(const core::ServerId& server_id) const {
    LOG_DEBUG("PlexConnectionManager", "is_server_connected() called for server: " + server_id.get());
    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it != m_servers.end()) {
        // Check if there's an active connection
        int active_idx = it->second->active_connection_index.load();
        if (active_idx >= 0 && active_idx < static_cast<int>(it->second->connection_attempts.size())) {
            bool connected = it->second->connection_attempts[active_idx]->sse_client &&
                           it->second->connection_attempts[active_idx]->sse_client->is_connected();
            LOG_DEBUG("PlexConnectionManager", "Server " + server_id.get() + " connection status: " +
                     (connected ? "connected" : "disconnected"));
            return connected;
        }
        LOG_DEBUG("PlexConnectionManager", "Server " + server_id.get() + " has no active connection");
        return false;
    }

    LOG_DEBUG("PlexConnectionManager", "Server " + server_id.get() + " not found in server map");
    return false;
}

std::string PlexConnectionManager::get_preferred_server_uri(const core::ServerId& server_id) {
    LOG_DEBUG("PlexConnectionManager", "get_preferred_server_uri() called for server: " + server_id.get());
    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it == m_servers.end()) {
        LOG_ERROR("PlexConnectionManager", "Server not found: " + server_id.get());
        return "";
    }

    const auto& server = it->second->server;

    // Test all local URIs first
    for (const auto& uri : server->local_uris) {
        LOG_DEBUG("PlexConnectionManager", "Testing local URI: " + uri);
        auto test_result = test_uri_accessibility(uri, server->access_token);
        if (test_result && test_result.value()) {
            LOG_INFO("PlexConnectionManager", "Using local URI for " + server->name + ": " + uri);
            return uri;
        }
    }

    // Test all public URIs as fallback
    for (const auto& uri : server->public_uris) {
        LOG_DEBUG("PlexConnectionManager", "Testing public URI: " + uri);
        auto test_result = test_uri_accessibility(uri, server->access_token);
        if (test_result && test_result.value()) {
            LOG_INFO("PlexConnectionManager", "Using public URI for " + server->name + ": " + uri);
            return uri;
        }
    }

    LOG_WARNING("PlexConnectionManager", "No accessible URI found for server: " + server->name);
    return "";
}

void PlexConnectionManager::set_sse_event_callback(SSEEventCallback callback) {
    m_sse_callback = std::move(callback);
}

void PlexConnectionManager::set_connection_state_callback(ConnectionStateCallback callback) {
    m_connection_state_callback = std::move(callback);
}

void PlexConnectionManager::start_all_connections() {
    LOG_DEBUG("PlexConnectionManager", "Starting all server connections");

    std::lock_guard<std::mutex> lock(m_servers_mutex);
    LOG_DEBUG("PlexConnectionManager", "Total servers to process: " + std::to_string(m_servers.size()));

    size_t started = 0;
    for (const auto& [server_id, runtime] : m_servers) {
        // Check if already connected
        int active_idx = runtime->active_connection_index.load();
        bool is_connected = false;

        if (active_idx >= 0 && active_idx < static_cast<int>(runtime->connection_attempts.size())) {
            if (runtime->connection_attempts[active_idx]->sse_client &&
                runtime->connection_attempts[active_idx]->sse_client->is_connected()) {
                is_connected = true;
            }
        }

        if (!is_connected) {
            LOG_DEBUG("PlexConnectionManager", "Starting connection for server: " + server_id.get());
            setup_server_sse_connection(runtime);
            ++started;
        } else {
            LOG_DEBUG("PlexConnectionManager", "Server already connected: " + server_id.get());
        }
    }

    LOG_DEBUG("PlexConnectionManager", "Started " + std::to_string(started) + " server connection(s)");
}

void PlexConnectionManager::stop_all_connections() {
    LOG_INFO("PlexConnectionManager", "Stopping all server connections");

    m_shutting_down = true;

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    // Signal all SSE clients to disconnect
    for (const auto& [server_id, runtime] : m_servers) {
        runtime->should_restart_race = true;
        for (auto& attempt : runtime->connection_attempts) {
            if (attempt->sse_client) {
                attempt->sse_client->disconnect();
            }
        }
        runtime->active_connection_index = -1;
    }

    m_servers.clear();

    LOG_INFO("PlexConnectionManager", "All server connections stopped");
}

void PlexConnectionManager::setup_server_sse_connection(std::shared_ptr<PlexServerRuntime> runtime_ptr) {
    const auto& server = runtime_ptr->server;
    core::ServerId server_id(server->client_identifier);

    LOG_DEBUG("PlexConnectionManager", "Setting up parallel SSE connections for: " + server->name);

    // Build list of all URIs to try (local URIs first, then public URIs)
    std::vector<std::string> uris_to_try;
    uris_to_try.insert(uris_to_try.end(), server->local_uris.begin(), server->local_uris.end());
    uris_to_try.insert(uris_to_try.end(), server->public_uris.begin(), server->public_uris.end());

    if (uris_to_try.empty()) {
        LOG_ERROR("PlexConnectionManager", "No URIs configured for server: " + server->name);
        runtime_ptr->active_connection_index = -1;
        return;
    }

    LOG_INFO("PlexConnectionManager", "Starting connection race for " + std::to_string(uris_to_try.size()) +
             " URIs for server: " + server->name);

    // Prepare headers using server-specific client identifier
    HttpHeaders headers = {
        {"X-Plex-Product", "Presence For Plex"},
        {"X-Plex-Version", "1.0.0"},
        {"X-Plex-Client-Identifier", server->client_identifier},
        {"X-Plex-Platform", "Linux"},
        {"X-Plex-Device", "PC"},
        {"X-Plex-Token", server->access_token}
    };

    // Clear any existing connection attempts
    runtime_ptr->connection_attempts.clear();
    runtime_ptr->active_connection_index = -1;

    // Create connection attempt for each URI and start them all in parallel
    for (size_t i = 0; i < uris_to_try.size(); ++i) {
        const auto& uri = uris_to_try[i];

        auto attempt = std::make_unique<SSEConnectionAttempt>();
        attempt->uri = uri;
        attempt->sse_client = std::make_unique<SSEClient>(m_http_client);
        attempt->connected = false;

        LOG_INFO("PlexConnectionManager", "Starting connection attempt " + std::to_string(i) +
                 " to " + server->name + ": " + uri);

        // Construct SSE endpoint URL
        std::string sse_url = uri + "/:/eventsource/notifications?filters=playing";

        // Set up SSE callback that checks if this is the first to connect
        SSEBasicEventCallback sse_callback = [this, server_id, runtime_ptr, index = i](const std::string& event) {
            // Only process events if this is the active connection
            if (runtime_ptr->active_connection_index == static_cast<int>(index)) {
                if (m_sse_callback && !m_shutting_down) {
                    m_sse_callback(server_id, event);
                }
            }
        };

        // Start SSE connection (async)
        auto connect_result = attempt->sse_client->connect(sse_url, headers, sse_callback);

        if (connect_result.has_value()) {
            runtime_ptr->connection_attempts.push_back(std::move(attempt));
        } else {
            LOG_WARNING("PlexConnectionManager", "Failed to initiate connection attempt for: " + uri);
        }
    }

    // Start monitoring thread to detect the first successful connection
    auto conn_callback = m_connection_state_callback;
    runtime_ptr->monitor_thread = std::make_unique<std::jthread>([this, runtime_ptr, server_id,
                                                                    server_name = server->name,
                                                                    conn_callback]() {
        while (!m_shutting_down && !runtime_ptr->should_restart_race) {
            // Check if any connection succeeded (and we haven't picked a winner yet)
            if (runtime_ptr->active_connection_index == -1) {
                for (size_t i = 0; i < runtime_ptr->connection_attempts.size(); ++i) {
                    auto& attempt = runtime_ptr->connection_attempts[i];

                    if (attempt->sse_client && attempt->sse_client->is_connected()) {
                        // This one won the race!
                        LOG_INFO("PlexConnectionManager", "Connection established to " + server_name +
                                " via: " + attempt->uri);

                        runtime_ptr->active_connection_index = static_cast<int>(i);
                        attempt->connected = true;

                        // Stop all other connection attempts
                        for (size_t j = 0; j < runtime_ptr->connection_attempts.size(); ++j) {
                            if (j != i && runtime_ptr->connection_attempts[j]->sse_client) {
                                runtime_ptr->connection_attempts[j]->sse_client->disconnect();
                            }
                        }

                        // Notify via callback
                        if (conn_callback && !m_shutting_down) {
                            conn_callback(server_id, true, attempt->uri);
                        }

                        break;
                    }
                }
            } else {
                // Monitor the active connection
                auto& active_attempt = runtime_ptr->connection_attempts[runtime_ptr->active_connection_index];

                if (!active_attempt->sse_client->is_connected()) {
                    // Active connection dropped!
                    LOG_WARNING("PlexConnectionManager", "Active connection dropped for " + server_name +
                               ", restarting URI race");

                    if (conn_callback && !m_shutting_down) {
                        conn_callback(server_id, false, active_attempt->uri);
                    }

                    // Restart the race
                    setup_server_sse_connection(runtime_ptr);
                    return;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    LOG_DEBUG("PlexConnectionManager", "Connection race started for: " + server->name);
}

std::expected<bool, core::PlexError> PlexConnectionManager::test_uri_accessibility(const std::string& uri, const core::PlexToken& token) {
    HttpHeaders headers = {
        {"X-Plex-Product", "Presence For Plex"},
        {"X-Plex-Version", "1.0.0"},
        {"X-Plex-Client-Identifier", m_auth_service->get_plex_client_identifier()},
        {"X-Plex-Platform", "Linux"},
        {"X-Plex-Device", "PC"},
        {"X-Plex-Token", token}
    };

    auto response = m_http_client->get(uri, headers);
    if (!response) {
        LOG_DEBUG("PlexConnectionManager", "URI accessibility test for " + uri + ": FAIL (no response)");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    bool accessible = response->is_success();
    LOG_DEBUG("PlexConnectionManager", "URI accessibility test for " + uri + ": " +
                   (accessible ? "PASS" : "FAIL"));

    return accessible;
}

} // namespace services
} // namespace presence_for_plex
