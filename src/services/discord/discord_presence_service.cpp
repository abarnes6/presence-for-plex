#include "presence_for_plex/services/discord_presence_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <chrono>
#include <thread>
#include <expected>

namespace presence_for_plex::services {

using json = nlohmann::json;

DiscordPresenceService::DiscordPresenceService(Config config)
    : m_config(std::move(config)) {

    if (!m_config.is_valid()) {
        throw std::invalid_argument("Invalid configuration provided to DiscordPresenceService");
    }

    // Initialize components
    if (m_config.enable_rate_limiting) {
        m_rate_limiter = RateLimiterFactory::create_discord_limiter(m_config.rate_limit_config);
    } else {
        m_rate_limiter = RateLimiterFactory::create_no_op_limiter();
    }

    if (m_config.enable_frame_queuing) {
        m_frame_queue = std::make_unique<FrameQueue<json>>(m_config.queue_config);
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
    if (m_frame_queue) {
        m_frame_queue->clear();
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

void DiscordPresenceService::set_update_callback(PresenceUpdateCallback callback) {
    m_update_callback = std::move(callback);
}

void DiscordPresenceService::set_error_callback(PresenceErrorCallback callback) {
    m_error_callback = std::move(callback);
}

void DiscordPresenceService::set_connection_callback(PresenceConnectionStateCallback callback) {
    m_connection_callback = std::move(callback);
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

DiscordPresenceService::ServiceStats DiscordPresenceService::get_service_stats() const {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    ServiceStats stats = m_stats;

    if (m_connection_manager) {
        stats.connection_stats = m_connection_manager->get_retry_stats();
    }

    if (m_frame_queue) {
        stats.queue_stats = m_frame_queue->get_stats();
        stats.queued_frames = m_frame_queue->size();
    }

    return stats;
}

void DiscordPresenceService::force_reconnect() {
    if (m_connection_manager) {
        PLEX_LOG_INFO("DiscordPresenceService", "Forcing reconnection");
        m_connection_manager->force_reconnect();
    }
}

void DiscordPresenceService::on_presence_updated(const PresenceData& data) {
    record_successful_update();

    if (m_update_callback) {
        try {
            m_update_callback(data);
        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("DiscordPresenceService",
                "Exception in update callback: " + std::string(e.what()));
        }
    }
}

void DiscordPresenceService::on_connection_state_changed(bool connected) {
    PLEX_LOG_INFO("DiscordPresenceService",
        "Connection state changed: " + std::string(connected ? "CONNECTED" : "DISCONNECTED"));

    if (m_connection_callback) {
        try {
            m_connection_callback(connected);
        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("DiscordPresenceService",
                "Exception in connection callback: " + std::string(e.what()));
        }
    }
}

void DiscordPresenceService::on_error_occurred(core::DiscordError error, const std::string& message) {
    PLEX_LOG_ERROR("DiscordPresenceService", message);

    if (m_error_callback) {
        try {
            m_error_callback(error, message);
        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("DiscordPresenceService",
                "Exception in error callback: " + std::string(e.what()));
        }
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
                            // Queue for retry if queuing is enabled
                            if (m_frame_queue) {
                                m_frame_queue->enqueue(std::move(activity), 1); // Normal priority
                            }
                        }
                    } else {
                        // Rate limited, queue the frame
                        if (m_frame_queue) {
                            m_frame_queue->enqueue(std::move(activity), 1);
                            increment_stat_counter(&ServiceStats::rate_limited_updates);
                        }
                    }
                } else {
                    // Not connected, queue the frame
                    if (m_frame_queue) {
                        m_frame_queue->enqueue(std::move(activity), 1);
                    }
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
    if (!m_frame_queue || !is_connected()) {
        return;
    }

    // Process frames while rate limits allow
    while (m_rate_limiter && m_rate_limiter->can_proceed()) {
        auto frame = m_frame_queue->dequeue();
        if (!frame) {
            break; // No more frames
        }

        if (send_presence_frame(frame->data)) {
            m_rate_limiter->record_operation();
            // For queued frames, we can't easily map back to PresenceData,
            // so we'll just record the successful update
            record_successful_update();
        } else {
            record_failed_update();
            // Re-queue with lower priority for retry, preventing underflow
            size_t retry_priority = frame->priority == 0 ? 0 : frame->priority - 1;
            m_frame_queue->enqueue(std::move(frame->data), retry_priority);
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
        if (m_frame_queue && !m_frame_queue->empty()) {
            // Trigger immediate processing by requesting an update
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

// Factory implementations
std::unique_ptr<PresenceService> DiscordPresenceServiceFactory::create_service(
    const core::ApplicationConfig& app_config) {

    auto config = create_config_from_app_config(app_config);
    return create_discord_service(std::move(config));
}

std::unique_ptr<DiscordPresenceService> DiscordPresenceServiceFactory::create_discord_service(
    DiscordPresenceService::Config config) {

    return std::make_unique<DiscordPresenceService>(std::move(config));
}

DiscordPresenceService::Config DiscordPresenceServiceFactory::create_config_from_app_config(
    const core::ApplicationConfig& app_config) {

    DiscordPresenceService::Config config;

    // Discord configuration
    config.client_id = app_config.discord.application_id;
    config.update_interval = app_config.discord.update_interval;

    // Rate limiting configuration (use safe defaults)
    config.rate_limit_config = DiscordRateLimitConfig{};

    // Connection configuration (use conservative defaults)
    config.connection_config = ConnectionRetryConfig{};

    // Frame queue configuration
    config.queue_config = FrameQueueConfig{};

    // Enable all features by default
    config.enable_rate_limiting = true;
    config.enable_frame_queuing = true;
    config.enable_health_checks = true;

    return config;
}

} // namespace presence_for_plex::services