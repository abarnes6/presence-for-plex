#include "presence_for_plex/services/discord/discord_presence_service.hpp"
#include "presence_for_plex/core/events_impl.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/format_utils.hpp"
#include <cassert>
#include <chrono>
#include <thread>
#include <expected>
#include <sstream>
#include <numeric>
#include <algorithm>
#include <iomanip>

namespace presence_for_plex::services {

using json = nlohmann::json;

// Event publishing helper implementations
void DiscordPresenceService::publish_presence_updated(const PresenceData& data) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::PresenceUpdated{data});
    }
}

void DiscordPresenceService::publish_presence_cleared(const std::string& reason) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::PresenceCleared{reason});
    }
}

void DiscordPresenceService::publish_discord_connected(const std::string& app_id) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::DiscordConnected{app_id});
    }
}

void DiscordPresenceService::publish_discord_disconnected(const std::string& reason, bool will_retry) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::DiscordDisconnected{reason, will_retry});
    }
}

void DiscordPresenceService::publish_discord_error(core::DiscordError error, const std::string& message) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::DiscordErrorEvent{error, message});
    }
}

DiscordPresenceService::DiscordPresenceService(Config config)
    : m_config(std::move(config)) {

    assert(m_config.is_valid() && "Configuration should be validated by factory before construction");

    // Initialize rate limiter
    m_rate_limiter = std::make_unique<DiscordRateLimiter>(m_config.rate_limit_config);

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

    LOG_INFO("DiscordPresenceService", "Discord client ID: " + m_config.client_id);
}

DiscordPresenceService::~DiscordPresenceService() {
    shutdown();
}

std::expected<void, core::DiscordError> DiscordPresenceService::initialize() {
    if (m_initialized.exchange(true)) {
        LOG_WARNING("DiscordPresenceService", "Already initialized");
        return {};
    }

    LOG_DEBUG("DiscordPresenceService", "Initializing Discord presence service");

    // Start connection manager
    if (!m_connection_manager->start()) {
        LOG_ERROR("DiscordPresenceService", "Failed to start connection manager");
        m_initialized = false;
        return std::unexpected<core::DiscordError>(core::DiscordError::IpcError);
    }

    // Start update thread
    m_update_thread = std::thread([this]() { update_loop(); });

    LOG_DEBUG("DiscordPresenceService", "Discord presence service initialized");
    return {};
}

void DiscordPresenceService::shutdown() {
    if (m_shutting_down.exchange(true)) {
        return;
    }

    LOG_INFO("DiscordPresenceService", "Shutting down Discord presence service");

    m_initialized = false;

    // Stop connection manager first to prevent new operations
    if (m_connection_manager) {
        m_connection_manager->stop();
    }

    // Signal update thread to exit
    m_shutdown_cv.notify_all();
    m_update_cv.notify_all();

    // Wait for update thread to finish
    if (m_update_thread.joinable()) {
        m_update_thread.join();
    }

    // Clear any pending frame
    {
        std::lock_guard lock(m_pending_mutex);
        m_pending_frame.reset();
    }

    LOG_INFO("DiscordPresenceService", "Discord presence service shut down");
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

    bool state_changed = false;
    {
        std::lock_guard<std::mutex> lock(m_presence_mutex);
        state_changed = (m_current_presence != data);
        if (state_changed) {
            m_current_presence = data;
            m_update_requested = true;
        }
    }

    if (state_changed) {
        m_update_cv.notify_one();
        LOG_DEBUG("DiscordPresenceService", "Presence update requested (state changed)");
    } else {
        LOG_DEBUG("DiscordPresenceService", "Presence update skipped (no state change)");
    }

    return {};
}

std::expected<void, core::DiscordError> DiscordPresenceService::clear_presence() {
    if (!m_initialized || m_shutting_down) {
        return std::unexpected<core::DiscordError>(core::DiscordError::ServiceUnavailable);
    }

    bool state_changed = false;
    {
        std::lock_guard<std::mutex> lock(m_presence_mutex);
        PresenceData empty_presence{};
        state_changed = (m_current_presence != empty_presence);
        if (state_changed) {
            m_current_presence = empty_presence;
            m_update_requested = true;
        }
    }

    if (state_changed) {
        m_update_cv.notify_one();
        LOG_DEBUG("DiscordPresenceService", "Presence clear requested");
    } else {
        LOG_DEBUG("DiscordPresenceService", "Presence clear skipped (already cleared)");
    }

    return {};
}

std::expected<void, core::DiscordError> DiscordPresenceService::update_from_media(const core::MediaInfo& media) {
    // Clear presence when playback is stopped
    if (media.state == core::PlaybackState::Stopped) {
        LOG_DEBUG("DiscordPresenceService", "Playback stopped, clearing presence");
        return clear_presence();
    }

    PresenceData presence = format_media(media);
    return update_presence(presence);
}

