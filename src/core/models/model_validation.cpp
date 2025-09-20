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

// Progress calculation
double MediaInfo::progress_percentage() const {
    if (duration <= 0) {
        return 0.0;
    }
    return std::clamp((progress / duration) * 100.0, 0.0, 100.0);
}

std::chrono::seconds MediaInfo::remaining_time() const {
    if (duration <= progress) {
        return std::chrono::seconds(0);
    }
    return std::chrono::seconds(static_cast<int64_t>(duration - progress));
}

// DiscordConfig validation
bool DiscordConfig::is_valid() const {
    // Application ID is required
    if (application_id.empty()) {
        return false;
    }

    // Update interval should be reasonable (1s to 5min)
    if (update_interval.count() < 1 || update_interval.count() > 300) {
        return false;
    }

    return true;
}

// PlexConfig validation
bool PlexConfig::is_valid() const {
    // Poll interval should be reasonable (1s to 1min)
    if (poll_interval.count() < 1 || poll_interval.count() > 60) {
        return false;
    }

    // Timeout should be reasonable (5s to 5min)
    if (timeout.count() < 5 || timeout.count() > 300) {
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

    // Log level should be valid
    if (log_level != "trace" && log_level != "debug" &&
        log_level != "info" && log_level != "warning" &&
        log_level != "error" && log_level != "critical") {
        return false;
    }

    return true;
}

} // namespace core
} // namespace presence_for_plex
