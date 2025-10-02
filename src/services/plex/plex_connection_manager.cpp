#include "presence_for_plex/services/plex/plex_connection_manager.hpp"
#include "presence_for_plex/services/network/sse_client.hpp"
#include "presence_for_plex/services/network_service.hpp"
#include "presence_for_plex/core/authentication_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <thread>
#include <chrono>

using namespace presence_for_plex::utils; // For expected/unexpected
using presence_for_plex::services::SSEBasicEventCallback;

namespace presence_for_plex {
namespace services {

// PlexConnectionManager implementation
PlexConnectionManager::PlexConnectionManager(std::shared_ptr<HttpClient> http_client, std::shared_ptr<core::AuthenticationService> auth_service)
    : m_http_client(std::move(http_client))
    , m_auth_service(std::move(auth_service)) {

    PLEX_LOG_INFO("PlexConnectionManager", "Creating connection manager");
}

PlexConnectionManager::~PlexConnectionManager() {
    stop_all_connections();
    PLEX_LOG_INFO("PlexConnectionManager", "Connection manager destroyed");
}

std::expected<void, core::PlexError> PlexConnectionManager::add_server(std::unique_ptr<core::PlexServer> server) {
    PLEX_LOG_DEBUG("PlexConnectionManager", "add_server() called");

    if (!server) {
        PLEX_LOG_ERROR("PlexConnectionManager", "Cannot add null server");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    core::ServerId server_id(server->client_identifier.get());

    PLEX_LOG_INFO("PlexConnectionManager", "Adding server: " + server->name + " (" + server_id.get() + ")");
    PLEX_LOG_DEBUG("PlexConnectionManager", "Server details - Local URI: " + server->local_uri + ", Public URI: " + server->public_uri + ", Owned: " + (server->owned ? "true" : "false"));

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    // Create runtime info
    auto runtime = std::make_shared<PlexServerRuntime>();
    runtime->server = std::move(server);
    runtime->sse_client = std::make_unique<SSEClient>(m_http_client);

    m_servers[server_id] = runtime;

    PLEX_LOG_DEBUG("PlexConnectionManager", "Server added successfully");
    return {};
}

void PlexConnectionManager::remove_server(const core::ServerId& server_id) {
    PLEX_LOG_INFO("PlexConnectionManager", "Removing server: " + server_id.get());

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it != m_servers.end()) {
        // Disconnect if connected
        disconnect_from_server(server_id);
        m_servers.erase(it);
        PLEX_LOG_DEBUG("PlexConnectionManager", "Server removed successfully");
    } else {
        PLEX_LOG_WARNING("PlexConnectionManager", "Server not found: " + server_id.get());
    }
}

std::vector<core::ServerId> PlexConnectionManager::get_connected_servers() const {
    PLEX_LOG_DEBUG("PlexConnectionManager", "get_connected_servers() called");
    std::lock_guard<std::mutex> lock(m_servers_mutex);

    std::vector<core::ServerId> connected_servers;
    for (const auto& [server_id, runtime] : m_servers) {
        // Only consider servers that have successfully connected at least once
        if (runtime->initial_connection_succeeded && runtime->sse_client && runtime->sse_client->is_connected()) {
            connected_servers.push_back(server_id);
        }
    }

    PLEX_LOG_DEBUG("PlexConnectionManager", "Returning " + std::to_string(connected_servers.size()) + " connected servers out of " + std::to_string(m_servers.size()) + " total");
    return connected_servers;
}

std::expected<void, core::PlexError> PlexConnectionManager::connect_to_server(const core::ServerId& server_id) {
    PLEX_LOG_INFO("PlexConnectionManager", "Connecting to server: " + server_id.get());

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it == m_servers.end()) {
        PLEX_LOG_ERROR("PlexConnectionManager", "Server not found: " + server_id.get());
        return std::unexpected<core::PlexError>(core::PlexError::ServerNotFound);
    }

    auto& runtime = *it->second;

    if (runtime.sse_client->is_connected()) {
        PLEX_LOG_DEBUG("PlexConnectionManager", "Server already connected: " + server_id.get());
        return {};
    }

