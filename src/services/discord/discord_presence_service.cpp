#include "presence_for_plex/services/discord_presence_service.hpp"
#include "presence_for_plex/services/presence_service.hpp"
#include "presence_for_plex/core/events_impl.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <cassert>
#include <chrono>
#include <thread>
#include <expected>

namespace presence_for_plex::services {

using json = nlohmann::json;

DiscordPresenceService::DiscordPresenceService(Config config)
    : m_config(std::move(config)) {

    assert(m_config.is_valid() && "Configuration should be validated by factory before construction");

    // Initialize components
    if (m_config.enable_rate_limiting) {
        m_rate_limiter = std::make_unique<DiscordRateLimiter>(m_config.rate_limit_config);
    } else {
        m_rate_limiter = std::make_unique<NoOpRateLimiter>();
    }

    m_formatter = PresenceFormatter::create_default_formatter();

    // Create Discord IPC and connection manager
    auto discord_ipc = std::make_unique<DiscordIPC>(m_config.client_id);
    m_connection_manager = std::make_unique<ConnectionManager>(
        std::move(discord_ipc), m_config.connection_config);

    // Set up connection callbacks
    m_connection_manager->set_connection_callback(
        [this](bool connected) { handle_connection_changed(connected); });

    if (m_config.enable_health_checks) {
        m_connection_manager->set_health_check_callback(
            [this](bool healthy) { handle_health_check_result(healthy); });
    }

    m_stats.service_start_time = std::chrono::system_clock::now();

    PLEX_LOG_INFO("DiscordPresenceService",
        "Initialized with client ID: " + m_config.client_id);
}

DiscordPresenceService::~DiscordPresenceService() {
    shutdown();
}

std::expected<void, core::DiscordError> DiscordPresenceService::initialize() {
    if (m_initialized.exchange(true)) {
        PLEX_LOG_WARNING("DiscordPresenceService", "Already initialized");
        return {};
    }

    PLEX_LOG_INFO("DiscordPresenceService", "Initializing Discord presence service");

    // Start connection manager
    if (!m_connection_manager->start()) {
        PLEX_LOG_ERROR("DiscordPresenceService", "Failed to start connection manager");
        m_initialized = false;
        return std::unexpected<core::DiscordError>(core::DiscordError::IpcError);
    }

    // Start update thread
    m_update_thread = std::thread([this] { update_loop(); });

    PLEX_LOG_INFO("DiscordPresenceService", "Discord presence service initialized");
    return {};
}

void DiscordPresenceService::shutdown() {
    if (m_shutting_down.exchange(true)) {
        return;
    }

    PLEX_LOG_INFO("DiscordPresenceService", "Shutting down Discord presence service");

    m_initialized = false;

    // Stop update thread
    if (m_update_thread.joinable()) {
        m_update_thread.join();
    }

    // Stop connection manager
    if (m_connection_manager) {
        m_connection_manager->stop();
    }

    // Clear any queued frames
    {
        std::lock_guard lock(m_queue_mutex);
        while (!m_frame_queue.empty()) {
            m_frame_queue.pop();
        }
    }

    PLEX_LOG_INFO("DiscordPresenceService", "Discord presence service shut down");
}

bool DiscordPresenceService::is_connected() const {
    return m_connection_manager && m_connection_manager->is_connected();
}

std::expected<void, core::DiscordError> DiscordPresenceService::update_presence(const PresenceData& data) {
    if (!m_initialized || m_shutting_down) {
        return std::unexpected<core::DiscordError>(core::DiscordError::ServiceUnavailable);
    }

    if (!data.is_valid()) {
        return std::unexpected<core::DiscordError>(core::DiscordError::InvalidPayload);
    }

    {
        std::lock_guard<std::mutex> lock(m_presence_mutex);
        m_current_presence = data;
        m_update_requested = true;
    }

    PLEX_LOG_DEBUG("DiscordPresenceService", "Presence update requested");
    return {};
}

std::expected<void, core::DiscordError> DiscordPresenceService::clear_presence() {
    if (!m_initialized || m_shutting_down) {
        return std::unexpected<core::DiscordError>(core::DiscordError::ServiceUnavailable);
    }

    {
        std::lock_guard<std::mutex> lock(m_presence_mutex);
        m_current_presence = {}; // Empty presence
        m_update_requested = true;
    }

    PLEX_LOG_DEBUG("DiscordPresenceService", "Presence clear requested");
    return {};
}

std::expected<void, core::DiscordError> DiscordPresenceService::update_from_media(const core::MediaInfo& media) {
    if (!m_formatter) {
        return std::unexpected<core::DiscordError>(core::DiscordError::ServiceUnavailable);
    }

    PresenceData presence = m_formatter->format_media(media);
    return update_presence(presence);
}

void DiscordPresenceService::set_event_bus(std::shared_ptr<core::EventBus> bus) {
    m_event_bus = bus;

    if (m_event_bus) {
        m_event_bus->subscribe<core::events::ConfigurationUpdated>(
            [this](const core::events::ConfigurationUpdated& event) {
                if (m_formatter) {
                    m_formatter->set_show_buttons(event.new_config.discord.show_buttons);
                    m_formatter->set_show_progress(event.new_config.discord.show_progress);
                }

                if (event.new_config.discord.update_interval != m_config.update_interval) {
                    set_update_interval(event.new_config.discord.update_interval);
                }

                PLEX_LOG_INFO("DiscordPresenceService", "Configuration updated from event");
            }
        );
    }
}

void DiscordPresenceService::set_update_interval(std::chrono::seconds interval) {
    if (interval > std::chrono::seconds{0}) {
        m_config.update_interval = interval;
        PLEX_LOG_DEBUG("DiscordPresenceService",
            "Update interval changed to " + std::to_string(interval.count()) + "s");
    }
}

std::chrono::seconds DiscordPresenceService::get_update_interval() const {
    return m_config.update_interval;
}

void DiscordPresenceService::update_config(const Config& config) {
    PLEX_LOG_INFO("DiscordPresenceService", "Updating configuration");

    // Update interval can be changed on the fly
    if (config.update_interval != m_config.update_interval) {
        m_config.update_interval = config.update_interval;
        PLEX_LOG_INFO("DiscordPresenceService",
            "Update interval changed to " + std::to_string(config.update_interval.count()) + "s");
    }

    // Client ID change requires reconnection
    if (config.client_id != m_config.client_id) {
        PLEX_LOG_INFO("DiscordPresenceService",
            "Client ID changed from " + m_config.client_id + " to " + config.client_id);
        m_config.client_id = config.client_id;

        // Reconnect with new client ID
        if (m_connection_manager) {
            m_connection_manager->stop();
            auto discord_ipc = std::make_unique<DiscordIPC>(m_config.client_id);
            m_connection_manager = std::make_unique<ConnectionManager>(
                std::move(discord_ipc), m_config.connection_config);
            m_connection_manager->set_connection_callback(
                [this](bool connected) { handle_connection_changed(connected); });
            m_connection_manager->start();
        }
    }
}

DiscordPresenceService::ServiceStats DiscordPresenceService::get_service_stats() const {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    ServiceStats stats = m_stats;

    if (m_connection_manager) {
        stats.connection_stats = m_connection_manager->get_retry_stats();
    }

    return stats;
}

void DiscordPresenceService::force_reconnect() {
    if (m_connection_manager) {
        PLEX_LOG_INFO("DiscordPresenceService", "Forcing reconnection");
        m_connection_manager->force_reconnect();
    }
}



void DiscordPresenceService::update_loop() {
    PLEX_LOG_DEBUG("DiscordPresenceService", "Update loop started");

    while (!m_shutting_down) {
        try {
            // Check for presence updates
            if (m_update_requested.exchange(false)) {
                PresenceData current_presence;
                {
                    std::lock_guard<std::mutex> lock(m_presence_mutex);
                    current_presence = m_current_presence;
                }

                json activity = create_discord_activity(current_presence);

                if (is_connected()) {
                    // Try to send immediately if rate limits allow
                    if (m_rate_limiter && m_rate_limiter->can_proceed()) {
                        if (send_presence_frame(activity)) {
                            m_rate_limiter->record_operation();
                            on_presence_updated(current_presence);
                        } else {
                            record_failed_update();
                            std::lock_guard lock(m_queue_mutex);
                            m_frame_queue.push(std::move(activity));
                        }
                    } else {
                        // Rate limited, queue the frame
                        std::lock_guard lock(m_queue_mutex);
                        m_frame_queue.push(std::move(activity));
                        increment_stat_counter(&ServiceStats::rate_limited_updates);
                    }
                } else {
                    // Not connected, queue the frame
                    std::lock_guard lock(m_queue_mutex);
                    m_frame_queue.push(std::move(activity));
                }
            }

            // Process queued frames
            process_pending_frames();

            // Sleep until next update cycle
            std::this_thread::sleep_for(m_config.update_interval);

        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("DiscordPresenceService",
                "Exception in update loop: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    PLEX_LOG_DEBUG("DiscordPresenceService", "Update loop terminated");
}

void DiscordPresenceService::process_pending_frames() {
    if (!is_connected()) {
        return;
    }

    // Process frames while rate limits allow
    while (m_rate_limiter && m_rate_limiter->can_proceed()) {
        json frame;
        {
            std::lock_guard lock(m_queue_mutex);
            if (m_frame_queue.empty()) {
                break; // No more frames
            }
            frame = std::move(m_frame_queue.front());
            m_frame_queue.pop();
        }

        if (send_presence_frame(frame)) {
            m_rate_limiter->record_operation();
            record_successful_update();
        } else {
            record_failed_update();
            // Re-queue for retry
            std::lock_guard lock(m_queue_mutex);
            m_frame_queue.push(std::move(frame));
            break; // Stop processing on failure
        }
    }
}

bool DiscordPresenceService::send_presence_frame(const json& frame) {
    if (!m_connection_manager) {
        return false;
    }

    auto* ipc = m_connection_manager->get_strategy<DiscordIPC>();
    if (!ipc) {
        PLEX_LOG_WARNING("DiscordPresenceService", "Cannot access Discord IPC");
        return false;
    }

    try {
        bool success = false;
        if (frame.empty() || frame.is_null()) {
            success = ipc->clear_presence();
        } else {
            success = ipc->send_presence(frame);
        }

        if (success) {
            increment_stat_counter(&ServiceStats::total_presence_updates);
            PLEX_LOG_DEBUG("DiscordPresenceService", "Successfully sent presence frame");
        } else {
            increment_stat_counter(&ServiceStats::failed_presence_updates);
            PLEX_LOG_WARNING("DiscordPresenceService", "Failed to send presence frame");
        }

        return success;
    } catch (const std::exception& e) {
        increment_stat_counter(&ServiceStats::failed_presence_updates);
        PLEX_LOG_ERROR("DiscordPresenceService",
            "Exception sending presence frame: " + std::string(e.what()));
        return false;
    }
}

json DiscordPresenceService::create_discord_activity(const PresenceData& data) const {
    json activity;

    if (data.details.empty() && data.state.empty() && data.large_image_key.empty()) {
        return activity; // Empty activity for clearing presence
    }

    // Use the activity type from PresenceData
    activity["type"] = data.activity_type;
    activity["status_display_type"] = 2;  // Match old implementation
    activity["instance"] = true;  // Required for rich presence

    if (!data.details.empty()) {
        activity["details"] = data.details;
    }

    if (!data.state.empty()) {
        activity["state"] = data.state;
    }

    // Assets
    if (!data.large_image_key.empty() || !data.small_image_key.empty()) {
        json assets;
        if (!data.large_image_key.empty()) {
            assets["large_image"] = data.large_image_key;
            if (!data.large_image_text.empty()) {
                assets["large_text"] = data.large_image_text;
            }
        }
        if (!data.small_image_key.empty()) {
            assets["small_image"] = data.small_image_key;
            if (!data.small_image_text.empty()) {
                assets["small_text"] = data.small_image_text;
            }
        }
        activity["assets"] = assets;
    }

    // Timestamps
    if (data.start_timestamp || data.end_timestamp) {
        json timestamps;
        if (data.start_timestamp) {
            auto epoch = data.start_timestamp->time_since_epoch();
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
            timestamps["start"] = seconds.count();
        }
        if (data.end_timestamp) {
            auto epoch = data.end_timestamp->time_since_epoch();
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
            timestamps["end"] = seconds.count();
        }
        activity["timestamps"] = timestamps;
    }

    // Buttons
    if (!data.buttons.empty()) {
        json buttons = json::array();
        for (const auto& button : data.buttons) {
            if (buttons.size() >= 2) break; // Discord limit
            buttons.push_back({
                {"label", button.label},
                {"url", button.url}
            });
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

void DiscordPresenceService::increment_stat_counter(size_t ServiceStats::*counter) const {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats.*counter += 1;
}

void DiscordPresenceService::record_successful_update() {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats.last_successful_update = std::chrono::system_clock::now();
}

void DiscordPresenceService::record_failed_update() {
    increment_stat_counter(&ServiceStats::failed_presence_updates);
}

void DiscordPresenceService::handle_connection_changed(bool connected) {
    on_connection_state_changed(connected);

    if (connected) {
        PLEX_LOG_INFO("DiscordPresenceService", "Connection established, processing queued frames");
        // Process any queued frames when we reconnect
        std::lock_guard lock(m_queue_mutex);
        if (!m_frame_queue.empty()) {
            m_update_requested = true;
        }
    }
}

void DiscordPresenceService::handle_health_check_result(bool healthy) {
    if (!healthy) {
        PLEX_LOG_WARNING("DiscordPresenceService", "Health check failed");
        on_error_occurred(core::DiscordError::IpcError, "Discord health check failed");
    }
}

void DiscordPresenceService::on_presence_updated(const PresenceData& data) {
    if (m_event_bus) {
        PresenceService::publish_presence_updated(data);
    }
}

void DiscordPresenceService::on_connection_state_changed(bool connected) {
    if (m_event_bus) {
        if (connected) {
            PresenceService::publish_discord_connected(m_config.client_id);
        } else {
            PresenceService::publish_discord_disconnected("Connection lost", false);
        }
    }
}

void DiscordPresenceService::on_error_occurred(core::DiscordError error, const std::string& message) {
    if (m_event_bus) {
        PresenceService::publish_discord_error(error, message);
    }
}

std::expected<std::unique_ptr<DiscordPresenceService>, core::ConfigError>
DiscordPresenceService::create(const core::ApplicationConfig& app_config) {
    if (app_config.discord.client_id.empty()) {
        return std::unexpected(core::ConfigError::ValidationError);
    }

    Config config;
    config.client_id = app_config.discord.client_id;
    config.update_interval = app_config.discord.update_interval;

    config.rate_limit_config = DiscordRateLimitConfig{};
    config.connection_config = ConnectionRetryConfig{};

    config.enable_rate_limiting = true;
    config.enable_health_checks = true;

    if (!config.is_valid()) {
        return std::unexpected(core::ConfigError::ValidationError);
    }

    try {
        return std::make_unique<DiscordPresenceService>(std::move(config));
    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("DiscordPresenceService",
            "Failed to create service: " + std::string(e.what()));
        return std::unexpected(core::ConfigError::InvalidFormat);
    }
}

} // namespace presence_for_plex::services