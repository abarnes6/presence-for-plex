#include "presence_for_plex/services/discord/presence_builder.hpp"
#include "presence_for_plex/utils/format_utils.hpp"

namespace presence_for_plex::services {

using json = nlohmann::json;

PresenceBuilder::PresenceBuilder(FormatOptions options)
    : m_options(std::move(options)) {}

PresenceData PresenceBuilder::from_media(const core::MediaInfo& media) const {
    PresenceData data;

    // Default large image
    data.large_image_key = "plex_logo";
    if (m_options.show_artwork && !media.art_path.empty()) {
        data.large_image_key = media.art_path;
    }

    // Activity type based on media type
    if (media.type == core::MediaType::Music) {
        data.activity_type = 2; // Listening
    } else if (media.type == core::MediaType::TVShow || media.type == core::MediaType::Movie) {
        data.activity_type = 3; // Watching
    } else {
        data.activity_type = 0; // Playing
    }

    // Select format templates based on media type
    std::string details_format, state_format, large_image_text_format;

    if (media.type == core::MediaType::TVShow) {
        details_format = m_options.tv_details;
        state_format = m_options.tv_state;
        large_image_text_format = m_options.tv_large_image_text;
    } else if (media.type == core::MediaType::Movie) {
        details_format = m_options.movie_details;
        state_format = m_options.movie_state;
        large_image_text_format = m_options.movie_large_image_text;
    } else if (media.type == core::MediaType::Music) {
        details_format = m_options.music_details;
        state_format = m_options.music_state;
        large_image_text_format = m_options.music_large_image_text;
    } else {
        details_format = "{title}";
        state_format = "Playing media";
        large_image_text_format = "{title}";
    }

    // Apply format templates
    data.details = utils::replace_placeholders(details_format, media);
    data.state = utils::replace_placeholders(state_format, media);
    data.large_image_text = large_image_text_format.empty() ? media.title
        : utils::replace_placeholders(large_image_text_format, media);

    apply_playback_state(data, media);
    apply_timestamps(data, media);
    apply_buttons(data, media);

    // Ensure valid defaults
    if (data.details.empty()) data.details = "Watching something...";
    if (data.state.empty()) data.state = "Idle";

    return data;
}

void PresenceBuilder::apply_playback_state(PresenceData& data, const core::MediaInfo& media) const {
    if (media.state == core::PlaybackState::Buffering) {
        data.state = "🔄 Buffering...";
    } else if (media.state == core::PlaybackState::Paused) {
        data.small_image_key = "paused";
        data.small_image_text = "Paused";
    } else if (media.state == core::PlaybackState::Stopped) {
        data.state = "Stopped";
    }
}

void PresenceBuilder::apply_timestamps(PresenceData& data, const core::MediaInfo& media) const {
    if (m_options.show_progress) {
        if (media.state == core::PlaybackState::Playing) {
            auto now = std::chrono::system_clock::now();
            auto progress_sec = static_cast<int64_t>(media.progress);
            auto duration_sec = static_cast<int64_t>(media.duration);

            data.start_timestamp = now - std::chrono::seconds(progress_sec);
            data.end_timestamp = now + std::chrono::seconds(duration_sec - progress_sec);
        } else if (media.state == core::PlaybackState::Paused ||
                   media.state == core::PlaybackState::Buffering) {
            constexpr int MAX_PAUSED_DURATION = 9999;
            auto now = std::chrono::system_clock::now();
            auto max_duration = std::chrono::hours(MAX_PAUSED_DURATION);

            data.start_timestamp = now + max_duration;
            data.end_timestamp = now + max_duration + std::chrono::seconds(static_cast<int64_t>(media.duration));
        }
    } else {
        if (media.session_created_at != std::chrono::system_clock::time_point{}) {
            data.start_timestamp = media.session_created_at;
        }
    }
}

void PresenceBuilder::apply_buttons(PresenceData& data, const core::MediaInfo& media) const {
    if (!m_options.show_buttons) return;

    if (!media.mal_id.empty()) {
        data.buttons.push_back({"View on MyAnimeList", "https://myanimelist.net/anime/" + media.mal_id});
    }
    if (!media.imdb_id.empty() && data.buttons.size() < 2) {
        data.buttons.push_back({"View on IMDb", "https://www.imdb.com/title/" + media.imdb_id});
    }
}

json PresenceBuilder::to_json(const PresenceData& data) {
    json activity;

    if (data.details.empty() && data.state.empty() && data.large_image_key.empty()) {
        return activity;
    }

    activity["type"] = data.activity_type;
    activity["status_display_type"] = 2;
    activity["instance"] = true;

    if (!data.details.empty()) activity["details"] = data.details;
    if (!data.state.empty()) activity["state"] = data.state;

    // Assets
    if (!data.large_image_key.empty() || !data.small_image_key.empty()) {
        json assets;
        if (!data.large_image_key.empty()) {
            assets["large_image"] = data.large_image_key;
            if (!data.large_image_text.empty()) assets["large_text"] = data.large_image_text;
        }
        if (!data.small_image_key.empty()) {
            assets["small_image"] = data.small_image_key;
            if (!data.small_image_text.empty()) assets["small_text"] = data.small_image_text;
        }
        activity["assets"] = assets;
    }

    // Timestamps
    if (data.start_timestamp || data.end_timestamp) {
        json timestamps;
        if (data.start_timestamp) {
            auto epoch = data.start_timestamp->time_since_epoch();
            timestamps["start"] = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
        }
        if (data.end_timestamp) {
            auto epoch = data.end_timestamp->time_since_epoch();
            timestamps["end"] = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
        }
        activity["timestamps"] = timestamps;
    }

    // Buttons
    if (!data.buttons.empty()) {
        json buttons = json::array();
        for (const auto& button : data.buttons) {
            if (buttons.size() >= 2) break;
            buttons.push_back({{"label", button.label}, {"url", button.url}});
        }
        activity["buttons"] = buttons;
    }

    // Party
    if (data.party) {
        json party;
        party["id"] = data.party->id;
        if (data.party->current_size > 0 && data.party->max_size > 0) {
            party["size"] = {data.party->current_size, data.party->max_size};
        }
        activity["party"] = party;
    }

    return activity;
}

// Setters
void PresenceBuilder::set_show_progress(bool show) { m_options.show_progress = show; }
void PresenceBuilder::set_show_buttons(bool show) { m_options.show_buttons = show; }
void PresenceBuilder::set_show_artwork(bool show) { m_options.show_artwork = show; }

void PresenceBuilder::set_tv_details_format(const std::string& format) { m_options.tv_details = format; }
void PresenceBuilder::set_tv_state_format(const std::string& format) { m_options.tv_state = format; }
void PresenceBuilder::set_tv_large_image_text_format(const std::string& format) { m_options.tv_large_image_text = format; }
void PresenceBuilder::set_movie_details_format(const std::string& format) { m_options.movie_details = format; }
void PresenceBuilder::set_movie_state_format(const std::string& format) { m_options.movie_state = format; }
void PresenceBuilder::set_movie_large_image_text_format(const std::string& format) { m_options.movie_large_image_text = format; }
void PresenceBuilder::set_music_details_format(const std::string& format) { m_options.music_details = format; }
void PresenceBuilder::set_music_state_format(const std::string& format) { m_options.music_state = format; }
void PresenceBuilder::set_music_large_image_text_format(const std::string& format) { m_options.music_large_image_text = format; }

} // namespace presence_for_plex::services
