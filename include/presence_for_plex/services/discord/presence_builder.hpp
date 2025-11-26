#pragma once

#include "presence_for_plex/core/models.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <optional>
#include <chrono>

namespace presence_for_plex::services {

// Discord rich presence data
struct PresenceData {
    std::string state;
    std::string details;
    std::string large_image_key;
    std::string large_image_text;
    std::string small_image_key;
    std::string small_image_text;

    // Activity type (2 = Listening, 3 = Watching, 0 = Playing)
    int activity_type = 3;

    std::optional<std::chrono::system_clock::time_point> start_timestamp;
    std::optional<std::chrono::system_clock::time_point> end_timestamp;

    struct Button {
        std::string label;
        std::string url;
        bool operator==(const Button& other) const {
            return label == other.label && url == other.url;
        }
    };
    std::vector<Button> buttons;

    struct Party {
        std::string id;
        int current_size = 0;
        int max_size = 0;
        bool operator==(const Party& other) const {
            return id == other.id && current_size == other.current_size && max_size == other.max_size;
        }
    };
    std::optional<Party> party;

    bool is_valid() const {
        return !state.empty() || !details.empty() || !large_image_key.empty();
    }

    bool operator==(const PresenceData& other) const {
        return state == other.state && details == other.details &&
               large_image_key == other.large_image_key && large_image_text == other.large_image_text &&
               small_image_key == other.small_image_key && small_image_text == other.small_image_text &&
               activity_type == other.activity_type &&
               start_timestamp == other.start_timestamp && end_timestamp == other.end_timestamp &&
               buttons == other.buttons && party == other.party;
    }

    bool operator!=(const PresenceData& other) const { return !(*this == other); }
};

class PresenceBuilder {
public:
    struct FormatOptions {
        bool show_progress = true;
        bool show_buttons = true;
        bool show_artwork = true;

        // TV format templates
        std::string tv_details = "{show}";
        std::string tv_state = "{se} - {title}";
        std::string tv_large_image_text = "{title}";

        // Movie format templates
        std::string movie_details = "{title} ({year})";
        std::string movie_state = "{genres}";
        std::string movie_large_image_text = "{title}";

        // Music format templates
        std::string music_details = "{title}";
        std::string music_state = "{artist} - {album}";
        std::string music_large_image_text = "{title}";
    };

    explicit PresenceBuilder(FormatOptions options = {});

    // Convert MediaInfo to PresenceData
    PresenceData from_media(const core::MediaInfo& media) const;

    // Convert PresenceData to Discord activity JSON
    static nlohmann::json to_json(const PresenceData& data);

    // Option setters
    void set_show_progress(bool show);
    void set_show_buttons(bool show);
    void set_show_artwork(bool show);

    void set_tv_details_format(const std::string& format);
    void set_tv_state_format(const std::string& format);
    void set_tv_large_image_text_format(const std::string& format);
    void set_movie_details_format(const std::string& format);
    void set_movie_state_format(const std::string& format);
    void set_movie_large_image_text_format(const std::string& format);
    void set_music_details_format(const std::string& format);
    void set_music_state_format(const std::string& format);
    void set_music_large_image_text_format(const std::string& format);

    // Option getters
    bool is_progress_shown() const { return m_options.show_progress; }
    bool are_buttons_shown() const { return m_options.show_buttons; }
    bool is_artwork_shown() const { return m_options.show_artwork; }

private:
    FormatOptions m_options;

    void apply_playback_state(PresenceData& data, const core::MediaInfo& media) const;
    void apply_timestamps(PresenceData& data, const core::MediaInfo& media) const;
    void apply_buttons(PresenceData& data, const core::MediaInfo& media) const;
};

} // namespace presence_for_plex::services
