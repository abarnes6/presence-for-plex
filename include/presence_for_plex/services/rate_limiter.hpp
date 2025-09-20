#pragma once

#include <chrono>
#include <deque>
#include <mutex>

namespace presence_for_plex::services {

/**
 * @brief Interface for rate limiting implementations
 *
 * Follows the Interface Segregation Principle by providing a focused interface
 * for rate limiting operations.
 */
class IRateLimiter {
public:
    virtual ~IRateLimiter() = default;

    /**
     * @brief Check if an operation can be performed now
     * @return true if operation is allowed, false if rate limited
     */
    virtual bool can_proceed() = 0;

    /**
     * @brief Record that an operation was performed
     * Should be called after successful operation
     */
    virtual void record_operation() = 0;

    /**
     * @brief Reset the rate limiter state
     */
    virtual void reset() = 0;

    /**
     * @brief Get time until next operation is allowed
     * @return Duration to wait, or zero if immediate operation is allowed
     */
    virtual std::chrono::milliseconds time_until_next_allowed() const = 0;
};

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
class DiscordRateLimiter : public IRateLimiter {
public:
    explicit DiscordRateLimiter(DiscordRateLimitConfig config = {});

    bool can_proceed() override;
    void record_operation() override;
    void reset() override;
    std::chrono::milliseconds time_until_next_allowed() const override;

    // Additional methods for monitoring
    size_t operations_in_window() const;
    size_t burst_operations_in_window() const;

private:
    mutable std::mutex m_mutex;
    DiscordRateLimitConfig m_config;

    std::deque<std::chrono::steady_clock::time_point> m_operation_times;
    std::chrono::steady_clock::time_point m_last_operation;

    void cleanup_expired_operations() const;
    bool check_minimum_interval() const;
    bool check_primary_window() const;
    bool check_burst_window() const;

    std::chrono::milliseconds calculate_wait_time() const;
};

/**
 * @brief Factory for creating rate limiters
 *
 * Follows the Factory pattern and Open/Closed Principle
 */
class RateLimiterFactory {
public:
    enum class Type {
        Discord,
        None  // No-op rate limiter for testing
    };

    static std::unique_ptr<IRateLimiter> create(Type type, const DiscordRateLimitConfig& config = {});
    static std::unique_ptr<IRateLimiter> create_discord_limiter(const DiscordRateLimitConfig& config = {});
    static std::unique_ptr<IRateLimiter> create_no_op_limiter();
};

/**
 * @brief No-operation rate limiter for testing and debugging
 */
class NoOpRateLimiter : public IRateLimiter {
public:
    bool can_proceed() override { return true; }
    void record_operation() override {}
    void reset() override {}
    std::chrono::milliseconds time_until_next_allowed() const override { return std::chrono::milliseconds{0}; }
};

} // namespace presence_for_plex::services