void DiscordPresenceService::set_event_bus(std::shared_ptr<core::EventBus> bus) {
    m_event_bus = bus;

    if (m_event_bus) {
        m_event_bus->subscribe<core::events::ConfigurationUpdated>(
            [this](const core::events::ConfigurationUpdated& event) {
                set_show_buttons(event.new_config.presence.discord.show_buttons);
                set_show_progress(event.new_config.presence.discord.show_progress);
                set_show_artwork(event.new_config.presence.discord.show_artwork);

                // Update format templates
                set_tv_details_format(event.new_config.presence.discord.tv_details_format);
                set_tv_state_format(event.new_config.presence.discord.tv_state_format);
                set_tv_large_image_text_format(event.new_config.presence.discord.tv_large_image_text_format);
                set_movie_details_format(event.new_config.presence.discord.movie_details_format);
                set_movie_state_format(event.new_config.presence.discord.movie_state_format);
                set_movie_large_image_text_format(event.new_config.presence.discord.movie_large_image_text_format);
                set_music_details_format(event.new_config.presence.discord.music_details_format);
                set_music_state_format(event.new_config.presence.discord.music_state_format);
                set_music_large_image_text_format(event.new_config.presence.discord.music_large_image_text_format);

                LOG_INFO("DiscordPresenceService", "Configuration updated from event");
            }
        );
    }
}

void DiscordPresenceService::set_update_interval(std::chrono::seconds interval) {
    if (interval > std::chrono::seconds{0}) {
        m_config.update_interval = interval;
        LOG_DEBUG("DiscordPresenceService",
            "Update interval changed to " + std::to_string(interval.count()) + "s");
    }
}

std::chrono::seconds DiscordPresenceService::get_update_interval() const {
    return m_config.update_interval;
}

void DiscordPresenceService::set_show_progress(bool show) {
    m_show_progress = show;
}

void DiscordPresenceService::set_show_buttons(bool show) {
    m_show_buttons = show;
}

void DiscordPresenceService::set_show_artwork(bool show) {
    m_show_artwork = show;
}

bool DiscordPresenceService::is_progress_shown() const {
    return m_show_progress;
}

bool DiscordPresenceService::are_buttons_shown() const {
    return m_show_buttons;
}

bool DiscordPresenceService::is_artwork_shown() const {
    return m_show_artwork;
}

void DiscordPresenceService::set_tv_details_format(const std::string& format) {
    m_tv_details_format = format;
}

void DiscordPresenceService::set_tv_state_format(const std::string& format) {
    m_tv_state_format = format;
}

void DiscordPresenceService::set_tv_large_image_text_format(const std::string& format) {
    m_tv_large_image_text_format = format;
}

void DiscordPresenceService::set_movie_details_format(const std::string& format) {
    m_movie_details_format = format;
}

void DiscordPresenceService::set_movie_state_format(const std::string& format) {
    m_movie_state_format = format;
}

void DiscordPresenceService::set_movie_large_image_text_format(const std::string& format) {
    m_movie_large_image_text_format = format;
}

void DiscordPresenceService::set_music_details_format(const std::string& format) {
    m_music_details_format = format;
}

void DiscordPresenceService::set_music_state_format(const std::string& format) {
    m_music_state_format = format;
}

void DiscordPresenceService::set_music_large_image_text_format(const std::string& format) {
    m_music_large_image_text_format = format;
}

