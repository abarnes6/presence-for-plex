#pragma once

#include "presence_for_plex/core/models.hpp"
#include <atomic>
#include <memory>
#include <functional>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <expected>

namespace presence_for_plex {

namespace core {
class AuthenticationService;
}

namespace services {

// Forward declarations
class HttpClient;
class SSEClient;

// SSE event callback
using SSEEventCallback = std::function<void(const core::ServerId&, const std::string&)>;

// Connection state callback
using ConnectionStateCallback = std::function<void(const core::ServerId&, bool connected, const std::string& uri)>;

// Server runtime info - each server has its own SSE event thread
struct PlexServerRuntime {
    std::unique_ptr<core::PlexServer> server;
    std::unique_ptr<SSEClient> sse_client;  // Manages dedicated thread internally
    std::atomic<bool> sse_running{false};
    std::atomic<bool> initial_connection_succeeded{false};
};

class PlexConnectionManager {
public:
    explicit PlexConnectionManager(std::shared_ptr<HttpClient> http_client, std::shared_ptr<core::AuthenticationService> auth_service);
    ~PlexConnectionManager();

    std::expected<void, core::PlexError> add_server(std::unique_ptr<core::PlexServer> server);
    void remove_server(const core::ServerId& server_id);
    std::vector<core::ServerId> get_connected_servers() const;

    std::expected<void, core::PlexError> connect_to_server(const core::ServerId& server_id);
    void disconnect_from_server(const core::ServerId& server_id);
    bool is_server_connected(const core::ServerId& server_id) const;

    std::string get_preferred_server_uri(const core::ServerId& server_id);

    void set_sse_event_callback(SSEEventCallback callback);
    void set_connection_state_callback(ConnectionStateCallback callback);
    void start_all_connections();
    void stop_all_connections();

private:
    void setup_server_sse_connection(std::shared_ptr<PlexServerRuntime> runtime_ptr);
    std::expected<bool, core::PlexError> test_uri_accessibility(const std::string& uri, const core::PlexToken& token);

    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::AuthenticationService> m_auth_service;
    mutable std::mutex m_servers_mutex;
    std::map<core::ServerId, std::shared_ptr<PlexServerRuntime>> m_servers;
    SSEEventCallback m_sse_callback;
    ConnectionStateCallback m_connection_state_callback;
    std::atomic<bool> m_shutting_down{false};
};

} // namespace services
} // namespace presence_for_plex
