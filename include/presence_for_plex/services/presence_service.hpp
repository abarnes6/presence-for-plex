#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/utils/expected.hpp"
#include <functional>
#include <memory>

namespace presence_for_plex {
namespace services {

using core::MediaInfo;
using core::DiscordError;

// Rich presence data structure
struct PresenceData {
    std::string state;              // First line of rich presence
    std::string details;            // Second line of rich presence
    std::string large_image_key;    // Large image asset key
    std::string large_image_text;   // Large image hover text
    std::string small_image_key;    // Small image asset key
    std::string small_image_text;   // Small image hover text

    // Timestamps
    std::optional<std::chrono::system_clock::time_point> start_timestamp;
    std::optional<std::chrono::system_clock::time_point> end_timestamp;

    // Buttons (max 2)
    struct Button {
        std::string label;
        std::string url;
    };
    std::vector<Button> buttons;

    // Party information
    struct Party {
        std::string id;
        int current_size = 0;
        int max_size = 0;
    };
    std::optional<Party> party;

    bool is_valid() const;
};

// Callback types for presence events
using PresenceUpdateCallback = std::function<void(const PresenceData&)>;
using PresenceErrorCallback = std::function<void(DiscordError, const std::string&)>;
using PresenceConnectionStateCallback = std::function<void(bool)>;

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

    // Event callbacks
    virtual void set_update_callback(PresenceUpdateCallback callback) = 0;
    virtual void set_error_callback(PresenceErrorCallback callback) = 0;
    virtual void set_connection_callback(PresenceConnectionStateCallback callback) = 0;

    // Configuration
    virtual void set_update_interval(std::chrono::seconds interval) = 0;
    virtual std::chrono::seconds get_update_interval() const = 0;

protected:
    // Template method pattern for subclasses
    virtual void on_presence_updated(const PresenceData& data) = 0;
    virtual void on_connection_state_changed(bool connected) = 0;
    virtual void on_error_occurred(DiscordError error, const std::string& message) = 0;
};

// Rich presence formatter interface
class PresenceFormatter {
public:
    virtual ~PresenceFormatter() = default;

    // Convert media info to presence data
    virtual PresenceData format_media(const MediaInfo& media) const = 0;

    // Customization methods
    virtual void set_show_progress(bool show) = 0;
    virtual void set_show_buttons(bool show) = 0;
    virtual void set_custom_format(const std::string& format) = 0;

    virtual bool is_progress_shown() const = 0;
    virtual bool are_buttons_shown() const = 0;

    // Factory method
    static std::unique_ptr<PresenceFormatter> create_default_formatter();
};

// Factory interface for creating presence services
class PresenceServiceFactory {
public:
    virtual ~PresenceServiceFactory() = default;

    enum class ServiceType {
        Discord,
        Slack,
        Teams
    };

    virtual std::unique_ptr<PresenceService> create_service(
        ServiceType type,
        const core::ApplicationConfig& config
    ) = 0;

    static std::unique_ptr<PresenceServiceFactory> create_default_factory();

    /**
     * @brief Create factory with robustness features
     * @return Factory that creates Discord presence services
     */
    static std::unique_ptr<PresenceServiceFactory> create_enhanced_factory();
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

    // Factory method
    static std::unique_ptr<AssetManager> create_default_manager();
};

} // namespace services
} // namespace presence_for_plex