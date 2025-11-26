#include "presence_for_plex/services/discord/discord.hpp"
#include "presence_for_plex/core/events_impl.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <cassert>
#include <algorithm>

namespace presence_for_plex::services {

using json = nlohmann::json;

// Event publishing
void Discord::publish_presence_updated(const PresenceData& data) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::PresenceUpdated{data});
    }
}

void Discord::publish_presence_cleared(const std::string& reason) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::PresenceCleared{reason});
    }
}

void Discord::publish_discord_connected(const std::string& app_id) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::DiscordConnected{app_id});
    }
}

void Discord::publish_discord_disconnected(const std::string& reason, bool will_retry) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::DiscordDisconnected{reason, will_retry});
    }
}

void Discord::publish_discord_error(core::DiscordError error, const std::string& message) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::DiscordErrorEvent{error, message});
    }
}

Discord::Discord(Config config)
    : m_config(std::move(config)) {

    assert(m_config.is_valid() && "Configuration should be validated before construction");

    // Apply safety factor to rate limits
    m_config.rate_limit_config.max_operations_per_window = static_cast<int>(
        m_config.rate_limit_config.max_operations_per_window * m_config.rate_limit_config.safety_factor);
    m_config.rate_limit_config.max_burst_operations = static_cast<int>(
        m_config.rate_limit_config.max_burst_operations * m_config.rate_limit_config.safety_factor);

    m_ipc = std::make_unique<DiscordIPC>(m_config.client_id);
    m_stats.service_start_time = std::chrono::system_clock::now();

    LOG_INFO("Discord", "Discord client ID: " + m_config.client_id);
}

Discord::~Discord() {
    shutdown();
}

std::expected<void, core::DiscordError> Discord::initialize() {
    if (m_initialized.exchange(true)) {
        LOG_WARNING("Discord", "Already initialized");
        return {};
    }

    LOG_DEBUG("Discord", "Initializing Discord service");

    // Try initial connection
    bool initial_success = attempt_connection();
    if (initial_success) {
        handle_connection_success(false);
    }

    // Start connection management thread
    m_connection_thread = std::thread([this] { connection_loop(); });

    // Start update thread
    m_update_thread = std::jthread([this](std::stop_token) { update_loop(); });

    LOG_DEBUG("Discord", "Discord service initialized");
    return {};
}

void Discord::shutdown() {
    if (m_shutting_down.exchange(true)) {
        return;
    }

    LOG_INFO("Discord", "Shutting down Discord service");

    m_initialized = false;

    // Signal threads to exit
    m_shutdown_cv.notify_all();
    m_update_cv.notify_all();

    // Wait for connection thread
    if (m_connection_thread.joinable()) {
        m_connection_thread.join();
    }

    // Disconnect IPC
    if (m_ipc) {
        m_ipc->disconnect();
    }
    m_connected = false;

    // Clear pending frame
    {
        std::lock_guard lock(m_pending_mutex);
        m_pending_frame.reset();
    }

    LOG_INFO("Discord", "Discord service shut down");
}

bool Discord::is_connected() const {
    return m_connected && m_ipc && m_ipc->is_connected();
}

std::expected<void, core::DiscordError> Discord::update_presence(const PresenceData& data) {
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
        LOG_DEBUG("Discord", "Presence update requested (state changed)");
    }

    return {};
}

std::expected<void, core::DiscordError> Discord::clear_presence() {
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
        LOG_DEBUG("Discord", "Presence clear requested");
    }

    return {};
}

std::expected<void, core::DiscordError> Discord::update_from_media(const core::MediaInfo& media) {
    if (media.state == core::PlaybackState::Stopped) {
        LOG_DEBUG("Discord", "Playback stopped, clearing presence");
        return clear_presence();
    }

    PresenceData presence = m_presence_builder.from_media(media);
    return update_presence(presence);
}

