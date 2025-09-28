#include "presence_for_plex/core/models.hpp"
#include <cmath>
#include <algorithm>

namespace presence_for_plex {
namespace core {

// MediaInfo validation
bool MediaInfo::is_valid() const {
    // Basic validation: must have a title and valid duration
    if (title.empty()) {
        return false;
    }

    // Duration should be non-negative
    if (duration < 0) {
        return false;
    }

    // Progress should be within duration bounds
    if (progress < 0 || (duration > 0 && progress > duration)) {
        return false;
    }

    // Type-specific validation
    switch (type) {
        case MediaType::TVShow:
            // TV shows need valid season/episode numbers
            if (season < 0 || episode < 0) {
                return false;
            }
            break;
        case MediaType::Music:
            // Music needs artist info
            if (artist.empty()) {
                return false;
            }
            break;
        default:
            break;
    }

    return true;
}

// DiscordConfig validation
bool DiscordConfig::is_valid() const {
    // Client ID is required
    if (client_id.empty()) {
        return false;
    }

    // Update interval should be within configured limits
    if (update_interval < ConfigLimits::MIN_UPDATE_INTERVAL ||
        update_interval > ConfigLimits::MAX_UPDATE_INTERVAL) {
        return false;
    }

    return true;
}

// PlexConfig validation
bool PlexConfig::is_valid() const {
    // Poll interval should be within configured limits
    if (poll_interval < ConfigLimits::MIN_POLL_INTERVAL ||
        poll_interval > ConfigLimits::MAX_POLL_INTERVAL) {
        return false;
    }

    // Timeout should be within configured limits
    if (timeout < ConfigLimits::MIN_TIMEOUT ||
        timeout > ConfigLimits::MAX_TIMEOUT) {
        return false;
    }

    return true;
}

// ApplicationConfig validation
bool ApplicationConfig::is_valid() const {
    // Validate sub-configs
    if (!discord.is_valid() || !plex.is_valid()) {
        return false;
    }

    // Log level enum is always valid by design
    return true;
}

} // namespace core
} // namespace presence_for_plex
