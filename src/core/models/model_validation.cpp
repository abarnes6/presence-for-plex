#include "presence_for_plex/core/models.hpp"
#include <cmath>
#include <algorithm>

namespace presence_for_plex {
namespace core {

// MediaInfo validation
std::expected<void, core::ValidationError> MediaInfo::validate() const {
    // Basic validation: must have a title and valid duration
    if (title.empty()) {
        return std::unexpected(core::ValidationError::EmptyTitle);
    }

    // Duration should be non-negative
    if (duration < 0) {
        return std::unexpected(core::ValidationError::InvalidDuration);
    }

    // Progress should be within duration bounds
    if (progress < 0 || (duration > 0 && progress > duration)) {
        return std::unexpected(core::ValidationError::ProgressOutOfBounds);
    }

    // Type-specific validation
    switch (type) {
        case MediaType::TVShow:
            // TV shows need valid season/episode numbers
            if (season < 0) {
                return std::unexpected(core::ValidationError::MissingSeasonInfo);
            }
            if (episode < 0) {
                return std::unexpected(core::ValidationError::MissingEpisodeInfo);
            }
            break;
        case MediaType::Music:
            // Music needs artist info
            if (artist.empty()) {
                return std::unexpected(core::ValidationError::MissingRequiredField);
            }
            break;
        default:
            break;
    }

    return {};
}

// Legacy compatibility method
bool MediaInfo::is_valid() const {
    return validate().has_value();
}

// DiscordConfig validation
std::expected<void, core::ValidationError> DiscordConfig::validate() const {
    // Client ID is required
    if (client_id.empty()) {
        return std::unexpected(core::ValidationError::EmptyClientId);
    }

    // Update interval should be within configured limits
    if (update_interval < ConfigLimits::MIN_UPDATE_INTERVAL ||
        update_interval > ConfigLimits::MAX_UPDATE_INTERVAL) {
        return std::unexpected(core::ValidationError::InvalidUpdateInterval);
    }

    return {};
}

// Legacy compatibility method
bool DiscordConfig::is_valid() const {
    return validate().has_value();
}

// PlexConfig validation
std::expected<void, core::ValidationError> PlexConfig::validate() const {
    // Poll interval should be within configured limits
    if (poll_interval < ConfigLimits::MIN_POLL_INTERVAL ||
        poll_interval > ConfigLimits::MAX_POLL_INTERVAL) {
        return std::unexpected(core::ValidationError::InvalidPollInterval);
    }

    // Timeout should be within configured limits
    if (timeout < ConfigLimits::MIN_TIMEOUT ||
        timeout > ConfigLimits::MAX_TIMEOUT) {
        return std::unexpected(core::ValidationError::InvalidFormat);
    }

    return {};
}

// Legacy compatibility method
bool PlexConfig::is_valid() const {
    return validate().has_value();
}

// ApplicationConfig validation
std::expected<void, core::ValidationError> ApplicationConfig::validate() const {
    // Validate sub-configs
    auto discord_result = discord.validate();
    if (!discord_result) {
        return discord_result;
    }

    auto plex_result = plex.validate();
    if (!plex_result) {
        return plex_result;
    }

    // Log level enum is always valid by design
    return {};
}

// Legacy compatibility method
bool ApplicationConfig::is_valid() const {
    return validate().has_value();
}

// Version information methods
std::string ApplicationConfig::version_string() const {
#ifdef VERSION_STRING
    return VERSION_STRING;
#else
    return "0.0.0";
#endif
}

int ApplicationConfig::version_major() const {
#ifdef VERSION_MAJOR
    return VERSION_MAJOR;
#else
    return 0;
#endif
}

int ApplicationConfig::version_minor() const {
#ifdef VERSION_MINOR
    return VERSION_MINOR;
#else
    return 0;
#endif
}

int ApplicationConfig::version_patch() const {
#ifdef VERSION_PATCH
    return VERSION_PATCH;
#else
    return 0;
#endif
}

} // namespace core
} // namespace presence_for_plex
