#pragma once

#include "presence_for_plex/services/network_service.hpp"
#include "presence_for_plex/core/models.hpp"
#include <functional>
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <expected>

namespace presence_for_plex {
namespace services {

// SSE event callback type (basic callback, server-specific callback defined in connection manager)
using SSEBasicEventCallback = std::function<void(const std::string&)>;

// Interface for Server-Sent Events client
class ISSEClient {
public:
    virtual ~ISSEClient() = default;

    // Connection management
    virtual std::expected<void, core::PlexError> connect(
        const std::string& url,
        const HttpHeaders& headers,
        SSEBasicEventCallback callback
    ) = 0;

    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    // Status
    virtual std::string get_url() const = 0;
    virtual std::chrono::system_clock::time_point get_last_event_time() const = 0;
};

// Concrete SSE client implementation using HTTP streaming
class SSEClientImpl : public ISSEClient {
public:
    explicit SSEClientImpl(std::shared_ptr<HttpClient> http_client);
    ~SSEClientImpl() override;

    std::expected<void, core::PlexError> connect(
        const std::string& url,
        const HttpHeaders& headers,
        SSEBasicEventCallback callback
    ) override;

    void disconnect() override;
    bool is_connected() const override;

    std::string get_url() const override;
    std::chrono::system_clock::time_point get_last_event_time() const override;

    // Public method for processing streaming data chunks
    void process_streaming_data(const std::string& data_chunk);

private:
    void event_loop();
    void parse_sse_data(const std::string& data);
    void handle_event(const std::string& event_data);
    void process_sse_line(const std::string& line);
    void process_sse_field(const std::string& field, const std::string& value);

    std::shared_ptr<HttpClient> m_http_client;
    std::string m_url;
    HttpHeaders m_headers;
    SSEBasicEventCallback m_callback;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::thread m_event_thread;

    mutable std::mutex m_state_mutex;
    std::chrono::system_clock::time_point m_last_event_time;
    std::string m_partial_data;

    // Connection tracking
    int m_connection_attempts{0};
    static constexpr int MAX_INITIAL_CONNECTION_ATTEMPTS = 3;
    bool m_initial_connection_succeeded{false};

    // SSE event parsing state
    std::string m_current_event_data;
    std::string m_current_event_type;
    std::string m_current_event_id;

    static constexpr std::chrono::seconds RECONNECT_DELAY{5};
    static constexpr std::chrono::seconds CONNECTION_TIMEOUT{30};
};

// Factory for creating SSE clients
class SSEClientFactory {
public:
    static std::unique_ptr<ISSEClient> create_client(std::shared_ptr<HttpClient> http_client);
};

} // namespace services
} // namespace presence_for_plex