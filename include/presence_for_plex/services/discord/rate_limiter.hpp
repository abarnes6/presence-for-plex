#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>

namespace presence_for_plex::services {

/**
 * @brief Configuration for Discord rate limiting
 *
 * Based on Discord's documented rate limits and observed behavior
 */
struct DiscordRateLimitConfig {
    // Primary rate limit: max operations in sliding window
    int max_operations_per_window = 5;
    std::chrono::seconds primary_window_duration{15};

    // Secondary rate limit: burst protection
    int max_burst_operations = 3;
    std::chrono::seconds burst_window_duration{5};

    // Minimum interval between operations
    std::chrono::seconds minimum_interval{1};

    // Safety margin to stay well under Discord's limits
    double safety_factor = 0.8;

    bool is_valid() const {
        return max_operations_per_window > 0 &&
               max_burst_operations > 0 &&
               primary_window_duration > std::chrono::seconds{0} &&
               burst_window_duration > std::chrono::seconds{0} &&
               minimum_interval >= std::chrono::seconds{0} &&
               safety_factor > 0.0 && safety_factor <= 1.0;
    }
};

/**
 * @brief Sliding window rate limiter for Discord operations
 *
 * Implements both primary and burst rate limiting using sliding windows.
 * Thread-safe and follows Single Responsibility Principle.
 */
class DiscordRateLimiter {
public:
    explicit DiscordRateLimiter(DiscordRateLimitConfig config = {});

    bool can_proceed();
    void record_operation();
    void reset();
    std::chrono::milliseconds time_until_next_allowed() const;

    // Additional methods for monitoring
    size_t operations_in_window() const;
    size_t burst_operations_in_window() const;

private:
    mutable std::mutex m_mutex;
    DiscordRateLimitConfig m_config;

    mutable std::deque<std::chrono::steady_clock::time_point> m_operation_times;
    std::chrono::steady_clock::time_point m_last_operation;

    void cleanup_expired_operations() const;
    bool check_minimum_interval() const;
    bool check_primary_window() const;
    bool check_burst_window() const;

    std::chrono::milliseconds calculate_wait_time() const;
};

} // namespace presence_for_plex::services