void Discord::set_event_bus(std::shared_ptr<core::EventBus> bus) {
    m_event_bus = bus;

    if (m_event_bus) {
        m_event_bus->subscribe<core::events::ConfigurationUpdated>(
            [this](const core::events::ConfigurationUpdated& event) {
                set_show_buttons(event.new_config.discord.discord.show_buttons);
                set_show_progress(event.new_config.discord.discord.show_progress);
                set_show_artwork(event.new_config.discord.discord.show_artwork);

                set_tv_details_format(event.new_config.discord.discord.tv_details_format);
                set_tv_state_format(event.new_config.discord.discord.tv_state_format);
                set_tv_large_image_text_format(event.new_config.discord.discord.tv_large_image_text_format);
                set_movie_details_format(event.new_config.discord.discord.movie_details_format);
                set_movie_state_format(event.new_config.discord.discord.movie_state_format);
                set_movie_large_image_text_format(event.new_config.discord.discord.movie_large_image_text_format);
                set_music_details_format(event.new_config.discord.discord.music_details_format);
                set_music_state_format(event.new_config.discord.discord.music_state_format);
                set_music_large_image_text_format(event.new_config.discord.discord.music_large_image_text_format);

                LOG_INFO("Discord", "Configuration updated from event");
            }
        );
    }
}

void Discord::set_update_interval(std::chrono::seconds interval) {
    if (interval > std::chrono::seconds{0}) {
        m_config.update_interval = interval;
    }
}

std::chrono::seconds Discord::get_update_interval() const {
    return m_config.update_interval;
}

// Formatting options - delegated to PresenceBuilder
void Discord::set_show_progress(bool show) { m_presence_builder.set_show_progress(show); }
void Discord::set_show_buttons(bool show) { m_presence_builder.set_show_buttons(show); }
void Discord::set_show_artwork(bool show) { m_presence_builder.set_show_artwork(show); }
bool Discord::is_progress_shown() const { return m_presence_builder.is_progress_shown(); }
bool Discord::are_buttons_shown() const { return m_presence_builder.are_buttons_shown(); }
bool Discord::is_artwork_shown() const { return m_presence_builder.is_artwork_shown(); }

void Discord::set_tv_details_format(const std::string& format) { m_presence_builder.set_tv_details_format(format); }
void Discord::set_tv_state_format(const std::string& format) { m_presence_builder.set_tv_state_format(format); }
void Discord::set_tv_large_image_text_format(const std::string& format) { m_presence_builder.set_tv_large_image_text_format(format); }
void Discord::set_movie_details_format(const std::string& format) { m_presence_builder.set_movie_details_format(format); }
void Discord::set_movie_state_format(const std::string& format) { m_presence_builder.set_movie_state_format(format); }
void Discord::set_movie_large_image_text_format(const std::string& format) { m_presence_builder.set_movie_large_image_text_format(format); }
void Discord::set_music_details_format(const std::string& format) { m_presence_builder.set_music_details_format(format); }
void Discord::set_music_state_format(const std::string& format) { m_presence_builder.set_music_state_format(format); }
void Discord::set_music_large_image_text_format(const std::string& format) { m_presence_builder.set_music_large_image_text_format(format); }

void Discord::update_config(const Config& config) {
    LOG_INFO("Discord", "Updating configuration");

    if (config.update_interval != m_config.update_interval) {
        m_config.update_interval = config.update_interval;
    }

    if (config.client_id != m_config.client_id) {
        LOG_INFO("Discord", "Client ID changed, reconnecting");
        m_config.client_id = config.client_id;

        if (m_ipc) {
            m_ipc->disconnect();
        }
        m_ipc = std::make_unique<DiscordIPC>(m_config.client_id);
        m_connected = false;
        reset_retry_state();
    }
}

Discord::ServiceStats Discord::get_service_stats() const {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    ServiceStats stats = m_stats;

    std::lock_guard<std::mutex> conn_lock(m_connection_mutex);
    stats.connection_stats = m_retry_stats;

    return stats;
}

