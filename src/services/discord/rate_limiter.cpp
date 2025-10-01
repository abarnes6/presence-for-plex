#include "presence_for_plex/services/rate_limiter.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <algorithm>

namespace presence_for_plex::services {

DiscordRateLimiter::DiscordRateLimiter(DiscordRateLimitConfig config)
    : m_config(std::move(config)) {

    if (!m_config.is_valid()) {
        PLEX_LOG_WARNING("RateLimiter", "Invalid rate limit configuration, using defaults");
        m_config = DiscordRateLimitConfig{};
    }

    // Apply safety factor to limits
    m_config.max_operations_per_window = static_cast<int>(
        m_config.max_operations_per_window * m_config.safety_factor);
    m_config.max_burst_operations = static_cast<int>(
        m_config.max_burst_operations * m_config.safety_factor);

    PLEX_LOG_DEBUG("RateLimiter",
        "Initialized with " + std::to_string(m_config.max_operations_per_window) +
        " ops/" + std::to_string(m_config.primary_window_duration.count()) + "s, " +
        std::to_string(m_config.max_burst_operations) +
        " burst/" + std::to_string(m_config.burst_window_duration.count()) + "s");
}

bool DiscordRateLimiter::can_proceed() {
    std::lock_guard<std::mutex> lock(m_mutex);

    cleanup_expired_operations();

    if (!check_minimum_interval()) {
        PLEX_LOG_DEBUG("RateLimiter", "Blocked by minimum interval");
        return false;
    }

    if (!check_primary_window()) {
        PLEX_LOG_DEBUG("RateLimiter", "Blocked by primary window limit");
        return false;
    }

    if (!check_burst_window()) {
        PLEX_LOG_DEBUG("RateLimiter", "Blocked by burst window limit");
        return false;
    }

    return true;
}

void DiscordRateLimiter::record_operation() {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = std::chrono::steady_clock::now();
    m_operation_times.push_back(now);
    m_last_operation = now;

    PLEX_LOG_DEBUG("RateLimiter",
        "Operation recorded. Current window: " + std::to_string(m_operation_times.size()) +
        "/" + std::to_string(m_config.max_operations_per_window));
}

void DiscordRateLimiter::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_operation_times.clear();
    m_last_operation = {};

    PLEX_LOG_DEBUG("RateLimiter", "Rate limiter reset");
}

std::chrono::milliseconds DiscordRateLimiter::time_until_next_allowed() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    cleanup_expired_operations();
    return calculate_wait_time();
}

size_t DiscordRateLimiter::operations_in_window() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    cleanup_expired_operations();
    return m_operation_times.size();
}

size_t DiscordRateLimiter::burst_operations_in_window() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = std::chrono::steady_clock::now();
    auto burst_cutoff = now - m_config.burst_window_duration;

    return static_cast<size_t>(std::count_if(m_operation_times.begin(), m_operation_times.end(),
        [burst_cutoff](const auto& time) {
            return time >= burst_cutoff;
        }));
}

void DiscordRateLimiter::cleanup_expired_operations() const {
    // Remove const since we need to modify the deque
    auto& operations = const_cast<std::deque<std::chrono::steady_clock::time_point>&>(m_operation_times);
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - m_config.primary_window_duration;

    // Remove operations outside the primary window
    while (!operations.empty() && operations.front() < cutoff) {
        operations.pop_front();
    }
}

bool DiscordRateLimiter::check_minimum_interval() const {
    if (m_last_operation == std::chrono::steady_clock::time_point{}) {
        return true;  // No previous operation
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - m_last_operation;

    return elapsed >= m_config.minimum_interval;
}

bool DiscordRateLimiter::check_primary_window() const {
    return static_cast<int>(m_operation_times.size()) < m_config.max_operations_per_window;
}

bool DiscordRateLimiter::check_burst_window() const {
    auto now = std::chrono::steady_clock::now();
    auto burst_cutoff = now - m_config.burst_window_duration;

    int burst_count = static_cast<int>(std::count_if(m_operation_times.begin(), m_operation_times.end(),
        [burst_cutoff](const auto& time) {
            return time >= burst_cutoff;
        }));

    return burst_count < m_config.max_burst_operations;
}

std::chrono::milliseconds DiscordRateLimiter::calculate_wait_time() const {
    auto now = std::chrono::steady_clock::now();
    std::chrono::milliseconds max_wait{0};

    // Check minimum interval wait
    if (m_last_operation != std::chrono::steady_clock::time_point{}) {
        auto elapsed = now - m_last_operation;
        if (elapsed < m_config.minimum_interval) {
            auto interval_wait = std::chrono::duration_cast<std::chrono::milliseconds>(
                m_config.minimum_interval - elapsed);
            max_wait = std::max(max_wait, interval_wait);
        }
    }

    // Check primary window wait
    if (static_cast<int>(m_operation_times.size()) >= m_config.max_operations_per_window &&
        !m_operation_times.empty()) {
        auto oldest_operation = m_operation_times.front();
        auto window_expires = oldest_operation + m_config.primary_window_duration;
        if (window_expires > now) {
            auto window_wait = std::chrono::duration_cast<std::chrono::milliseconds>(
                window_expires - now);
            max_wait = std::max(max_wait, window_wait);
        }
    }

    // Check burst window wait
    auto burst_cutoff = now - m_config.burst_window_duration;
    auto burst_operations = std::count_if(m_operation_times.begin(), m_operation_times.end(),
        [burst_cutoff](const auto& time) {
            return time >= burst_cutoff;
        });

    if (static_cast<int>(burst_operations) >= m_config.max_burst_operations) {
        // Find the oldest operation in the burst window
        auto oldest_in_burst = std::find_if(m_operation_times.begin(), m_operation_times.end(),
            [burst_cutoff](const auto& time) {
                return time >= burst_cutoff;
            });

        if (oldest_in_burst != m_operation_times.end()) {
            auto burst_expires = *oldest_in_burst + m_config.burst_window_duration;
            if (burst_expires > now) {
                auto burst_wait = std::chrono::duration_cast<std::chrono::milliseconds>(
                    burst_expires - now);
                max_wait = std::max(max_wait, burst_wait);
            }
        }
    }

    return max_wait;
}

} // namespace presence_for_plex::services