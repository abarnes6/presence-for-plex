#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/services/rate_limiter.hpp"
#include "presence_for_plex/services/connection_manager.hpp"
#include "presence_for_plex/services/frame_queue.hpp"
#include <string>
#include <vector>
#include <expected>

namespace presence_for_plex::utils {

/**
 * @brief Configuration validation errors
 */
enum class ValidationError {
    EmptyClientId,
    InvalidUpdateInterval,
    InvalidRateLimit,
    InvalidConnectionConfig,
    InvalidQueueConfig,
    InvalidLogLevel,
    InvalidServerUrl,
    MissingRequiredField
};

/**
 * @brief Detailed validation result
 */
struct ValidationResult {
    bool is_valid = true;
    std::vector<std::pair<ValidationError, std::string>> errors;
    std::vector<std::string> warnings;

    void add_error(ValidationError error, const std::string& message) {
        is_valid = false;
        errors.emplace_back(error, message);
    }

    void add_warning(const std::string& message) {
        warnings.emplace_back(message);
    }

    std::string get_error_summary() const {
        std::string summary;
        for (const auto& [error, message] : errors) {
            if (!summary.empty()) summary += "; ";
            summary += message;
        }
        return summary;
    }

    std::string get_warning_summary() const {
        std::string summary;
        for (const auto& warning : warnings) {
            if (!summary.empty()) summary += "; ";
            summary += warning;
        }
        return summary;
    }
};

/**
 * @brief Comprehensive configuration validator
 *
 * Validates all configuration objects and provides detailed feedback
 * about issues and potential improvements.
 */
class ConfigValidator {
public:
    static ValidationResult validate_discord_config(const core::DiscordConfig& config);
    static ValidationResult validate_plex_config(const core::PlexConfig& config);
    static ValidationResult validate_application_config(const core::ApplicationConfig& config);

    static ValidationResult validate_rate_limit_config(const services::DiscordRateLimitConfig& config);
    static ValidationResult validate_connection_config(const services::ConnectionRetryConfig& config);
    static ValidationResult validate_frame_queue_config(const services::FrameQueueConfig& config);

    /**
     * @brief Validate Discord client ID format
     */
    static bool is_valid_discord_client_id(const std::string& client_id);

    /**
     * @brief Validate URL format
     */
    static bool is_valid_url(const std::string& url);

    /**
     * @brief Validate log level string
     */
    static bool is_valid_log_level(const std::string& level);

    /**
     * @brief Get recommended configurations based on use case
     */
    static services::DiscordRateLimitConfig get_recommended_rate_limit_config();
    static services::ConnectionRetryConfig get_recommended_connection_config();
    static services::FrameQueueConfig get_recommended_queue_config();

    /**
     * @brief Validate and suggest improvements for configuration
     */
    static ValidationResult validate_and_suggest(const core::ApplicationConfig& config);

private:
    static bool is_numeric_string(const std::string& str);
    static bool is_reasonable_interval(std::chrono::seconds interval,
                                       std::chrono::seconds min_recommended,
                                       std::chrono::seconds max_recommended);
};

/**
 * @brief Configuration sanitizer that fixes common issues
 */
class ConfigSanitizer {
public:
    /**
     * @brief Sanitize Discord configuration
     * Fixes common issues and applies safe defaults
     */
    static core::DiscordConfig sanitize_discord_config(const core::DiscordConfig& config);

    /**
     * @brief Sanitize Plex configuration
     */
    static core::PlexConfig sanitize_plex_config(const core::PlexConfig& config);

    /**
     * @brief Sanitize application configuration
     */
    static core::ApplicationConfig sanitize_application_config(const core::ApplicationConfig& config);

    /**
     * @brief Sanitize rate limit configuration
     */
    static services::DiscordRateLimitConfig sanitize_rate_limit_config(
        const services::DiscordRateLimitConfig& config);

    /**
     * @brief Sanitize connection configuration
     */
    static services::ConnectionRetryConfig sanitize_connection_config(
        const services::ConnectionRetryConfig& config);

    /**
     * @brief Sanitize frame queue configuration
     */
    static services::FrameQueueConfig sanitize_queue_config(
        const services::FrameQueueConfig& config);

private:
    template<typename T>
    static T clamp_value(T value, T min_val, T max_val) {
        return std::max(min_val, std::min(value, max_val));
    }

    static std::string sanitize_client_id(const std::string& client_id);
    static std::string sanitize_url(const std::string& url);
    static std::string sanitize_log_level(const std::string& level);
};

} // namespace presence_for_plex::utils