void DiscordPresenceService::update_config(const Config& config) {
    LOG_INFO("DiscordPresenceService", "Updating configuration");

    // Update interval can be changed on the fly
    if (config.update_interval != m_config.update_interval) {
        m_config.update_interval = config.update_interval;
        LOG_INFO("DiscordPresenceService",
            "Update interval changed to " + std::to_string(config.update_interval.count()) + "s");
    }

    // Client ID change requires reconnection
    if (config.client_id != m_config.client_id) {
        LOG_INFO("DiscordPresenceService",
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
        LOG_INFO("DiscordPresenceService", "Forcing reconnection");
        m_connection_manager->force_reconnect();
    }
}



void DiscordPresenceService::update_loop() {
    LOG_DEBUG("DiscordPresenceService", "Update loop started");

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
                            {
                                std::lock_guard lock(m_presence_mutex);
                                m_last_sent_presence = current_presence;
                            }
                            on_presence_updated(current_presence);
                        } else {
                            record_failed_update();
                            std::lock_guard lock(m_pending_mutex);
                            m_pending_frame = std::move(activity);
                        }
                    } else {
                        // Rate limited, store as pending (replaces any old pending frame)
                        std::lock_guard lock(m_pending_mutex);
                        m_pending_frame = std::move(activity);
                        increment_stat_counter(&ServiceStats::rate_limited_updates);
                    }
                } else {
                    // Not connected, store as pending
                    std::lock_guard lock(m_pending_mutex);
                    m_pending_frame = std::move(activity);
                }
            }

            // Process pending frame
            process_pending_frame();

            // Wait for update request or timeout
            std::unique_lock lock(m_shutdown_mutex);
            m_update_cv.wait_for(lock, std::chrono::seconds(1),
                [this] { return m_shutting_down.load() || m_update_requested.load(); });

        } catch (const std::exception& e) {
            LOG_ERROR("DiscordPresenceService",
                "Exception in update loop: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    LOG_DEBUG("DiscordPresenceService", "Update loop terminated");
}

void DiscordPresenceService::process_pending_frame() {
    if (!is_connected()) {
        return;
    }

    if (!m_rate_limiter || !m_rate_limiter->can_proceed()) {
        return;
    }

    std::optional<json> frame;
    {
        std::lock_guard lock(m_pending_mutex);
        if (!m_pending_frame) {
            return;
        }
        frame = std::move(m_pending_frame);
        m_pending_frame.reset();
    }

    if (send_presence_frame(*frame)) {
        m_rate_limiter->record_operation();
        record_successful_update();
    } else {
        record_failed_update();
        // Put it back as pending for retry
        std::lock_guard lock(m_pending_mutex);
        m_pending_frame = std::move(frame);
    }
}

bool DiscordPresenceService::send_presence_frame(const json& frame) {
    if (!m_connection_manager) {
        return false;
    }

    auto* ipc = m_connection_manager->get_ipc();
    if (!ipc) {
        LOG_WARNING("DiscordPresenceService", "Cannot access Discord IPC");
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
            LOG_DEBUG("DiscordPresenceService", "Successfully sent presence frame");
        } else {
            increment_stat_counter(&ServiceStats::failed_presence_updates);
            LOG_WARNING("DiscordPresenceService", "Failed to send presence frame");
        }

        return success;
    } catch (const std::exception& e) {
        increment_stat_counter(&ServiceStats::failed_presence_updates);
        LOG_ERROR("DiscordPresenceService",
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
    activity["status_display_type"] = 2;  // Display type for watching activity
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
        LOG_DEBUG("DiscordPresenceService", "Connection established, will process pending frame");
        m_update_cv.notify_one();
    }
}

void DiscordPresenceService::handle_health_check_result(bool healthy) {
    if (!healthy) {
        LOG_WARNING("DiscordPresenceService", "Health check failed");
        on_error_occurred(core::DiscordError::IpcError, "Discord health check failed");
    }
}

void DiscordPresenceService::on_presence_updated(const PresenceData& data) {
    if (m_event_bus) {
        publish_presence_updated(data);
    }
}

void DiscordPresenceService::on_connection_state_changed(bool connected) {
    if (m_event_bus) {
        if (connected) {
            publish_discord_connected(m_config.client_id);
        } else {
            publish_discord_disconnected("Connection lost", false);
        }
    }
}

void DiscordPresenceService::on_error_occurred(core::DiscordError error, const std::string& message) {
    if (m_event_bus) {
        publish_discord_error(error, message);
    }
}

std::expected<std::unique_ptr<DiscordPresenceService>, core::ConfigError>
DiscordPresenceService::create(const core::ApplicationConfig& app_config) {
    if (app_config.presence.discord.client_id.empty()) {
        return std::unexpected(core::ConfigError::ValidationError);
    }

    Config config;
    config.client_id = app_config.presence.discord.client_id;
    config.update_interval = app_config.presence.discord.update_interval;

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
        LOG_ERROR("DiscordPresenceService",
            "Failed to create service: " + std::string(e.what()));
        return std::unexpected(core::ConfigError::InvalidFormat);
    }
}


PresenceData DiscordPresenceService::format_media(const core::MediaInfo& media) const {
    PresenceData data;

    // Default large image
    data.large_image_key = "plex_logo";
    if (m_show_artwork && !media.art_path.empty()) {
        data.large_image_key = media.art_path;
        LOG_DEBUG("DiscordPresenceService", "Using art_path for large_image: " + media.art_path);
    } else if (!media.art_path.empty()) {
        LOG_DEBUG("DiscordPresenceService", "Artwork disabled, using default plex_logo");
    } else {
        LOG_DEBUG("DiscordPresenceService", "art_path is empty, using default plex_logo");
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
    data.details = utils::replace_placeholders(details_format, media);
    data.state = utils::replace_placeholders(state_format, media);

    // Large image text
    if (!large_image_text_format.empty()) {
        data.large_image_text = utils::replace_placeholders(large_image_text_format, media);
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

} // namespace presence_for_plex::services