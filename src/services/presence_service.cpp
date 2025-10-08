#include "presence_for_plex/services/presence_service.hpp"
#include "presence_for_plex/services/discord_presence_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <sstream>
#include <numeric>
#include <algorithm>

namespace presence_for_plex::services {

bool PresenceData::is_valid() const {
    return !state.empty() || !details.empty() || !large_image_key.empty();
}

PresenceData PresenceService::format_media(const MediaInfo& media) const {
    PresenceData data;

    // Default large image
    data.large_image_key = "plex_logo";
    if (!media.art_path.empty()) {
        data.large_image_key = media.art_path;
        PLEX_LOG_DEBUG("PresenceService", "Using art_path for large_image: " + media.art_path);
    } else {
        PLEX_LOG_DEBUG("PresenceService", "art_path is empty, using default plex_logo");
    }

    // Format based on media type
    if (media.type == core::MediaType::TVShow) {
        // TV Show formatting
        data.activity_type = 3; // Watching
        data.details = media.grandparent_title; // Show title

        // Format state as "S1 • E1 - Episode Title"
        std::stringstream ss;
        ss << "S" << media.season;
        ss << " • ";
        ss << "E" << media.episode;
        ss << " - " << media.title;
        data.state = ss.str();

        data.large_image_text = media.grandparent_title; // Show title on hover
    }
    else if (media.type == core::MediaType::Movie) {
        // Movie formatting
        data.activity_type = 3; // Watching
        data.details = media.title;
        if (media.year > 0) {
            data.details += " (" + std::to_string(media.year) + ")";
        }

        // Use genres for state if available
        if (!media.genres.empty()) {
            data.state = std::accumulate(
                std::next(media.genres.begin()),
                media.genres.end(),
                media.genres[0],
                [](std::string a, const std::string& b) {
                    return a + ", " + b;
                }
            );
        } else {
            data.state = "Watching Movie";
        }

        data.large_image_text = media.title; // Movie title on hover
    }
    else if (media.type == core::MediaType::Music) {
        // Music formatting
        data.activity_type = 2; // Listening
        data.details = media.title; // Track title

        // Format state as "Artist - Album"
        std::string state_str;
        if (!media.artist.empty()) {
            state_str = media.artist; // Artist
        }
        if (!media.album.empty()) {
            if (!state_str.empty()) state_str += " - ";
            state_str += media.album; // Album
        }
        data.state = state_str.empty() ? "Listening to Music" : state_str;

        data.large_image_text = media.title;
    }
    else {
        // Unknown or other types
        data.activity_type = 0; // Playing (generic)
        data.details = media.title;
        data.state = "Playing media";
        data.large_image_text = media.title;
    }

    // Handle playback state
    if (media.state == core::PlaybackState::Buffering) {
        data.state = "🔄 Buffering...";
    }
    else if (media.state == core::PlaybackState::Paused) {
        data.small_image_key = "paused";
        data.small_image_text = "Paused";
    }
    else if (media.state == core::PlaybackState::Stopped) {
        data.state = "Stopped";
    }

    // Calculate timestamps for progress bar if enabled and playing
    if (m_show_progress && media.state == core::PlaybackState::Playing) {
        auto now = std::chrono::system_clock::now();

        // Calculate when playback started and will end
        auto progress_sec = static_cast<int64_t>(media.progress);
        auto duration_sec = static_cast<int64_t>(media.duration);

        data.start_timestamp = now - std::chrono::seconds(progress_sec);
        data.end_timestamp = now + std::chrono::seconds(duration_sec - progress_sec);
    }
    else if (media.state == core::PlaybackState::Paused ||
             media.state == core::PlaybackState::Buffering) {
        // For paused/buffering, set far future timestamps to show static progress
        constexpr int MAX_PAUSED_DURATION = 9999;
        auto now = std::chrono::system_clock::now();
        auto max_duration = std::chrono::hours(MAX_PAUSED_DURATION);

        data.start_timestamp = now + max_duration;
        auto duration_sec = static_cast<int64_t>(media.duration);
        data.end_timestamp = now + max_duration + std::chrono::seconds(duration_sec);
    }

    // Add buttons if enabled
    if (m_show_buttons) {
        // Add external ID buttons (MyAnimeList, IMDb, etc.)
        if (!media.mal_id.empty()) {
            data.buttons.push_back({
                "View on MyAnimeList",
                "https://myanimelist.net/anime/" + media.mal_id
            });
        }
        if (!media.imdb_id.empty() && data.buttons.size() < 2) {
            data.buttons.push_back({
                "View on IMDb",
                "https://www.imdb.com/title/" + media.imdb_id
            });
        }
    }

    // Ensure we have valid details and state
    if (data.details.empty()) {
        data.details = "Watching something...";
    }
    if (data.state.empty()) {
        data.state = "Idle";
    }

    return data;
}

std::expected<std::unique_ptr<PresenceService>, core::ConfigError>
PresenceServiceFactory::create(core::PresenceServiceType type, const core::ApplicationConfig& config) {
    if (type == core::PresenceServiceType::Discord) {
        auto service_result = DiscordPresenceService::create(config);
        if (!service_result) {
            return std::unexpected(service_result.error());
        }
        auto service = std::move(*service_result);

        service->set_show_buttons(config.presence.discord.show_buttons);
        service->set_show_progress(config.presence.discord.show_progress);

        return service;
    }

    return std::unexpected(core::ConfigError::InvalidFormat);
}

} // namespace presence_for_plex::services