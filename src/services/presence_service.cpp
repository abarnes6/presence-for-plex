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
    if (m_show_artwork && !media.art_path.empty()) {
        data.large_image_key = media.art_path;
        LOG_DEBUG("PresenceService", "Using art_path for large_image: " + media.art_path);
    } else if (!media.art_path.empty()) {
        LOG_DEBUG("PresenceService", "Artwork disabled, using default plex_logo");
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

    // Select format templates based on media type
    std::string details_format;
    std::string state_format;
    std::string large_image_text_format;

    if (media.type == core::MediaType::TVShow) {
        details_format = m_tv_details_format;
        state_format = m_tv_state_format;
        large_image_text_format = m_tv_large_image_text_format;
    } else if (media.type == core::MediaType::Movie) {
        details_format = m_movie_details_format;
        state_format = m_movie_state_format;
        large_image_text_format = m_movie_large_image_text_format;
    } else if (media.type == core::MediaType::Music) {
        details_format = m_music_details_format;
        state_format = m_music_state_format;
        large_image_text_format = m_music_large_image_text_format;
    } else {
        // Fallback for unknown media types
        details_format = "{title}";
        state_format = "Playing media";
        large_image_text_format = "{title}";
    }

    // Replace placeholders
    data.details = replace_placeholders(details_format, media);
    data.state = replace_placeholders(state_format, media);

    // Large image text
    if (!large_image_text_format.empty()) {
        data.large_image_text = replace_placeholders(large_image_text_format, media);
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

    // Handle timestamps based on show_progress setting
    if (m_show_progress) {
        // Show progress bar with elapsed/remaining time
        if (media.state == core::PlaybackState::Playing) {
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
    } else {
        // Show elapsed time since session started (keeps increasing even when paused)
        // Only set if session_created_at has been initialized (not epoch)
        if (media.session_created_at != std::chrono::system_clock::time_point{}) {
            data.start_timestamp = media.session_created_at;
            // No end timestamp - this shows elapsed time since session creation
        }
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
        service->set_show_artwork(config.presence.discord.show_artwork);

        // Set format templates
        service->set_tv_details_format(config.presence.discord.tv_details_format);
        service->set_tv_state_format(config.presence.discord.tv_state_format);
        service->set_tv_large_image_text_format(config.presence.discord.tv_large_image_text_format);
        service->set_movie_details_format(config.presence.discord.movie_details_format);
        service->set_movie_state_format(config.presence.discord.movie_state_format);
        service->set_movie_large_image_text_format(config.presence.discord.movie_large_image_text_format);
        service->set_music_details_format(config.presence.discord.music_details_format);
        service->set_music_state_format(config.presence.discord.music_state_format);
        service->set_music_large_image_text_format(config.presence.discord.music_large_image_text_format);

        return service;
    }

    return std::unexpected(core::ConfigError::InvalidFormat);
}

} // namespace presence_for_plex::services