#pragma once

#include "presence_for_plex/services/presence_service.hpp"
#include "presence_for_plex/services/rate_limiter.hpp"
#include "presence_for_plex/services/connection_manager.hpp"
#include "presence_for_plex/services/discord_ipc.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <expected>
#include <condition_variable>

namespace presence_for_plex::services {

class DiscordPresenceService : public PresenceService {
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

    ~DiscordPresenceService() override;

    // PresenceService implementation
    std::expected<void, core::DiscordError> initialize() override;
    void shutdown() override;
    bool is_connected() const override;

    std::expected<void, core::DiscordError> update_presence(const PresenceData& data) override;
    std::expected<void, core::DiscordError> clear_presence() override;
    std::expected<void, core::DiscordError> update_from_media(const core::MediaInfo& media) override;

    void set_event_bus(std::shared_ptr<core::EventBus> bus) override;

    void set_update_interval(std::chrono::seconds interval) override;
    std::chrono::seconds get_update_interval() const override;

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

    /**
     * @brief Get the presence formatter for configuration
     */
    PresenceFormatter* get_formatter() override { return m_formatter.get(); }

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

    // Core components
    std::unique_ptr<IRateLimiter> m_rate_limiter;
    std::unique_ptr<ConnectionManager> m_connection_manager;
    std::queue<nlohmann::json> m_frame_queue;
    std::unique_ptr<PresenceFormatter> m_formatter;

    // Threading
    std::jthread m_update_thread;
    std::atomic<bool> m_update_requested{false};

    // Synchronization
    mutable std::mutex m_stats_mutex;
    mutable std::mutex m_presence_mutex;
    mutable std::mutex m_queue_mutex;
    std::mutex m_shutdown_mutex;
    std::condition_variable m_shutdown_cv;
    PresenceData m_current_presence;

    // Statistics
    mutable ServiceStats m_stats;

    // Internal methods
    void update_loop();
    void process_pending_frames();
    bool send_presence_frame(const nlohmann::json& frame);
    nlohmann::json create_discord_activity(const PresenceData& data) const;

    void increment_stat_counter(size_t ServiceStats::*counter) const;
    void record_successful_update();
    void record_failed_update();

    // Connection event handlers
    void handle_connection_changed(bool connected);
    void handle_health_check_result(bool healthy);
};

} // namespace presence_for_plex::services