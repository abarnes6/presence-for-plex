#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/services/discord/rate_limiter.hpp"
#include "presence_for_plex/services/discord/connection_manager.hpp"
#include "presence_for_plex/services/discord/discord_ipc.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <expected>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <optional>

namespace presence_for_plex::services {

// Discord rich presence data
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

    bool is_valid() const {
        return !state.empty() || !details.empty() || !large_image_key.empty();
    }

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

class DiscordPresenceService {
public:
    struct Config {
        // Discord configuration
        std::string client_id;
        std::chrono::seconds update_interval{15};

        // Rate limiting configuration
        DiscordRateLimitConfig rate_limit_config;

        // Connection retry configuration
        ConnectionRetryConfig connection_config;

        // Feature flags
        bool enable_rate_limiting = true;
        bool enable_health_checks = true;

        bool is_valid() const {
            return !client_id.empty() &&
                   update_interval > std::chrono::seconds{0} &&
                   rate_limit_config.is_valid() &&
                   connection_config.is_valid();
        }
    };

    ~DiscordPresenceService();

    // PresenceService implementation
    std::expected<void, core::DiscordError> initialize();
    void shutdown();
    bool is_connected() const;

    std::expected<void, core::DiscordError> update_presence(const PresenceData& data);
    std::expected<void, core::DiscordError> clear_presence();
    std::expected<void, core::DiscordError> update_from_media(const core::MediaInfo& media);

    void set_event_bus(std::shared_ptr<core::EventBus> bus);

    void set_update_interval(std::chrono::seconds interval);
    std::chrono::seconds get_update_interval() const;

    // Formatting configuration
    void set_show_progress(bool show);
    void set_show_buttons(bool show);
    void set_show_artwork(bool show);
    bool is_progress_shown() const;
    bool are_buttons_shown() const;
    bool is_artwork_shown() const;

    // Format template configuration
    void set_tv_details_format(const std::string& format);
    void set_tv_state_format(const std::string& format);
    void set_tv_large_image_text_format(const std::string& format);
    void set_movie_details_format(const std::string& format);
    void set_movie_state_format(const std::string& format);
    void set_movie_large_image_text_format(const std::string& format);
    void set_music_details_format(const std::string& format);
    void set_music_state_format(const std::string& format);
    void set_music_large_image_text_format(const std::string& format);

    // Additional functionality
    struct ServiceStats {
        size_t total_presence_updates = 0;
        size_t failed_presence_updates = 0;
        size_t rate_limited_updates = 0;
        ConnectionManager::RetryStats connection_stats;

        std::chrono::system_clock::time_point last_successful_update;
        std::chrono::system_clock::time_point service_start_time;
    };

    ServiceStats get_service_stats() const;

    /**
     * @brief Force a connection retry
     */
    void force_reconnect();

    /**
     * @brief Get current configuration
     */
    const Config& get_config() const { return m_config; }

    /**
     * @brief Update configuration dynamically
     * @param config New configuration to apply
     */
    void update_config(const Config& config);

protected:
    void on_presence_updated(const PresenceData& data);
    void on_connection_state_changed(bool connected);
    void on_error_occurred(core::DiscordError error, const std::string& message);

    // Event publishing helpers
    void publish_presence_updated(const PresenceData& data);
    void publish_presence_cleared(const std::string& reason = "");
    void publish_discord_connected(const std::string& app_id);
    void publish_discord_disconnected(const std::string& reason, bool will_retry);
    void publish_discord_error(core::DiscordError error, const std::string& message);

public:
    explicit DiscordPresenceService(Config config);

    static std::expected<std::unique_ptr<DiscordPresenceService>, core::ConfigError>
    create(const core::ApplicationConfig& app_config);

private:

    Config m_config;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shutting_down{false};

    // Event bus
    std::shared_ptr<core::EventBus> m_event_bus;

    // Formatting configuration
    bool m_show_progress = true;
    bool m_show_buttons = true;
    bool m_show_artwork = true;

    // Format templates - TV Shows
    std::string m_tv_details_format = "{show}";
    std::string m_tv_state_format = "{se} - {title}";
    std::string m_tv_large_image_text_format = "{title}";

    // Format templates - Movies
    std::string m_movie_details_format = "{title} ({year})";
    std::string m_movie_state_format = "{genres}";
    std::string m_movie_large_image_text_format = "{title}";

    // Format templates - Music
    std::string m_music_details_format = "{title}";
    std::string m_music_state_format = "{artist} - {album}";
    std::string m_music_large_image_text_format = "{title}";

    // Core components
    std::unique_ptr<DiscordRateLimiter> m_rate_limiter;
    std::unique_ptr<ConnectionManager> m_connection_manager;
    std::optional<nlohmann::json> m_pending_frame;

    // Threading
    std::thread m_update_thread;
    std::atomic<bool> m_update_requested{false};

    // Synchronization
    mutable std::mutex m_stats_mutex;
    mutable std::mutex m_presence_mutex;
    mutable std::mutex m_pending_mutex;
    std::mutex m_shutdown_mutex;
    std::condition_variable m_shutdown_cv;
    std::condition_variable m_update_cv;
    PresenceData m_current_presence;
    PresenceData m_last_sent_presence;

    // Statistics
    mutable ServiceStats m_stats;

    // Internal methods
    void update_loop();
    void process_pending_frame();
    bool send_presence_frame(const nlohmann::json& frame);
    nlohmann::json create_discord_activity(const PresenceData& data) const;
    bool presence_changed(const PresenceData& old_data, const PresenceData& new_data) const;

    void increment_stat_counter(size_t ServiceStats::*counter) const;
    void record_successful_update();
    void record_failed_update();

    // Connection event handlers
    void handle_connection_changed(bool connected);
    void handle_health_check_result(bool healthy);

    // Format media to presence data
    PresenceData format_media(const core::MediaInfo& media) const;
};

} // namespace presence_for_plex::services