    setup_server_sse_connection(runtime);
    return {};
}

void PlexConnectionManager::disconnect_from_server(const core::ServerId& server_id) {
    PLEX_LOG_INFO("PlexConnectionManager", "Disconnecting from server: " + server_id.get());

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it != m_servers.end()) {
        auto& runtime = *it->second;

        runtime.sse_running = false;
        if (runtime.sse_client) {
            runtime.sse_client->disconnect();
        }

        PLEX_LOG_DEBUG("PlexConnectionManager", "Server disconnected: " + server_id.get());
    }
}

bool PlexConnectionManager::is_server_connected(const core::ServerId& server_id) const {
    PLEX_LOG_DEBUG("PlexConnectionManager", "is_server_connected() called for server: " + server_id.get());
    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it != m_servers.end()) {
        // Only report as connected if initial connection succeeded and currently connected
        bool connected = it->second->initial_connection_succeeded &&
                        it->second->sse_client &&
                        it->second->sse_client->is_connected();
        PLEX_LOG_DEBUG("PlexConnectionManager", "Server " + server_id.get() + " connection status: " + (connected ? "connected" : "disconnected"));
        return connected;
    }

    PLEX_LOG_DEBUG("PlexConnectionManager", "Server " + server_id.get() + " not found in server map");
    return false;
}

std::string PlexConnectionManager::get_preferred_server_uri(const core::ServerId& server_id) {
    PLEX_LOG_DEBUG("PlexConnectionManager", "get_preferred_server_uri() called for server: " + server_id.get());
    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it == m_servers.end()) {
        PLEX_LOG_ERROR("PlexConnectionManager", "Server not found: " + server_id.get());
        return "";
    }

    const auto& server = it->second->server;

    // Test local URI first if available
    if (!server->local_uri.empty()) {
        PLEX_LOG_DEBUG("PlexConnectionManager", "Testing local URI: " + server->local_uri);
        auto test_result = test_uri_accessibility(server->local_uri, server->access_token);
        if (test_result && test_result.value()) {
            PLEX_LOG_INFO("PlexConnectionManager", "Using local URI for " + server->name + ": " + server->local_uri);
            return server->local_uri;
        }
    }

    // Test public URI as fallback
    if (!server->public_uri.empty()) {
        PLEX_LOG_DEBUG("PlexConnectionManager", "Testing public URI: " + server->public_uri);
        auto test_result = test_uri_accessibility(server->public_uri, server->access_token);
        if (test_result && test_result.value()) {
            PLEX_LOG_INFO("PlexConnectionManager", "Using public URI for " + server->name + ": " + server->public_uri);
            return server->public_uri;
        }
    }

    PLEX_LOG_WARNING("PlexConnectionManager", "No accessible URI found for server: " + server->name);
    return "";
}

void PlexConnectionManager::set_sse_event_callback(SSEEventCallback callback) {
    m_sse_callback = std::move(callback);
}

void PlexConnectionManager::set_connection_state_callback(ConnectionStateCallback callback) {
    m_connection_state_callback = std::move(callback);
}

void PlexConnectionManager::start_all_connections() {
    PLEX_LOG_INFO("PlexConnectionManager", "Starting all server connections");

    std::lock_guard<std::mutex> lock(m_servers_mutex);
    PLEX_LOG_DEBUG("PlexConnectionManager", "Total servers to process: " + std::to_string(m_servers.size()));

    size_t started = 0;
    for (const auto& [server_id, runtime] : m_servers) {
        if (!runtime->sse_client->is_connected()) {
            PLEX_LOG_DEBUG("PlexConnectionManager", "Starting connection for server: " + server_id.get());
            setup_server_sse_connection(runtime);
            ++started;
        } else {
            PLEX_LOG_DEBUG("PlexConnectionManager", "Server already connected: " + server_id.get());
        }
    }

    PLEX_LOG_INFO("PlexConnectionManager", "Started " + std::to_string(started) + " server connection(s)");
}

void PlexConnectionManager::stop_all_connections() {
    PLEX_LOG_INFO("PlexConnectionManager", "Stopping all server connections");

    m_shutting_down = true;

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    // Signal all SSE clients to disconnect
    for (const auto& [server_id, runtime] : m_servers) {
        runtime->sse_running = false;
        if (runtime->sse_client) {
            runtime->sse_client->disconnect();
        }
    }

    m_servers.clear();

    PLEX_LOG_INFO("PlexConnectionManager", "All server connections stopped");
}

