#include "presence_for_plex/services/plex/plex_sse.hpp"
#include "presence_for_plex/services/network/sse_client.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/services/plex/plex_auth.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <thread>
#include <chrono>

using presence_for_plex::services::SSEBasicEventCallback;

namespace presence_for_plex {
namespace services {

PlexSSE::PlexSSE(std::shared_ptr<HttpClient> http_client, std::shared_ptr<PlexAuth> auth)
    : m_http_client(std::move(http_client))
    , m_auth(std::move(auth)) {
    LOG_DEBUG("PlexSSE", "Creating SSE manager");
}

PlexSSE::~PlexSSE() {
    stop_all_connections();
    LOG_INFO("PlexSSE", "SSE manager destroyed");
}

std::expected<void, core::PlexError> PlexSSE::add_server(std::unique_ptr<core::PlexServer> server) {
    if (!server) {
        LOG_ERROR("PlexSSE", "Cannot add null server");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    core::ServerId server_id(server->client_identifier);

    LOG_DEBUG("PlexSSE", "Adding server: " + server->name + " (" + server_id.get() + ")");
    LOG_DEBUG("PlexSSE", "Server details - " +
              std::to_string(server->local_uris.size()) + " local URI(s), " +
              std::to_string(server->public_uris.size()) + " public URI(s), Owned: " +
              (server->owned ? "true" : "false"));

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto runtime = std::make_shared<PlexServerRuntime>();
    runtime->server = std::move(server);
    runtime->active_connection_index = -1;

    m_servers[server_id] = runtime;

    LOG_DEBUG("PlexSSE", "Server added successfully");
    return {};
}

void PlexSSE::remove_server(const core::ServerId& server_id) {
    LOG_INFO("PlexSSE", "Removing server: " + server_id.get());

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it != m_servers.end()) {
        disconnect_from_server(server_id);
        m_servers.erase(it);
        LOG_DEBUG("PlexSSE", "Server removed successfully");
    } else {
        LOG_WARNING("PlexSSE", "Server not found: " + server_id.get());
    }
}

std::vector<core::ServerId> PlexSSE::get_connected_servers() const {
    std::lock_guard<std::mutex> lock(m_servers_mutex);

    std::vector<core::ServerId> connected_servers;
    for (const auto& [server_id, runtime] : m_servers) {
        int active_idx = runtime->active_connection_index.load();
        if (active_idx >= 0 && active_idx < static_cast<int>(runtime->connection_attempts.size())) {
            if (runtime->connection_attempts[active_idx]->sse_client &&
                runtime->connection_attempts[active_idx]->sse_client->is_connected()) {
                connected_servers.push_back(server_id);
            }
        }
    }

    return connected_servers;
}

std::expected<void, core::PlexError> PlexSSE::connect_to_server(const core::ServerId& server_id) {
    LOG_INFO("PlexSSE", "Connecting to server: " + server_id.get());

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it == m_servers.end()) {
        LOG_ERROR("PlexSSE", "Server not found: " + server_id.get());
        return std::unexpected<core::PlexError>(core::PlexError::ServerNotFound);
    }

    auto runtime = it->second;

    int active_idx = runtime->active_connection_index.load();
    if (active_idx >= 0 && active_idx < static_cast<int>(runtime->connection_attempts.size())) {
        if (runtime->connection_attempts[active_idx]->sse_client &&
            runtime->connection_attempts[active_idx]->sse_client->is_connected()) {
            LOG_DEBUG("PlexSSE", "Server already connected: " + server_id.get());
            return {};
        }
    }

    setup_server_sse_connection(runtime);
    return {};
}

void PlexSSE::disconnect_from_server(const core::ServerId& server_id) {
    LOG_INFO("PlexSSE", "Disconnecting from server: " + server_id.get());

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it != m_servers.end()) {
        auto& runtime = *it->second;

        for (auto& attempt : runtime.connection_attempts) {
            if (attempt->sse_client) {
                attempt->sse_client->disconnect();
            }
        }

        runtime.active_connection_index = -1;
        runtime.should_restart_race = true;

        LOG_DEBUG("PlexSSE", "Server disconnected: " + server_id.get());
    }
}

