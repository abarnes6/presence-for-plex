#include "presence_for_plex/utils/config_validator.hpp"
#include <regex>
#include <algorithm>
#include <cctype>

namespace presence_for_plex::utils {

ValidationResult ConfigValidator::validate_discord_config(const core::DiscordConfig& config) {
    ValidationResult result;

    // Validate client ID
    if (config.application_id.empty()) {
        result.add_error(ValidationError::EmptyClientId, "Discord application ID cannot be empty");
    } else if (!is_valid_discord_client_id(config.application_id)) {
        result.add_error(ValidationError::EmptyClientId, "Discord application ID format is invalid");
    }

    // Validate update interval
    if (config.update_interval < std::chrono::seconds{1}) {
        result.add_error(ValidationError::InvalidUpdateInterval,
            "Update interval must be at least 1 second");
    } else if (config.update_interval < std::chrono::seconds{5}) {
        result.add_warning("Update interval less than 5 seconds may cause rate limiting issues");
    } else if (config.update_interval > std::chrono::seconds{300}) {
        result.add_warning("Update interval over 5 minutes may result in stale presence");
    }

    return result;
}

ValidationResult ConfigValidator::validate_plex_config(const core::PlexConfig& config) {
    ValidationResult result;

    // Validate server URLs
    for (const auto& url : config.server_urls) {
        if (!is_valid_url(url)) {
            result.add_error(ValidationError::InvalidServerUrl,
                "Invalid server URL: " + url);
        }
    }

    // Validate intervals
    if (config.poll_interval < std::chrono::seconds{1}) {
        result.add_error(ValidationError::InvalidUpdateInterval,
            "Poll interval must be at least 1 second");
    } else if (config.poll_interval < std::chrono::seconds{2}) {
        result.add_warning("Poll interval less than 2 seconds may overload Plex server");
    }

    if (config.timeout < std::chrono::seconds{5}) {
        result.add_error(ValidationError::InvalidUpdateInterval,
            "Timeout must be at least 5 seconds");
    } else if (config.timeout > std::chrono::seconds{120}) {
        result.add_warning("Timeout over 2 minutes may cause poor responsiveness");
    }

    return result;
}

ValidationResult ConfigValidator::validate_application_config(const core::ApplicationConfig& config) {
    ValidationResult result;

    // Validate Discord config
    auto discord_result = validate_discord_config(config.discord);
    result.errors.insert(result.errors.end(), discord_result.errors.begin(), discord_result.errors.end());
    result.warnings.insert(result.warnings.end(), discord_result.warnings.begin(), discord_result.warnings.end());
    result.is_valid = result.is_valid && discord_result.is_valid;

    // Validate Plex config
    auto plex_result = validate_plex_config(config.plex);
    result.errors.insert(result.errors.end(), plex_result.errors.begin(), plex_result.errors.end());
    result.warnings.insert(result.warnings.end(), plex_result.warnings.begin(), plex_result.warnings.end());
    result.is_valid = result.is_valid && plex_result.is_valid;

    // Validate log level
    if (!is_valid_log_level(config.log_level)) {
        result.add_error(ValidationError::InvalidLogLevel,
            "Invalid log level: " + config.log_level);
    }

    return result;
}

ValidationResult ConfigValidator::validate_rate_limit_config(const services::DiscordRateLimitConfig& config) {
    ValidationResult result;

    if (config.max_operations_per_window <= 0) {
        result.add_error(ValidationError::InvalidRateLimit,
            "Max operations per window must be positive");
    } else if (config.max_operations_per_window > 10) {
        result.add_warning("High operations per window may trigger Discord rate limits");
    }

    if (config.max_burst_operations <= 0) {
        result.add_error(ValidationError::InvalidRateLimit,
            "Max burst operations must be positive");
    }

    if (config.primary_window_duration <= std::chrono::seconds{0}) {
        result.add_error(ValidationError::InvalidRateLimit,
            "Primary window duration must be positive");
    }

    if (config.burst_window_duration <= std::chrono::seconds{0}) {
        result.add_error(ValidationError::InvalidRateLimit,
            "Burst window duration must be positive");
    }

    if (config.safety_factor <= 0.0 || config.safety_factor > 1.0) {
        result.add_error(ValidationError::InvalidRateLimit,
            "Safety factor must be between 0 and 1");
    } else if (config.safety_factor < 0.5) {
        result.add_warning("Low safety factor may not provide adequate protection");
    }

    return result;
}

ValidationResult ConfigValidator::validate_connection_config(const services::ConnectionRetryConfig& config) {
    ValidationResult result;

    if (config.initial_delay <= std::chrono::seconds{0}) {
        result.add_error(ValidationError::InvalidConnectionConfig,
            "Initial delay must be positive");
    }

    if (config.max_delay < config.initial_delay) {
        result.add_error(ValidationError::InvalidConnectionConfig,
            "Max delay must be >= initial delay");
    }

    if (config.backoff_multiplier <= 1.0) {
        result.add_error(ValidationError::InvalidConnectionConfig,
            "Backoff multiplier must be > 1.0");
    } else if (config.backoff_multiplier > 5.0) {
        result.add_warning("High backoff multiplier may cause very long retry delays");
    }

    if (config.max_consecutive_failures <= 0) {
        result.add_error(ValidationError::InvalidConnectionConfig,
            "Max consecutive failures must be positive");
    }

    return result;
}

ValidationResult ConfigValidator::validate_frame_queue_config(const services::FrameQueueConfig& config) {
    ValidationResult result;

    if (config.max_queue_size == 0) {
        result.add_error(ValidationError::InvalidQueueConfig,
            "Max queue size must be positive");
    } else if (config.max_queue_size > 1000) {
        result.add_warning("Large queue size may consume significant memory");
    }

    if (config.default_ttl <= std::chrono::seconds{0}) {
        result.add_error(ValidationError::InvalidQueueConfig,
            "Default TTL must be positive");
    }

    if (config.max_frame_age < config.default_ttl) {
        result.add_error(ValidationError::InvalidQueueConfig,
            "Max frame age must be >= default TTL");
    }

    return result;
}

bool ConfigValidator::is_valid_discord_client_id(const std::string& client_id) {
    // Discord client IDs are 18-digit snowflake IDs
    if (client_id.length() < 17 || client_id.length() > 19) {
        return false;
    }

    return std::all_of(client_id.begin(), client_id.end(), ::isdigit);
}

bool ConfigValidator::is_valid_url(const std::string& url) {
    // Basic URL validation
    std::regex url_pattern(R"(^https?://[\w\-]+(\.[\w\-]+)*(:[\d]+)?(/.*)?$)");
    return std::regex_match(url, url_pattern);
}

bool ConfigValidator::is_valid_log_level(const std::string& level) {
    static const std::vector<std::string> valid_levels = {
        "trace", "debug", "info", "warning", "error", "critical"
    };

    std::string lower_level = level;
    std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(), ::tolower);

