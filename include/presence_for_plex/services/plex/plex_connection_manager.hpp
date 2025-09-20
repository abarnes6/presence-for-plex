#pragma once

#include "presence_for_plex/core/models.hpp"
#include <memory>
#include <functional>
#include <map>
#include <thread>
#include <mutex>
#include <expected>

namespace presence_for_plex {

namespace core {
class ConfigurationService;
}

namespace services {

// Forward declarations
class HttpClient;
class ISSEClient;

// SSE event callback
using SSEEventCallback = std::function<void(const core::ServerId&, const std::string&)>;

// Interface for connection management following SRP
class IPlexConnectionManager {
public:
    virtual ~IPlexConnectionManager() = default;

    // Server management
    virtual std::expected<void, core::PlexError> add_server(std::unique_ptr<core::PlexServer> server) = 0;
    virtual void remove_server(const core::ServerId& server_id) = 0;
    virtual std::vector<core::ServerId> get_connected_servers() const = 0;

    // Connection management
    virtual std::expected<void, core::PlexError> connect_to_server(const core::ServerId& server_id) = 0;
    virtual void disconnect_from_server(const core::ServerId& server_id) = 0;
    virtual bool is_server_connected(const core::ServerId& server_id) const = 0;

    // URI management
    virtual std::string get_preferred_server_uri(const core::ServerId& server_id) = 0;
    virtual std::expected<void, core::PlexError> test_connection(const core::ServerId& server_id) = 0;

    // SSE event handling
    virtual void set_sse_event_callback(SSEEventCallback callback) = 0;
    virtual void start_all_connections() = 0;
    virtual void stop_all_connections() = 0;
};

// Server runtime info
struct PlexServerRuntime {
    std::unique_ptr<core::PlexServer> server;
    std::unique_ptr<ISSEClient> sse_client;
    std::thread sse_thread;
    std::atomic<bool> sse_running{false};
    std::atomic<bool> initial_connection_succeeded{false};
};

// Concrete implementation
class PlexConnectionManager : public IPlexConnectionManager {
public:
    explicit PlexConnectionManager(std::shared_ptr<HttpClient> http_client, std::shared_ptr<core::ConfigurationService> config_service);
    ~PlexConnectionManager() override;

    std::expected<void, core::PlexError> add_server(std::unique_ptr<core::PlexServer> server) override;
    void remove_server(const core::ServerId& server_id) override;
    std::vector<core::ServerId> get_connected_servers() const override;

    std::expected<void, core::PlexError> connect_to_server(const core::ServerId& server_id) override;
    void disconnect_from_server(const core::ServerId& server_id) override;
    bool is_server_connected(const core::ServerId& server_id) const override;

    std::string get_preferred_server_uri(const core::ServerId& server_id) override;
    std::expected<void, core::PlexError> test_connection(const core::ServerId& server_id) override;

    void set_sse_event_callback(SSEEventCallback callback) override;
    void start_all_connections() override;
    void stop_all_connections() override;

private:
    void setup_server_sse_connection(PlexServerRuntime& runtime);
    bool test_uri_accessibility(const std::string& uri, const core::PlexToken& token);

    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::ConfigurationService> m_config_service;
    mutable std::mutex m_servers_mutex;
    std::map<core::ServerId, std::unique_ptr<PlexServerRuntime>> m_servers;
    SSEEventCallback m_sse_callback;
    std::atomic<bool> m_shutting_down{false};
};

} // namespace services
} // namespace presence_for_plex