bool PlexSSE::is_server_connected(const core::ServerId& server_id) const {
    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it != m_servers.end()) {
        int active_idx = it->second->active_connection_index.load();
        if (active_idx >= 0 && active_idx < static_cast<int>(it->second->connection_attempts.size())) {
            return it->second->connection_attempts[active_idx]->sse_client &&
                   it->second->connection_attempts[active_idx]->sse_client->is_connected();
        }
    }

    return false;
}

std::string PlexSSE::get_preferred_server_uri(const core::ServerId& server_id) {
    std::lock_guard<std::mutex> lock(m_servers_mutex);

    auto it = m_servers.find(server_id);
    if (it == m_servers.end()) {
        LOG_ERROR("PlexSSE", "Server not found: " + server_id.get());
        return "";
    }

    const auto& server = it->second->server;

    for (const auto& uri : server->local_uris) {
        auto test_result = test_uri_accessibility(uri, server->access_token);
        if (test_result && test_result.value()) {
            LOG_INFO("PlexSSE", "Using local URI for " + server->name + ": " + uri);
            return uri;
        }
    }

    for (const auto& uri : server->public_uris) {
        auto test_result = test_uri_accessibility(uri, server->access_token);
        if (test_result && test_result.value()) {
            LOG_INFO("PlexSSE", "Using public URI for " + server->name + ": " + uri);
            return uri;
        }
    }

    LOG_WARNING("PlexSSE", "No accessible URI found for server: " + server->name);
    return "";
}

void PlexSSE::set_sse_event_callback(SSEEventCallback callback) {
    m_sse_callback = std::move(callback);
}

void PlexSSE::set_connection_state_callback(ConnectionStateCallback callback) {
    m_connection_state_callback = std::move(callback);
}

void PlexSSE::start_all_connections() {
    LOG_DEBUG("PlexSSE", "Starting all server connections");

    std::lock_guard<std::mutex> lock(m_servers_mutex);

    for (const auto& [server_id, runtime] : m_servers) {
        int active_idx = runtime->active_connection_index.load();
        bool is_connected = false;

        if (active_idx >= 0 && active_idx < static_cast<int>(runtime->connection_attempts.size())) {
            if (runtime->connection_attempts[active_idx]->sse_client &&
                runtime->connection_attempts[active_idx]->sse_client->is_connected()) {
                is_connected = true;
            }
        }

        if (!is_connected) {
            LOG_DEBUG("PlexSSE", "Starting connection for server: " + server_id.get());
            setup_server_sse_connection(runtime);
        }
    }
}

void PlexSSE::stop_all_connections() {
    LOG_INFO("PlexSSE", "Stopping all server connections");

    m_shutting_down = true;

    std::lock_guard<std::mutex> lock(m_servers_mutex);

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

    LOG_INFO("PlexSSE", "All server connections stopped");
}