    return std::find(valid_levels.begin(), valid_levels.end(), lower_level) != valid_levels.end();
}

services::DiscordRateLimitConfig ConfigValidator::get_recommended_rate_limit_config() {
    services::DiscordRateLimitConfig config;
    config.max_operations_per_window = 5;
    config.primary_window_duration = std::chrono::seconds{15};
    config.max_burst_operations = 3;
    config.burst_window_duration = std::chrono::seconds{5};
    config.minimum_interval = std::chrono::seconds{1};
    config.safety_factor = 0.8;
    return config;
}

services::ConnectionRetryConfig ConfigValidator::get_recommended_connection_config() {
    services::ConnectionRetryConfig config;
    config.initial_delay = std::chrono::seconds{1};
    config.max_delay = std::chrono::seconds{60};
    config.backoff_multiplier = 2.0;
    config.max_consecutive_failures = 10;
    config.failure_cooldown = std::chrono::seconds{300};
    config.health_check_interval = std::chrono::seconds{60};
    config.max_failed_health_checks = 3;
    return config;
}

services::FrameQueueConfig ConfigValidator::get_recommended_queue_config() {
    services::FrameQueueConfig config;
    config.max_queue_size = 100;
    config.default_ttl = std::chrono::seconds{300};
    config.enable_expiration = true;
    config.replace_duplicates = true;
    config.max_frame_age = std::chrono::seconds{600};
    return config;
}

ValidationResult ConfigValidator::validate_and_suggest(const core::ApplicationConfig& config) {
    ValidationResult result = validate_application_config(config);

    // Add suggestions for improvement
    if (config.discord.update_interval > std::chrono::seconds{30}) {
        result.add_warning("Consider reducing update interval for more responsive presence updates");
    }

    if (config.plex.server_urls.empty() && !config.plex.auto_discover) {
        result.add_warning("No server URLs provided and auto-discovery disabled");
    }

    return result;
}

bool ConfigValidator::is_numeric_string(const std::string& str) {
    return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
}

bool ConfigValidator::is_reasonable_interval(std::chrono::seconds interval,
                                           std::chrono::seconds min_recommended,
                                           std::chrono::seconds max_recommended) {
    return interval >= min_recommended && interval <= max_recommended;
}

// ConfigSanitizer implementations
core::DiscordConfig ConfigSanitizer::sanitize_discord_config(const core::DiscordConfig& config) {
    core::DiscordConfig sanitized = config;

    // Sanitize client ID
    sanitized.application_id = sanitize_client_id(config.application_id);

    // Clamp update interval to reasonable range
    sanitized.update_interval = std::chrono::seconds{
        clamp_value(static_cast<long>(config.update_interval.count()), 5L, 300L)
    };

    return sanitized;
}