void Discord::force_reconnect() {
    LOG_INFO("Discord", "Forcing reconnection");
    m_force_reconnect_flag = true;
}

// Update loop
void Discord::update_loop() {
    LOG_DEBUG("Discord", "Update loop started");

    while (!m_shutting_down) {
        try {
            if (m_update_requested.exchange(false)) {
                PresenceData current_presence;
                {
                    std::lock_guard<std::mutex> lock(m_presence_mutex);
                    current_presence = m_current_presence;
                }

                json activity = PresenceBuilder::to_json(current_presence);

                if (is_connected()) {
                    if (rate_limit_can_proceed()) {
                        if (send_presence_frame(activity)) {
                            rate_limit_record_operation();
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
                        std::lock_guard lock(m_pending_mutex);
                        m_pending_frame = std::move(activity);
                        increment_stat_counter(&ServiceStats::rate_limited_updates);
                    }
                } else {
                    std::lock_guard lock(m_pending_mutex);
                    m_pending_frame = std::move(activity);
                }
            }

            process_pending_frame();

            std::unique_lock lock(m_shutdown_mutex);
            m_update_cv.wait_for(lock, std::chrono::seconds(1),
                [this] { return m_shutting_down.load() || m_update_requested.load(); });

        } catch (const std::exception& e) {
            LOG_ERROR("Discord", "Exception in update loop: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    LOG_DEBUG("Discord", "Update loop terminated");
}

void Discord::process_pending_frame() {
    if (!is_connected() || !rate_limit_can_proceed()) {
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
        rate_limit_record_operation();
        record_successful_update();
    } else {
        record_failed_update();
        std::lock_guard lock(m_pending_mutex);
        m_pending_frame = std::move(frame);
    }
}

bool Discord::send_presence_frame(const json& frame) {
    if (!m_ipc) {
        return false;
    }

    try {
        bool success = false;
        if (frame.empty() || frame.is_null()) {
            success = m_ipc->clear_presence();
        } else {
            success = m_ipc->send_presence(frame);
        }

        if (success) {
            increment_stat_counter(&ServiceStats::total_presence_updates);
            LOG_DEBUG("Discord", "Successfully sent presence frame");
        } else {
            increment_stat_counter(&ServiceStats::failed_presence_updates);
            LOG_WARNING("Discord", "Failed to send presence frame");
        }

        return success;
    } catch (const std::exception& e) {
        increment_stat_counter(&ServiceStats::failed_presence_updates);
        LOG_ERROR("Discord", "Exception sending presence frame: " + std::string(e.what()));
        return false;
    }
}

void Discord::increment_stat_counter(size_t ServiceStats::*counter) const {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats.*counter += 1;
}

void Discord::record_successful_update() {
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    m_stats.last_successful_update = std::chrono::system_clock::now();
}

void Discord::record_failed_update() {
    increment_stat_counter(&ServiceStats::failed_presence_updates);
}

void Discord::on_presence_updated(const PresenceData& data) {
    publish_presence_updated(data);
}

void Discord::on_connection_state_changed(bool connected) {
    if (connected) {
        publish_discord_connected(m_config.client_id);
    } else {
        publish_discord_disconnected("Connection lost", false);
    }
}

void Discord::on_error_occurred(core::DiscordError error, const std::string& message) {
    publish_discord_error(error, message);
}

// Rate limiting
bool Discord::rate_limit_can_proceed() {
    std::lock_guard<std::mutex> lock(m_rate_limit_mutex);

    rate_limit_cleanup_expired();

    if (!rate_limit_check_minimum_interval()) return false;
    if (!rate_limit_check_primary_window()) return false;
    if (!rate_limit_check_burst_window()) return false;

    return true;
}

void Discord::rate_limit_record_operation() {
    std::lock_guard<std::mutex> lock(m_rate_limit_mutex);

    auto now = std::chrono::steady_clock::now();
    m_operation_times.push_back(now);
    m_last_operation = now;
}

void Discord::rate_limit_cleanup_expired() const {
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - m_config.rate_limit_config.primary_window_duration;

    while (!m_operation_times.empty() && m_operation_times.front() < cutoff) {
        m_operation_times.pop_front();
    }
}

bool Discord::rate_limit_check_minimum_interval() const {
    if (m_last_operation == std::chrono::steady_clock::time_point{}) {
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    return (now - m_last_operation) >= m_config.rate_limit_config.minimum_interval;
}

bool Discord::rate_limit_check_primary_window() const {
    return static_cast<int>(m_operation_times.size()) < m_config.rate_limit_config.max_operations_per_window;
}

bool Discord::rate_limit_check_burst_window() const {
    auto now = std::chrono::steady_clock::now();
    auto burst_cutoff = now - m_config.rate_limit_config.burst_window_duration;

    int burst_count = static_cast<int>(std::count_if(m_operation_times.begin(), m_operation_times.end(),
        [burst_cutoff](const auto& time) { return time >= burst_cutoff; }));

    return burst_count < m_config.rate_limit_config.max_burst_operations;
}

// Connection management
void Discord::connection_loop() {
    LOG_DEBUG("Discord", "Connection loop started");

    auto last_health_check = std::chrono::steady_clock::now();
    int failed_health_checks = 0;

    while (!m_shutting_down) {
        try {
            auto now = std::chrono::steady_clock::now();

            if (m_force_reconnect_flag.exchange(false)) {
                LOG_INFO("Discord", "Processing force reconnect");
                if (m_ipc) {
                    m_ipc->disconnect();
                }
                m_connected = false;
                reset_retry_state();
            }

            if (!is_connected()) {
                if (should_attempt_reconnection()) {
                    if (attempt_connection()) {
                        handle_connection_success();
                        failed_health_checks = 0;
                        last_health_check = now;
                    } else {
                        handle_connection_failure();
                    }
                }
            } else {
                auto time_since_health_check = now - last_health_check;
                if (m_config.enable_health_checks &&
                    time_since_health_check >= m_config.connection_config.health_check_interval) {
                    if (perform_health_check()) {
                        failed_health_checks = 0;
                    } else {
                        ++failed_health_checks;
                        on_error_occurred(core::DiscordError::IpcError, "Health check failed");

                        if (failed_health_checks >= m_config.connection_config.max_failed_health_checks) {
                            LOG_WARNING("Discord", "Max health check failures reached, disconnecting");
                            if (m_ipc) {
                                m_ipc->disconnect();
                            }
                            m_connected = false;
                            on_connection_state_changed(false);
                            failed_health_checks = 0;
                        }
                    }
                    last_health_check = now;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        } catch (const std::exception& e) {
            LOG_ERROR("Discord", "Exception in connection loop: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    LOG_DEBUG("Discord", "Connection loop terminated");
}

bool Discord::attempt_connection() {
    if (!m_ipc) return false;

    LOG_DEBUG("Discord", "Attempting connection");

    try {
        bool success = m_ipc->connect();
        LOG_DEBUG("Discord", success ? "Connection successful" : "Connection failed");
        return success;
    } catch (const std::exception& e) {
        LOG_ERROR("Discord", "Exception during connection: " + std::string(e.what()));
        return false;
    }
}

void Discord::handle_connection_success(bool is_reconnect) {
    {
        std::lock_guard<std::mutex> lock(m_connection_mutex);
        m_retry_stats.last_success = std::chrono::system_clock::now();
        if (is_reconnect) {
            m_retry_stats.total_reconnections++;
        }
        m_retry_stats.consecutive_failures = 0;
        m_retry_stats.current_delay = std::chrono::seconds{0};
    }

    m_connected = true;
    on_connection_state_changed(true);
    m_update_cv.notify_one();

    LOG_DEBUG("Discord", "Connection established successfully");
}

void Discord::handle_connection_failure() {
    int attempt_number;
    std::chrono::seconds next_delay;

    {
        std::lock_guard<std::mutex> lock(m_connection_mutex);
        m_retry_stats.last_failure = std::chrono::system_clock::now();
        m_retry_stats.consecutive_failures++;
        attempt_number = m_retry_stats.consecutive_failures;
    }

    next_delay = calculate_next_delay();

    {
        std::lock_guard<std::mutex> lock(m_connection_mutex);
        m_retry_stats.current_delay = next_delay;
    }

    m_connected = false;
    on_connection_state_changed(false);

    LOG_DEBUG("Discord", "Connection failed (attempt " + std::to_string(attempt_number) +
        "), retrying in " + std::to_string(next_delay.count()) + "s");

    auto end_time = std::chrono::steady_clock::now() + next_delay;
    while (!m_shutting_down && std::chrono::steady_clock::now() < end_time) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (m_force_reconnect_flag) break;
    }
}

bool Discord::perform_health_check() {
    if (!m_ipc) return false;

    try {
        bool healthy = m_ipc->send_health_check();
        LOG_DEBUG("Discord", "Health check: " + std::string(healthy ? "OK" : "FAILED"));
        return healthy;
    } catch (const std::exception& e) {
        LOG_WARNING("Discord", "Health check exception: " + std::string(e.what()));
        return false;
    }
}

std::chrono::seconds Discord::calculate_next_delay() {
    std::lock_guard<std::mutex> lock(m_connection_mutex);

    if (m_retry_stats.consecutive_failures == 0) {
        return m_config.connection_config.initial_delay;
    }

    auto delay = m_config.connection_config.initial_delay;
    for (int i = 1; i < m_retry_stats.consecutive_failures; ++i) {
        delay = std::chrono::seconds(static_cast<long>(
            static_cast<double>(delay.count()) * m_config.connection_config.backoff_multiplier));
        if (delay >= m_config.connection_config.max_delay) {
            delay = m_config.connection_config.max_delay;
            break;
        }
    }

    auto jitter_range = delay.count() / 10;
    auto jitter = (std::rand() % (2 * jitter_range + 1)) - jitter_range;
    delay += std::chrono::seconds(jitter);

    return (delay > m_config.connection_config.initial_delay) ? delay : m_config.connection_config.initial_delay;
}

void Discord::reset_retry_state() {
    std::lock_guard<std::mutex> lock(m_connection_mutex);
    m_retry_stats.consecutive_failures = 0;
    m_retry_stats.current_delay = std::chrono::seconds{0};
}

bool Discord::should_attempt_reconnection() {
    std::lock_guard<std::mutex> lock(m_connection_mutex);

    if (m_retry_stats.consecutive_failures >= m_config.connection_config.max_consecutive_failures) {
        auto now = std::chrono::system_clock::now();
        auto time_since_last_failure = now - m_retry_stats.last_failure;

        if (time_since_last_failure < m_config.connection_config.failure_cooldown) {
            return false;
        }

        m_retry_stats.consecutive_failures = 0;
        m_retry_stats.current_delay = std::chrono::seconds{0};
    }

    return true;
}

// Factory
std::expected<std::unique_ptr<Discord>, core::ConfigError>
Discord::create(const core::ApplicationConfig& app_config) {
    if (app_config.discord.discord.client_id.empty()) {
        return std::unexpected(core::ConfigError::ValidationError);
    }

    Config config;
    config.client_id = app_config.discord.discord.client_id;
    config.update_interval = app_config.discord.discord.update_interval;
    config.rate_limit_config = RateLimitConfig{};
    config.connection_config = ConnectionConfig{};
    config.enable_rate_limiting = true;
    config.enable_health_checks = true;

    if (!config.is_valid()) {
        return std::unexpected(core::ConfigError::ValidationError);
    }

    try {
        return std::make_unique<Discord>(std::move(config));
    } catch (const std::exception& e) {
        LOG_ERROR("Discord", "Failed to create service: " + std::string(e.what()));
        return std::unexpected(core::ConfigError::InvalidFormat);
    }
}

} // namespace presence_for_plex::services
