#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/core/events.hpp"
#include <chrono>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace presence_for_plex {
namespace services {

using core::MediaInfo;
using core::DiscordError;
using core::EventBus;

// Rich presence data structure
struct PresenceData {
    std::string state;              // First line of rich presence
    std::string details;            // Second line of rich presence
    std::string large_image_key;    // Large image asset key
    std::string large_image_text;   // Large image hover text
    std::string small_image_key;    // Small image asset key
    std::string small_image_text;   // Small image hover text

    // Activity type for Discord (2 = Listening, 3 = Watching, 0 = Playing)
    int activity_type = 3;

    // Timestamps
    std::optional<std::chrono::system_clock::time_point> start_timestamp;
    std::optional<std::chrono::system_clock::time_point> end_timestamp;

    // Buttons (max 2)
    struct Button {
        std::string label;
        std::string url;

        bool operator==(const Button& other) const {
            return label == other.label && url == other.url;
        }
    };
    std::vector<Button> buttons;

    // Party information
    struct Party {
        std::string id;
        int current_size = 0;
        int max_size = 0;

        bool operator==(const Party& other) const {
            return id == other.id &&
                   current_size == other.current_size &&
                   max_size == other.max_size;
        }
    };
    std::optional<Party> party;

    bool is_valid() const;

    bool operator==(const PresenceData& other) const {
        return state == other.state &&
               details == other.details &&
               large_image_key == other.large_image_key &&
               large_image_text == other.large_image_text &&
               small_image_key == other.small_image_key &&
               small_image_text == other.small_image_text &&
               activity_type == other.activity_type &&
               start_timestamp == other.start_timestamp &&
               end_timestamp == other.end_timestamp &&
               buttons == other.buttons &&
               party == other.party;
    }

    bool operator!=(const PresenceData& other) const {
        return !(*this == other);
    }
};

// Abstract interface for rich presence services (Discord, Slack, etc.)
class PresenceService {
public:
    virtual ~PresenceService() = default;

    // Lifecycle management
    virtual std::expected<void, DiscordError> initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool is_connected() const = 0;

    // Rich presence management
    virtual std::expected<void, DiscordError> update_presence(const PresenceData& data) = 0;
    virtual std::expected<void, DiscordError> clear_presence() = 0;

    // Media integration
    virtual std::expected<void, DiscordError> update_from_media(const MediaInfo& media) = 0;

    // Event bus integration
    virtual void set_event_bus(std::shared_ptr<EventBus> bus) = 0;

    // Configuration
    virtual void set_update_interval(std::chrono::seconds interval) = 0;
    virtual std::chrono::seconds get_update_interval() const = 0;

    // Formatting configuration
    virtual void set_show_progress(bool show) = 0;
    virtual void set_show_buttons(bool show) = 0;
    virtual bool is_progress_shown() const = 0;
    virtual bool are_buttons_shown() const = 0;

    // Format template configuration
    virtual void set_details_format(const std::string& format) = 0;
    virtual void set_state_format(const std::string& format) = 0;
    virtual void set_large_image_text_format(const std::string& format) = 0;

protected:
    std::shared_ptr<EventBus> m_event_bus;

    // Format media to presence data
    PresenceData format_media(const MediaInfo& media) const;

    bool m_show_progress = true;
    bool m_show_buttons = true;

    // Format templates
    std::string m_details_format = "{title}";
    std::string m_state_format;
    std::string m_large_image_text_format = "{title}";
};

// Factory for creating presence service implementations
class PresenceServiceFactory {
public:
    static std::expected<std::unique_ptr<PresenceService>, core::ConfigError>
    create(core::PresenceServiceType type, const core::ApplicationConfig& config);
};

// Rich presence asset manager
class AssetManager {
public:
    virtual ~AssetManager() = default;

    // Asset management
    virtual std::expected<std::string, DiscordError> get_asset_key(const std::string& asset_name) const = 0;
    virtual std::expected<void, DiscordError> register_asset(const std::string& name, const std::string& key) = 0;
    virtual void clear_assets() = 0;

    // Built-in assets
    virtual std::string get_default_large_image() const = 0;
    virtual std::string get_play_icon() const = 0;
    virtual std::string get_pause_icon() const = 0;
    virtual std::string get_stop_icon() const = 0;
};

} // namespace services
} // namespace presence_for_plex