void PlexSSE::setup_server_sse_connection(std::shared_ptr<PlexServerRuntime> runtime_ptr) {
    const auto& server = runtime_ptr->server;
    core::ServerId server_id(server->client_identifier);

    LOG_DEBUG("PlexSSE", "Setting up parallel SSE connections for: " + server->name);

    std::vector<std::string> uris_to_try;
    uris_to_try.insert(uris_to_try.end(), server->local_uris.begin(), server->local_uris.end());
    uris_to_try.insert(uris_to_try.end(), server->public_uris.begin(), server->public_uris.end());

    if (uris_to_try.empty()) {
        LOG_ERROR("PlexSSE", "No URIs configured for server: " + server->name);
        runtime_ptr->active_connection_index = -1;
        return;
    }

    LOG_INFO("PlexSSE", "Starting connection race for " + std::to_string(uris_to_try.size()) +
             " URIs for server: " + server->name);

    HttpHeaders headers = {
        {"X-Plex-Product", "Presence For Plex"},
        {"X-Plex-Version", "1.0.0"},
        {"X-Plex-Client-Identifier", server->client_identifier},
        {"X-Plex-Platform", "Linux"},
        {"X-Plex-Device", "PC"},
        {"X-Plex-Token", server->access_token}
    };

    runtime_ptr->connection_attempts.clear();
    runtime_ptr->active_connection_index = -1;

    for (size_t i = 0; i < uris_to_try.size(); ++i) {
        const auto& uri = uris_to_try[i];

        auto attempt = std::make_unique<SSEConnectionAttempt>();
        attempt->uri = uri;
        attempt->sse_client = std::make_unique<SSEClient>(m_http_client);
        attempt->connected = false;

        LOG_INFO("PlexSSE", "Starting connection attempt " + std::to_string(i) +
                 " to " + server->name + ": " + uri);

        std::string sse_url = uri + "/:/eventsource/notifications?filters=playing";

        SSEBasicEventCallback sse_callback = [this, server_id, runtime_ptr, index = i](const std::string& event) {
            if (runtime_ptr->active_connection_index == static_cast<int>(index)) {
                if (m_sse_callback && !m_shutting_down) {
                    m_sse_callback(server_id, event);
                }
            }
        };

        auto connect_result = attempt->sse_client->connect(sse_url, headers, sse_callback);

        if (connect_result.has_value()) {
            runtime_ptr->connection_attempts.push_back(std::move(attempt));
        } else {
            LOG_WARNING("PlexSSE", "Failed to initiate connection attempt for: " + uri);
        }
    }

    auto conn_callback = m_connection_state_callback;
    runtime_ptr->monitor_thread = std::make_unique<std::jthread>([this, runtime_ptr, server_id,
                                                                    server_name = server->name,
                                                                    conn_callback]() {
        while (!m_shutting_down && !runtime_ptr->should_restart_race) {
            if (runtime_ptr->active_connection_index == -1) {
                for (size_t i = 0; i < runtime_ptr->connection_attempts.size(); ++i) {
                    auto& attempt = runtime_ptr->connection_attempts[i];

                    if (attempt->sse_client && attempt->sse_client->is_connected()) {
                        LOG_INFO("PlexSSE", "Connection established to " + server_name +
                                " via: " + attempt->uri);

                        runtime_ptr->active_connection_index = static_cast<int>(i);
                        attempt->connected = true;

                        for (size_t j = 0; j < runtime_ptr->connection_attempts.size(); ++j) {
                            if (j != i && runtime_ptr->connection_attempts[j]->sse_client) {
                                runtime_ptr->connection_attempts[j]->sse_client->disconnect();
                            }
                        }

                        if (conn_callback && !m_shutting_down) {
                            conn_callback(server_id, true, attempt->uri);
                        }

                        break;
                    }
                }
            } else {
                auto& active_attempt = runtime_ptr->connection_attempts[runtime_ptr->active_connection_index];

                if (!active_attempt->sse_client->is_connected()) {
                    LOG_WARNING("PlexSSE", "Active connection dropped for " + server_name +
                               ", restarting URI race");

                    if (conn_callback && !m_shutting_down) {
                        conn_callback(server_id, false, active_attempt->uri);
                    }

                    setup_server_sse_connection(runtime_ptr);
                    return;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    LOG_DEBUG("PlexSSE", "Connection race started for: " + server->name);
}

std::expected<bool, core::PlexError> PlexSSE::test_uri_accessibility(const std::string& uri, const core::PlexToken& token) {
    HttpHeaders headers = {
        {"X-Plex-Product", "Presence For Plex"},
        {"X-Plex-Version", "1.0.0"},
        {"X-Plex-Client-Identifier", m_auth->get_client_identifier()},
        {"X-Plex-Platform", "Linux"},
        {"X-Plex-Device", "PC"},
        {"X-Plex-Token", token}
    };

    auto response = m_http_client->get(uri, headers);
    if (!response) {
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    return response->is_success();
}

} // namespace services
} // namespace presence_for_plex