void PlexConnectionManager::setup_server_sse_connection(std::shared_ptr<PlexServerRuntime> runtime_ptr) {
    const auto& server = runtime_ptr->server;
    core::ServerId server_id(server->client_identifier.get());

    PLEX_LOG_INFO("PlexConnectionManager", "Setting up SSE connection to: " + server->name);

    // Prepare headers using server-specific client identifier
    HttpHeaders headers = {
        {"X-Plex-Product", "Presence For Plex"},
        {"X-Plex-Version", "1.0.0"},
        {"X-Plex-Client-Identifier", server->client_identifier.get()},
        {"X-Plex-Platform", "Linux"},
        {"X-Plex-Device", "PC"},
        {"X-Plex-Token", server->access_token.get()}
    };

    // Set up SSE callback
    SSEBasicEventCallback sse_callback = [this, server_id](const std::string& event) {
        if (m_sse_callback && !m_shutting_down) {
            m_sse_callback(server_id, event);
        }
    };

    // Prefer local URI, fall back to public URI
    std::string uri_to_use = !server->local_uri.empty() ? server->local_uri : server->public_uri;

    if (uri_to_use.empty()) {
        PLEX_LOG_ERROR("PlexConnectionManager", "No URI configured for server: " + server->name);
        runtime_ptr->sse_running = false;
        runtime_ptr->initial_connection_succeeded = false;
        return;
    }

    PLEX_LOG_INFO("PlexConnectionManager", "Attempting SSE connection to " + server->name + ": " + uri_to_use);

    // Construct SSE endpoint URL
    std::string sse_url = uri_to_use + "/:/eventsource/notifications?filters=playing";

    // Start SSE connection (this is async and runs in its own thread)
    auto connect_result = runtime_ptr->sse_client->connect(sse_url, headers, sse_callback);

    if (connect_result.has_value()) {
        PLEX_LOG_INFO("PlexConnectionManager", "SSE connection initiated for: " + server->name + " at: " + sse_url);
        runtime_ptr->sse_running = true;

        auto conn_callback = m_connection_state_callback;
        std::thread([runtime_ptr, server_id, server_name = server->name, sse_url, shutdown_flag = &m_shutting_down, conn_callback = std::move(conn_callback)]() {
            for (int wait = 0; wait < 300 && !(*shutdown_flag); ++wait) {
                if (runtime_ptr->sse_client && runtime_ptr->sse_client->is_connected()) {
                    PLEX_LOG_INFO("PlexConnectionManager", "SSE connection confirmed for: " + server_name);
                    runtime_ptr->initial_connection_succeeded = true;

                    if (conn_callback && !(*shutdown_flag)) {
                        std::string uri = sse_url.substr(0, sse_url.find("/:/eventsource"));
                        conn_callback(server_id, true, uri);
                    }
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (!(*shutdown_flag)) {
                PLEX_LOG_WARNING("PlexConnectionManager", "SSE connection timeout for: " + server_name);
            }
        }).detach();
    } else {
        PLEX_LOG_ERROR("PlexConnectionManager", "Failed to initiate SSE connection for: " + server->name);
        runtime_ptr->sse_running = false;
        runtime_ptr->initial_connection_succeeded = false;
    }
}

std::expected<bool, core::PlexError> PlexConnectionManager::test_uri_accessibility(const std::string& uri, const core::PlexToken& token) {
    HttpHeaders headers = {
        {"X-Plex-Product", "Presence For Plex"},
        {"X-Plex-Version", "1.0.0"},
        {"X-Plex-Client-Identifier", m_auth_service->get_plex_client_identifier()},
        {"X-Plex-Platform", "Linux"},
        {"X-Plex-Device", "PC"},
        {"X-Plex-Token", token.get()}
    };

    auto response = m_http_client->get(uri, headers);
    if (!response) {
        PLEX_LOG_DEBUG("PlexConnectionManager", "URI accessibility test for " + uri + ": FAIL (no response)");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    bool accessible = response->is_success();
    PLEX_LOG_DEBUG("PlexConnectionManager", "URI accessibility test for " + uri + ": " +
                   (accessible ? "PASS" : "FAIL"));

    return accessible;
}

} // namespace services
} // namespace presence_for_plex
