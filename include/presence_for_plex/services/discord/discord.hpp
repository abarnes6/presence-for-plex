#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/services/discord/discord_ipc.hpp"
#include "presence_for_plex/services/discord/presence_builder.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <deque>
#include <queue>
#include <expected>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <optional>

namespace presence_for_plex::services {

class Discord {
public:
    struct RateLimitConfig {
        int max_operations_per_window = 5;
        std::chrono::seconds primary_window_duration{15};
        int max_burst_operations = 3;
        std::chrono::seconds burst_window_duration{5};
        std::chrono::seconds minimum_interval{1};
        double safety_factor = 0.8;

        bool is_valid() const {
            return max_operations_per_window > 0 && max_burst_operations > 0 &&
                   primary_window_duration > std::chrono::seconds{0} &&
                   burst_window_duration > std::chrono::seconds{0} &&
                   minimum_interval >= std::chrono::seconds{0} &&
                   safety_factor > 0.0 && safety_factor <= 1.0;
        }
    };

    struct ConnectionConfig {
        std::chrono::seconds initial_delay{1};
        std::chrono::seconds max_delay{60};
        double backoff_multiplier = 2.0;
        int max_consecutive_failures = 10;
        std::chrono::seconds failure_cooldown{300};
        std::chrono::seconds health_check_interval{60};
        int max_failed_health_checks = 3;

        bool is_valid() const {
            return initial_delay > std::chrono::seconds{0} && max_delay >= initial_delay &&
                   backoff_multiplier > 1.0 && max_consecutive_failures > 0 &&
                   failure_cooldown > std::chrono::seconds{0} &&
                   health_check_interval > std::chrono::seconds{0} && max_failed_health_checks > 0;
        }
    };

    struct Config {
        std::string client_id;
        std::chrono::seconds update_interval{15};
        RateLimitConfig rate_limit_config;
        ConnectionConfig connection_config;
        bool enable_rate_limiting = true;
        bool enable_health_checks = true;

        bool is_valid() const {
            return !client_id.empty() && update_interval > std::chrono::seconds{0} &&
                   rate_limit_config.is_valid() && connection_config.is_valid();
        }
    };

    struct RetryStats {
        int consecutive_failures = 0;
        std::chrono::seconds current_delay{0};
        int total_reconnections = 0;
        std::chrono::system_clock::time_point last_success;
        std::chrono::system_clock::time_point last_failure;
    };

    struct ServiceStats {
        size_t total_presence_updates = 0;
        size_t failed_presence_updates = 0;
        size_t rate_limited_updates = 0;
        RetryStats connection_stats;
        std::chrono::system_clock::time_point last_successful_update;
        std::chrono::system_clock::time_point service_start_time;
    };

    explicit Discord(Config config);
    ~Discord();

    Discord(const Discord&) = delete;
    Discord& operator=(const Discord&) = delete;

    // Lifecycle
    std::expected<void, core::DiscordError> initialize();
    void shutdown();
    bool is_connected() const;

    // Presence management
    std::expected<void, core::DiscordError> update_presence(const PresenceData& data);
    std::expected<void, core::DiscordError> clear_presence();
    std::expected<void, core::DiscordError> update_from_media(const core::MediaInfo& media);

    void set_event_bus(std::shared_ptr<core::EventBus> bus);

    void set_update_interval(std::chrono::seconds interval);
    std::chrono::seconds get_update_interval() const;

    // Formatting options (delegated to PresenceBuilder)
    void set_show_progress(bool show);
    void set_show_buttons(bool show);
    void set_show_artwork(bool show);
    bool is_progress_shown() const;
    bool are_buttons_shown() const;
    bool is_artwork_shown() const;

    // Format templates (delegated to PresenceBuilder)
    void set_tv_details_format(const std::string& format);
    void set_tv_state_format(const std::string& format);
    void set_tv_large_image_text_format(const std::string& format);
    void set_movie_details_format(const std::string& format);
    void set_movie_state_format(const std::string& format);
    void set_movie_large_image_text_format(const std::string& format);
    void set_music_details_format(const std::string& format);
    void set_music_state_format(const std::string& format);
    void set_music_large_image_text_format(const std::string& format);

    ServiceStats get_service_stats() const;
    void force_reconnect();
    const Config& get_config() const { return m_config; }
    void update_config(const Config& config);

    static std::expected<std::unique_ptr<Discord>, core::ConfigError>
    create(const core::ApplicationConfig& app_config);

protected:
    void on_presence_updated(const PresenceData& data);
    void on_connection_state_changed(bool connected);
    void on_error_occurred(core::DiscordError error, const std::string& message);

    void publish_presence_updated(const PresenceData& data);
    void publish_presence_cleared(const std::string& reason = "");
    void publish_discord_connected(const std::string& app_id);
    void publish_discord_disconnected(const std::string& reason, bool will_retry);
    void publish_discord_error(core::DiscordError error, const std::string& message);

private:
    Config m_config;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_shutting_down{false};

    std::shared_ptr<core::EventBus> m_event_bus;

    // Presence formatting
    PresenceBuilder m_presence_builder;

    // IPC
    std::unique_ptr<DiscordIPC> m_ipc;
    std::optional<nlohmann::json> m_pending_frame;

    // Rate limiting state
    mutable std::deque<std::chrono::steady_clock::time_point> m_operation_times;
    std::chrono::steady_clock::time_point m_last_operation;
    mutable std::mutex m_rate_limit_mutex;

    // Connection state
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_force_reconnect_flag{false};
    mutable std::mutex m_connection_mutex;
    RetryStats m_retry_stats;

    // Threading
    std::jthread m_update_thread;
    std::thread m_connection_thread;
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

    mutable ServiceStats m_stats;

    // Update loop
    void update_loop();
    void process_pending_frame();
    bool send_presence_frame(const nlohmann::json& frame);

    void increment_stat_counter(size_t ServiceStats::*counter) const;
    void record_successful_update();
    void record_failed_update();

    // Rate limiting
    bool rate_limit_can_proceed();
    void rate_limit_record_operation();
    void rate_limit_cleanup_expired() const;
    bool rate_limit_check_minimum_interval() const;
    bool rate_limit_check_primary_window() const;
    bool rate_limit_check_burst_window() const;

    // Connection management
    void connection_loop();
    bool attempt_connection();
    void handle_connection_success(bool is_reconnect = true);
    void handle_connection_failure();
    bool perform_health_check();
    std::chrono::seconds calculate_next_delay();
    void reset_retry_state();
    bool should_attempt_reconnection();
};

} // namespace presence_for_plex::services