core::PlexConfig ConfigSanitizer::sanitize_plex_config(const core::PlexConfig& config) {
    core::PlexConfig sanitized = config;

    // Sanitize URLs
    for (auto& url : sanitized.server_urls) {
        url = sanitize_url(url);
    }

    // Clamp intervals
    sanitized.poll_interval = std::chrono::seconds{
        clamp_value(static_cast<long>(config.poll_interval.count()), 2L, 60L)
    };

    sanitized.timeout = std::chrono::seconds{
        clamp_value(static_cast<long>(config.timeout.count()), 5L, 120L)
    };

    return sanitized;
}

core::ApplicationConfig ConfigSanitizer::sanitize_application_config(const core::ApplicationConfig& config) {
    core::ApplicationConfig sanitized = config;

    sanitized.discord = sanitize_discord_config(config.discord);
    sanitized.plex = sanitize_plex_config(config.plex);
    sanitized.log_level = sanitize_log_level(config.log_level);

    return sanitized;
}

services::DiscordRateLimitConfig ConfigSanitizer::sanitize_rate_limit_config(
    const services::DiscordRateLimitConfig& config) {

    services::DiscordRateLimitConfig sanitized = config;

    sanitized.max_operations_per_window = clamp_value(config.max_operations_per_window, 1, 10);
    sanitized.max_burst_operations = clamp_value(config.max_burst_operations, 1, 5);

    sanitized.primary_window_duration = std::chrono::seconds{
        clamp_value(static_cast<long>(config.primary_window_duration.count()), 5L, 60L)
    };

    sanitized.burst_window_duration = std::chrono::seconds{
        clamp_value(static_cast<long>(config.burst_window_duration.count()), 1L, 30L)
    };

    sanitized.minimum_interval = std::chrono::seconds{
        clamp_value(static_cast<long>(config.minimum_interval.count()), 0L, 10L)
    };

    sanitized.safety_factor = clamp_value(config.safety_factor, 0.5, 1.0);

    return sanitized;
}

services::ConnectionRetryConfig ConfigSanitizer::sanitize_connection_config(
    const services::ConnectionRetryConfig& config) {

    services::ConnectionRetryConfig sanitized = config;

    sanitized.initial_delay = std::chrono::seconds{
        clamp_value(static_cast<long>(config.initial_delay.count()), 1L, 30L)
    };

    sanitized.max_delay = std::chrono::seconds{
        clamp_value(static_cast<long>(config.max_delay.count()),
                   static_cast<long>(sanitized.initial_delay.count()), 300L)
    };

    sanitized.backoff_multiplier = clamp_value(config.backoff_multiplier, 1.5, 5.0);
    sanitized.max_consecutive_failures = clamp_value(config.max_consecutive_failures, 3, 50);

    sanitized.failure_cooldown = std::chrono::seconds{
        clamp_value(static_cast<long>(config.failure_cooldown.count()), 60L, 1800L)
    };

    sanitized.health_check_interval = std::chrono::seconds{
        clamp_value(static_cast<long>(config.health_check_interval.count()), 30L, 300L)
    };

    sanitized.max_failed_health_checks = clamp_value(config.max_failed_health_checks, 1, 10);

    return sanitized;
}

services::FrameQueueConfig ConfigSanitizer::sanitize_queue_config(
    const services::FrameQueueConfig& config) {

    services::FrameQueueConfig sanitized = config;

    sanitized.max_queue_size = clamp_value(config.max_queue_size, static_cast<size_t>(10), static_cast<size_t>(1000));

    sanitized.default_ttl = std::chrono::seconds{
        clamp_value(static_cast<long>(config.default_ttl.count()), 60L, 3600L)
    };

    sanitized.max_frame_age = std::chrono::seconds{
        clamp_value(static_cast<long>(config.max_frame_age.count()),
                   static_cast<long>(sanitized.default_ttl.count()), 7200L)
    };

    return sanitized;
}

std::string ConfigSanitizer::sanitize_client_id(const std::string& client_id) {
    // Remove non-numeric characters
    std::string sanitized;
    std::copy_if(client_id.begin(), client_id.end(), std::back_inserter(sanitized), ::isdigit);
    return sanitized;
}

std::string ConfigSanitizer::sanitize_url(const std::string& url) {
    std::string sanitized = url;

    // Ensure it starts with http:// or https://
    if (!sanitized.empty() && sanitized.find("://") == std::string::npos) {
        sanitized = "http://" + sanitized;
    }

    return sanitized;
}

std::string ConfigSanitizer::sanitize_log_level(const std::string& level) {
    std::string lower_level = level;
    std::transform(lower_level.begin(), lower_level.end(), lower_level.begin(), ::tolower);

    static const std::vector<std::string> valid_levels = {
        "trace", "debug", "info", "warning", "error", "critical"
    };

    if (std::find(valid_levels.begin(), valid_levels.end(), lower_level) != valid_levels.end()) {
        return lower_level;
    }

    return "info"; // Default safe level
}

} // namespace presence_for_plex::utils