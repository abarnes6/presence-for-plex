#include "presence_for_plex/services/presence_service.hpp"
#include "presence_for_plex/services/discord_presence_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <sstream>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <regex>

namespace presence_for_plex::services {

namespace {

std::string format_duration(double seconds) {
    int total_seconds = static_cast<int>(seconds);
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int secs = total_seconds % 60;

    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << ":" << std::setfill('0') << std::setw(2) << minutes
            << ":" << std::setw(2) << secs;
    } else {
        oss << minutes << ":" << std::setfill('0') << std::setw(2) << secs;
    }
    return oss.str();
}

std::string format_progress_percentage(double progress, double duration) {
    if (duration <= 0) return "0%";
    double percentage = (progress / duration) * 100.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << percentage << "%";
    return oss.str();
}

std::string replace_placeholders(const std::string& format, const core::MediaInfo& media) {
    std::string result = format;

    auto replace = [&result](const std::string& placeholder, const std::string& value) {
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), value);
            pos += value.length();
        }
    };

    // Basic media information
    replace("{title}", media.title);
    replace("{original_title}", media.original_title);
    replace("{year}", media.year > 0 ? std::to_string(media.year) : "");
    replace("{studio}", media.studio);
    replace("{summary}", media.summary);

    // TV show specific
    replace("{show}", media.grandparent_title);
    replace("{show_title}", media.grandparent_title);
    replace("{season}", media.season > 0 ? std::to_string(media.season) : "");
    replace("{episode}", media.episode > 0 ? std::to_string(media.episode) : "");
    replace("{season_padded}", media.season > 0 ? (media.season < 10 ? "0" + std::to_string(media.season) : std::to_string(media.season)) : "");
    replace("{episode_padded}", media.episode > 0 ? (media.episode < 10 ? "0" + std::to_string(media.episode) : std::to_string(media.episode)) : "");

    // Music specific
    replace("{artist}", media.artist);
    replace("{album}", media.album);
    replace("{track}", media.track > 0 ? std::to_string(media.track) : "");

    // Playback state
    std::string state_str;
    switch (media.state) {
        case core::PlaybackState::Playing: state_str = "Playing"; break;
        case core::PlaybackState::Paused: state_str = "Paused"; break;
        case core::PlaybackState::Buffering: state_str = "Buffering"; break;
        case core::PlaybackState::Stopped: state_str = "Stopped"; break;
        default: state_str = "Unknown"; break;
    }
    replace("{state}", state_str);

    // Media type
    std::string type_str;
    switch (media.type) {
        case core::MediaType::Movie: type_str = "Movie"; break;
        case core::MediaType::TVShow: type_str = "TV Show"; break;
        case core::MediaType::Music: type_str = "Music"; break;
        default: type_str = "Media"; break;
    }
    replace("{type}", type_str);

    // Playback progress
    replace("{progress}", format_duration(media.progress));
    replace("{duration}", format_duration(media.duration));
    replace("{progress_percentage}", format_progress_percentage(media.progress, media.duration));
    replace("{remaining}", format_duration(media.duration - media.progress));

    // User information
    replace("{username}", media.username);

    // Genres
    if (!media.genres.empty()) {
        std::string genres_str = std::accumulate(
            std::next(media.genres.begin()),
            media.genres.end(),
            media.genres[0],
            [](const std::string& a, const std::string& b) {
                return a + ", " + b;
            }
        );
        replace("{genres}", genres_str);
        replace("{genre}", media.genres[0]);
    } else {
        replace("{genres}", "");
        replace("{genre}", "");
    }

    // Rating
    if (media.rating > 0) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << media.rating;
        replace("{rating}", oss.str());
    } else {
        replace("{rating}", "");
    }

    // Season/Episode formatting helpers
    if (media.season > 0 && media.episode > 0) {
        std::ostringstream se_format;
        se_format << "S" << media.season << " • E" << media.episode;
        replace("{se}", se_format.str());

        std::ostringstream sxe_format;
        sxe_format << "S" << std::setfill('0') << std::setw(2) << media.season
                   << "E" << std::setw(2) << media.episode;
        replace("{SxE}", sxe_format.str());
    } else {
        replace("{se}", "");
        replace("{SxE}", "");
    }

    return result;
}

} // anonymous namespace

bool PresenceData::is_valid() const {
    return !state.empty() || !details.empty() || !large_image_key.empty();
}

PresenceData PresenceService::format_media(const MediaInfo& media) const {
    PresenceData data;

    // Default large image
    data.large_image_key = "plex_logo";
    if (!media.art_path.empty()) {
        data.large_image_key = media.art_path;
        LOG_DEBUG("PresenceService", "Using art_path for large_image: " + media.art_path);
    } else {
        LOG_DEBUG("PresenceService", "art_path is empty, using default plex_logo");
    }

    // Set activity type based on media type
    if (media.type == core::MediaType::Music) {
        data.activity_type = 2; // Listening
    } else if (media.type == core::MediaType::TVShow || media.type == core::MediaType::Movie) {
        data.activity_type = 3; // Watching
    } else {
        data.activity_type = 0; // Playing (generic)
    }

    // Apply format templates with placeholders
    std::string details_format = m_details_format;
    std::string state_format = m_state_format;

    // Use default formats if not configured
    if (details_format.empty()) {
        if (media.type == core::MediaType::TVShow) {
            details_format = "{show}";
        } else if (media.type == core::MediaType::Movie) {
            details_format = "{title}";
        } else if (media.type == core::MediaType::Music) {
            details_format = "{title}";
        } else {
            details_format = "{title}";
        }
    }

    if (state_format.empty()) {
        if (media.type == core::MediaType::TVShow) {
            state_format = "{se} - {title}";
        } else if (media.type == core::MediaType::Movie) {
            state_format = "{genres}";
        } else if (media.type == core::MediaType::Music) {
            state_format = "{artist} - {album}";
        } else {
            state_format = "Playing media";
        }
    }

    // Replace placeholders
    data.details = replace_placeholders(details_format, media);
    data.state = replace_placeholders(state_format, media);

    // Large image text
    if (!m_large_image_text_format.empty()) {
        data.large_image_text = replace_placeholders(m_large_image_text_format, media);
    } else {
        data.large_image_text = media.title;
    }

    // Handle playback state overrides
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

        // Set format templates
        service->set_details_format(config.presence.discord.details_format);
        service->set_state_format(config.presence.discord.state_format);
        service->set_large_image_text_format(config.presence.discord.large_image_text_format);

        return service;
    }

    return std::unexpected(core::ConfigError::InvalidFormat);
}

} // namespace presence_for_plex::services