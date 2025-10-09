#include "presence_for_plex/services/connection_manager.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <algorithm>
#include <cassert>
#include <thread>
#include <mutex>

namespace presence_for_plex::services {

ConnectionManager::ConnectionManager(
    std::unique_ptr<IConnectionStrategy> strategy,
    ConnectionRetryConfig config)
    : m_strategy(std::move(strategy))
    , m_config(std::move(config)) {

    if (!m_config.is_valid()) {
        LOG_WARNING("ConnectionManager", "Invalid configuration, using defaults");
        m_config = ConnectionRetryConfig{};
    }

    assert(m_strategy && "Connection strategy cannot be null");

    LOG_DEBUG("ConnectionManager",
        "Initialized with backoff " + std::to_string(m_config.initial_delay.count()) +
        "s to " + std::to_string(m_config.max_delay.count()) + "s");
}

ConnectionManager::~ConnectionManager() {
    stop();
}

bool ConnectionManager::start() {
    if (m_running.exchange(true)) {
        LOG_WARNING("ConnectionManager", "Already running");
        return is_connected();
    }

    LOG_INFO("ConnectionManager", "Starting connection management");

    // Try initial connection
    bool initial_success = attempt_connection();

    // Handle successful initial connection
    if (initial_success) {
        handle_connection_success(false);
    }

    // Start management thread
    m_management_thread = std::thread([this] { management_loop(); });

    return initial_success;
}

void ConnectionManager::stop() {
    if (!m_running.exchange(false)) {
        return;
    }

    LOG_INFO("ConnectionManager", "Stopping connection management");

    if (m_management_thread.joinable()) {
        m_management_thread.join();
    }

    if (m_strategy) {
        m_strategy->disconnect();
    }

    m_connected = false;
    notify_connection_state(false);
}

bool ConnectionManager::is_connected() const {
    return m_connected && m_strategy && m_strategy->is_connected();
}

void ConnectionManager::force_reconnect() {
    LOG_INFO("ConnectionManager", "Force reconnect requested");
    m_force_reconnect = true;
}

void ConnectionManager::set_connection_callback(ConnectionCallback callback) {
    m_connection_callback = std::move(callback);
}

void ConnectionManager::set_health_check_callback(HealthCheckCallback callback) {
    m_health_check_callback = std::move(callback);
}

ConnectionManager::RetryStats ConnectionManager::get_retry_stats() const {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    return m_stats;
}

void ConnectionManager::management_loop() {
    LOG_DEBUG("ConnectionManager", "Management loop started");

    auto last_health_check = std::chrono::steady_clock::now();
    int failed_health_checks = 0;

    while (m_running) {
        try {
            auto now = std::chrono::steady_clock::now();

            // Handle force reconnect
            if (m_force_reconnect.exchange(false)) {
                LOG_INFO("ConnectionManager", "Processing force reconnect");
                if (m_strategy) {
                    m_strategy->disconnect();
                }
                m_connected = false;
                reset_retry_state();
            }

            // Check if we need to reconnect
            if (!is_connected()) {
                if (should_attempt_reconnection()) {
                    if (attempt_connection()) {
                        handle_connection_success();
                        failed_health_checks = 0;
                        last_health_check = now;
                    } else {
                        handle_connection_failure();
                    }
                }
            } else {
                // Perform health checks when connected
                auto time_since_health_check = now - last_health_check;
                if (time_since_health_check >= m_config.health_check_interval) {
                    if (perform_health_check()) {
                        failed_health_checks = 0;
                        notify_health_check(true);
                    } else {
                        ++failed_health_checks;
                        notify_health_check(false);

                        if (failed_health_checks >= m_config.max_failed_health_checks) {
                            LOG_WARNING("ConnectionManager",
                                "Max health check failures (" +
                                std::to_string(failed_health_checks) + ") reached, disconnecting");

                            if (m_strategy) {
                                m_strategy->disconnect();
                            }
                            m_connected = false;
                            notify_connection_state(false);
                            failed_health_checks = 0;
                        }
                    }
                    last_health_check = now;
                }
            }

            // Sleep for a short interval
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        } catch (const std::exception& e) {
            LOG_ERROR("ConnectionManager",
                "Exception in management loop: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    LOG_DEBUG("ConnectionManager", "Management loop terminated");
}

bool ConnectionManager::attempt_connection() {
    if (!m_strategy) {
        return false;
    }

    LOG_DEBUG("ConnectionManager", "Attempting connection");

    try {
        bool success = m_strategy->connect();
        if (success) {
            LOG_INFO("ConnectionManager", "Connection successful");
            return true;
        } else {
            LOG_DEBUG("ConnectionManager", "Connection failed");
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("ConnectionManager",
            "Exception during connection: " + std::string(e.what()));
        return false;
    }
}

void ConnectionManager::handle_connection_success(bool is_reconnect) {
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        m_stats.last_success = std::chrono::system_clock::now();
        if (is_reconnect) {
            m_stats.total_reconnections++;
        }
        m_stats.consecutive_failures = 0;
        m_stats.current_delay = std::chrono::seconds{0};
    }

    m_connected = true;
    notify_connection_state(true);

    LOG_INFO("ConnectionManager", "Connection established successfully");
}

void ConnectionManager::handle_connection_failure() {
    int attempt_number;
    std::chrono::seconds next_delay;
    
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        m_stats.last_failure = std::chrono::system_clock::now();
        m_stats.consecutive_failures++;
        attempt_number = m_stats.consecutive_failures;
    }

    // Calculate delay without holding mutex to avoid deadlock
    next_delay = calculate_next_delay();
    
    {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        m_stats.current_delay = next_delay;
    }

    m_connected = false;
    notify_connection_state(false);

    LOG_DEBUG("ConnectionManager",
        "Connection failed (attempt " + std::to_string(attempt_number) +
        "), retrying in " + std::to_string(next_delay.count()) + "s");

    // Wait for the calculated delay
    auto end_time = std::chrono::steady_clock::now() + next_delay;
    while (m_running && std::chrono::steady_clock::now() < end_time) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Allow force reconnect to interrupt the delay
        if (m_force_reconnect) {
            break;
        }
    }
}

bool ConnectionManager::perform_health_check() {
    if (!m_strategy) {
        return false;
    }

    try {
        bool healthy = m_strategy->send_health_check();
        LOG_DEBUG("ConnectionManager", "Health check: " + std::string(healthy ? "OK" : "FAILED"));
        return healthy;
    } catch (const std::exception& e) {
        LOG_WARNING("ConnectionManager",
            "Health check exception: " + std::string(e.what()));
        return false;
    }
}

void ConnectionManager::notify_connection_state(bool connected) {
    if (m_connection_callback) {
        try {
            m_connection_callback(connected);
        } catch (const std::exception& e) {
            LOG_ERROR("ConnectionManager",
                "Exception in connection callback: " + std::string(e.what()));
        }
    }
}

void ConnectionManager::notify_health_check(bool healthy) {
    if (m_health_check_callback) {
        try {
            m_health_check_callback(healthy);
        } catch (const std::exception& e) {
            LOG_ERROR("ConnectionManager",
                "Exception in health check callback: " + std::string(e.what()));
        }
    }
}

std::chrono::seconds ConnectionManager::calculate_next_delay() {
    std::lock_guard<std::mutex> lock(m_stats_mutex);

    if (m_stats.consecutive_failures == 0) {
        return m_config.initial_delay;
    }

    // Exponential backoff with jitter
    auto delay = m_config.initial_delay;
    for (int i = 1; i < m_stats.consecutive_failures; ++i) {
        delay = std::chrono::seconds(static_cast<long>(static_cast<double>(delay.count()) * m_config.backoff_multiplier));
        if (delay >= m_config.max_delay) {
            delay = m_config.max_delay;
            break;
        }
    }

    // Add small random jitter (±10%) to prevent thundering herd
    auto jitter_range = delay.count() / 10;
    auto jitter = (std::rand() % (2 * jitter_range + 1)) - jitter_range;
    delay += std::chrono::seconds(jitter);

    return std::max(delay, m_config.initial_delay);
}

void ConnectionManager::reset_retry_state() {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats.consecutive_failures = 0;
    m_stats.current_delay = std::chrono::seconds{0};
}

bool ConnectionManager::should_attempt_reconnection() {
    std::lock_guard<std::mutex> lock(m_stats_mutex);

    if (m_stats.consecutive_failures >= m_config.max_consecutive_failures) {
        auto now = std::chrono::system_clock::now();
        auto time_since_last_failure = now - m_stats.last_failure;

        if (time_since_last_failure < m_config.failure_cooldown) {
            return false;
        }

        m_stats.consecutive_failures = 0;
        m_stats.current_delay = std::chrono::seconds{0};
    }

    return true;
}

} // namespace presence_for_plex::services