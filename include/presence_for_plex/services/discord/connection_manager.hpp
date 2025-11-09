#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

namespace presence_for_plex::services {

// Forward declaration
class DiscordIPC;

/**
 * @brief Configuration for connection retry behavior
 */
struct ConnectionRetryConfig {
    // Initial retry delay
    std::chrono::seconds initial_delay{1};

    // Maximum retry delay (backoff cap)
    std::chrono::seconds max_delay{60};

    // Backoff multiplier
    double backoff_multiplier = 2.0;

    // Maximum number of consecutive failures before giving up temporarily
    int max_consecutive_failures = 10;

    // How long to wait after max failures before trying again
    std::chrono::seconds failure_cooldown{300}; // 5 minutes

    // Health check interval when connected
    std::chrono::seconds health_check_interval{60};

    // How many failed health checks before considering disconnected
    int max_failed_health_checks = 3;

    bool is_valid() const {
        return initial_delay > std::chrono::seconds{0} &&
               max_delay >= initial_delay &&
               backoff_multiplier > 1.0 &&
               max_consecutive_failures > 0 &&
               failure_cooldown > std::chrono::seconds{0} &&
               health_check_interval > std::chrono::seconds{0} &&
               max_failed_health_checks > 0;
    }
};

/**
 * @brief Manages connection lifecycle with retry logic and health monitoring
 *
 * Implements exponential backoff, health checking, and failure recovery.
 * Follows Single Responsibility Principle by focusing only on connection management.
 */
class ConnectionManager {
public:
    using ConnectionCallback = std::function<void(bool)>;
    using HealthCheckCallback = std::function<void(bool)>;

    explicit ConnectionManager(
        std::unique_ptr<DiscordIPC> ipc,
        ConnectionRetryConfig config = {}
    );

    ~ConnectionManager();

    // Delete copy and move operations (non-copyable, non-movable due to atomics and threads)
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;
    ConnectionManager(ConnectionManager&&) = delete;
    ConnectionManager& operator=(ConnectionManager&&) = delete;

    /**
     * @brief Start connection management
     * @return true if initial connection successful, false otherwise
     */
    bool start();

    /**
     * @brief Stop connection management and disconnect
     */
    void stop();

    /**
     * @brief Check if currently connected
     */
    bool is_connected() const;

    /**
     * @brief Force a reconnection attempt
     */
    void force_reconnect();

    /**
     * @brief Set callback for connection state changes
     */
    void set_connection_callback(ConnectionCallback callback);

    /**
     * @brief Set callback for health check results
     */
    void set_health_check_callback(HealthCheckCallback callback);

    /**
     * @brief Get current retry statistics
     */
    struct RetryStats {
        int consecutive_failures = 0;
        std::chrono::seconds current_delay{0};
        int total_reconnections = 0;
        std::chrono::system_clock::time_point last_success;
        std::chrono::system_clock::time_point last_failure;
    };

    RetryStats get_retry_stats() const;

    /**
     * @brief Get the Discord IPC instance
     * @return Pointer to the IPC, or nullptr if not available
     */
    DiscordIPC* get_ipc() const {
        return m_ipc.get();
    }

private:
    std::unique_ptr<DiscordIPC> m_ipc;
    ConnectionRetryConfig m_config;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_force_reconnect{false};

    mutable std::mutex m_stats_mutex;
    RetryStats m_stats;

    ConnectionCallback m_connection_callback;
    HealthCheckCallback m_health_check_callback;

    std::thread m_management_thread;

    void management_loop();
    bool attempt_connection();
    void handle_connection_success(bool is_reconnect = true);
    void handle_connection_failure();
    bool perform_health_check();
    void notify_connection_state(bool connected);
    void notify_health_check(bool healthy);

    std::chrono::seconds calculate_next_delay();
    void reset_retry_state();
    bool should_attempt_reconnection();
};

} // namespace presence_for_plex::services