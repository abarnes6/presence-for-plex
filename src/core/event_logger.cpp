#include "presence_for_plex/core/event_logger.hpp"
#include "presence_for_plex/core/events_impl.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace presence_for_plex::core {

EventLogger::EventLogger(std::shared_ptr<EventBus> bus, LogLevel min_level)
    : m_event_bus(std::move(bus)), m_min_level(min_level) {}

EventLogger::~EventLogger() {
    stop();
}

void EventLogger::start() {
    if (m_running) return;

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::ConfigurationUpdated>(
            [this](const auto& e) { log_configuration_event(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::ConfigurationError>(
            [this](const auto& e) { log_configuration_error(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::MediaSessionStarted>(
            [this](const auto& e) { log_media_session_started(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::MediaSessionUpdated>(
            [this](const auto& e) { log_media_session_updated(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::MediaSessionEnded>(
            [this](const auto& e) { log_media_session_ended(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::MediaError>(
            [this](const auto& e) { log_media_error(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::ServerConnectionEstablished>(
            [this](const auto& e) { log_server_connection(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::ServerConnectionLost>(
            [this](const auto& e) { log_server_disconnection(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::PresenceUpdated>(
            [this](const auto& e) { log_presence_updated(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::DiscordConnected>(
            [this](const auto& e) { log_discord_connected(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::DiscordDisconnected>(
            [this](const auto& e) { log_discord_disconnected(e); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::ApplicationStateChanged>(
            [this](const auto& e) { log_application_state(e); }));

    m_running = true;
    PLEX_LOG_INFO("EventLogger", "Event logger started");
}

void EventLogger::stop() {
    if (!m_running) return;

    for (auto id : m_subscriptions) {
        m_event_bus->unsubscribe(id);
    }
    m_subscriptions.clear();
    m_running = false;
    PLEX_LOG_INFO("EventLogger", "Event logger stopped");
}

void EventLogger::set_log_level(LogLevel level) {
    m_min_level = level;
}

EventLogger::LogLevel EventLogger::get_log_level() const {
    return m_min_level;
}

void EventLogger::log_configuration_event(const events::ConfigurationUpdated& event) {
    (void)event;
    PLEX_LOG_INFO("EventLogger", "Configuration updated");
}

void EventLogger::log_configuration_error(const events::ConfigurationError& event) {
    PLEX_LOG_ERROR("EventLogger", "Configuration error: " + event.message);
}

void EventLogger::log_media_session_started(const events::MediaSessionStarted& event) {
    PLEX_LOG_INFO("EventLogger", "Media session started: " + event.media_info.title + " on server " + event.server_id.value);
}

void EventLogger::log_media_session_updated(const events::MediaSessionUpdated& event) {
    if (m_min_level <= LogLevel::Debug) {
        PLEX_LOG_DEBUG("EventLogger", "Media session updated: " + event.current_info.title);
    }
}

void EventLogger::log_media_session_ended(const events::MediaSessionEnded& event) {
    PLEX_LOG_INFO("EventLogger", "Media session ended: " + event.session_key.value + " on server " + event.server_id.value);
}

void EventLogger::log_media_error(const events::MediaError& event) {
    PLEX_LOG_ERROR("EventLogger", "Media error: " + event.message);
}

void EventLogger::log_server_connection(const events::ServerConnectionEstablished& event) {
    PLEX_LOG_INFO("EventLogger", "Connected to server: " + event.server_name + " (" + event.server_id.value + ")");
}

void EventLogger::log_server_disconnection(const events::ServerConnectionLost& event) {
    PLEX_LOG_WARNING("EventLogger", "Disconnected from server " + event.server_id.value + ": " + event.reason);
}

void EventLogger::log_presence_updated(const events::PresenceUpdated& event) {
    if (m_min_level <= LogLevel::Debug) {
        PLEX_LOG_DEBUG("EventLogger", "Presence updated: " + event.presence_data.details);
    }
}

void EventLogger::log_discord_connected(const events::DiscordConnected& event) {
    PLEX_LOG_INFO("EventLogger", "Discord connected: client_id=" + event.client_id);
}

void EventLogger::log_discord_disconnected(const events::DiscordDisconnected& event) {
    PLEX_LOG_WARNING("EventLogger", "Discord disconnected: " + event.reason + " (will_retry=" + (event.will_retry ? "true" : "false") + ")");
}

void EventLogger::log_discord_error(const events::DiscordErrorEvent& event) {
    PLEX_LOG_ERROR("EventLogger", "Discord error: " + event.message);
}

void EventLogger::log_application_state(const events::ApplicationStateChanged& event) {
    PLEX_LOG_INFO("EventLogger", "Application state changed: " + std::to_string(static_cast<int>(event.previous_state)) + " -> " + std::to_string(static_cast<int>(event.current_state)));
}

void EventLogger::log_service_event(const events::ServiceInitialized& event) {
    PLEX_LOG_INFO("EventLogger", "Service initialized: " + event.service_name);
}

void EventLogger::log_service_error(const events::ServiceError& event) {
    PLEX_LOG_ERROR("EventLogger", "Service error [" + event.service_name + "]: " + event.error_message + " (recoverable=" + (event.recoverable ? "true" : "false") + ")");
}

EventDebugger::EventDebugger(std::shared_ptr<EventBus> bus, size_t max_history)
    : m_event_bus(std::move(bus)), m_max_history(max_history) {}

EventDebugger::~EventDebugger() {
    stop_recording();
}

void EventDebugger::start_recording() {
    if (m_recording) return;

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::ConfigurationUpdated>(
            [this](const auto& e) {
                (void)e;
                add_record("ConfigurationUpdated", "Config changed");
            }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::MediaSessionStarted>(
            [this](const auto& e) {
                add_record("MediaSessionStarted",
                          "Title: " + e.media_info.title);
            }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::MediaSessionEnded>(
            [this](const auto& e) {
                add_record("MediaSessionEnded",
                          "Session: " + e.session_key.value);
            }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::PresenceUpdated>(
            [this](const auto& e) {
                add_record("PresenceUpdated",
                          "Details: " + e.presence_data.details);
            }));

    m_recording = true;
}

void EventDebugger::stop_recording() {
    if (!m_recording) return;

    for (auto id : m_subscriptions) {
        m_event_bus->unsubscribe(id);
    }
    m_subscriptions.clear();
    m_recording = false;
}

bool EventDebugger::is_recording() const {
    return m_recording;
}

std::vector<EventDebugger::EventRecord> EventDebugger::get_history() const {
    std::lock_guard lock(m_history_mutex);
    return m_history;
}

std::vector<EventDebugger::EventRecord> EventDebugger::get_filtered_history(
    const std::string& type_filter) const {
    std::lock_guard lock(m_history_mutex);
    std::vector<EventRecord> filtered;

    for (const auto& record : m_history) {
        if (record.type_name.find(type_filter) != std::string::npos) {
            filtered.push_back(record);
        }
    }

    return filtered;
}

void EventDebugger::clear_history() {
    std::lock_guard lock(m_history_mutex);
    m_history.clear();
    m_sequence_counter = 0;
}

size_t EventDebugger::get_event_count() const {
    std::lock_guard lock(m_history_mutex);
    return m_history.size();
}

size_t EventDebugger::get_event_count_by_type(const std::string& type_name) const {
    std::lock_guard lock(m_history_mutex);
    return static_cast<size_t>(std::count_if(m_history.begin(), m_history.end(),
                        [&](const auto& record) {
                            return record.type_name == type_name;
                        }));
}

void EventDebugger::dump_to_file(const std::filesystem::path& path) const {
    std::lock_guard lock(m_history_mutex);
    std::ofstream file(path);

    for (const auto& record : m_history) {
        file << "[" << record.sequence_number << "] "
             << record.type_name << " - "
             << record.details << "\n";
    }
}

void EventDebugger::add_record(const std::string& type_name,
                               const std::string& details) {
    std::lock_guard lock(m_history_mutex);

    EventRecord record{
        type_name,
        std::chrono::steady_clock::now(),
        details,
        m_sequence_counter++
    };

    m_history.push_back(record);

    if (m_history.size() > m_max_history) {
        m_history.erase(m_history.begin());
    }
}

EventMetrics::EventMetrics(std::shared_ptr<EventBus> bus)
    : m_event_bus(std::move(bus)) {}

EventMetrics::~EventMetrics() {
    stop_collecting();
}

void EventMetrics::start_collecting() {
    if (m_collecting) return;

    m_start_time = std::chrono::steady_clock::now();
    m_collecting = true;

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::MediaSessionStarted>(
            [this](const auto& e) { (void)e; update_event_count("MediaSessionStarted"); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::MediaSessionEnded>(
            [this](const auto& e) { (void)e; update_event_count("MediaSessionEnded"); }));

    m_subscriptions.push_back(
        m_event_bus->subscribe<events::PresenceUpdated>(
            [this](const auto& e) { (void)e; update_event_count("PresenceUpdated"); }));
}

void EventMetrics::stop_collecting() {
    if (!m_collecting) return;

    for (auto id : m_subscriptions) {
        m_event_bus->unsubscribe(id);
    }
    m_subscriptions.clear();
    m_collecting = false;
}

void EventMetrics::reset() {
    std::lock_guard lock(m_metrics_mutex);
    m_metrics = Metrics{};
    m_start_time = std::chrono::steady_clock::now();
}

EventMetrics::Metrics EventMetrics::get_metrics() const {
    std::lock_guard lock(m_metrics_mutex);
    return m_metrics;
}

void EventMetrics::print_summary() const {
    auto metrics = get_metrics();

    PLEX_LOG_INFO("EventMetrics", "Event Metrics Summary:");
    PLEX_LOG_INFO("EventMetrics", "  Total events: " + std::to_string(metrics.total_events));
    PLEX_LOG_INFO("EventMetrics", "  Events per minute: " + std::to_string(metrics.events_per_minute));

    for (const auto& [type, count] : metrics.event_counts) {
        PLEX_LOG_INFO("EventMetrics", "  " + type + ": " + std::to_string(count));
    }
}

void EventMetrics::update_event_count(const std::string& type_name) {
    std::lock_guard lock(m_metrics_mutex);
    m_metrics.total_events++;
    m_metrics.event_counts[type_name]++;

    auto elapsed = std::chrono::steady_clock::now() - m_start_time;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed).count();
    if (minutes > 0) {
        m_metrics.events_per_minute = m_metrics.total_events / static_cast<size_t>(minutes);
    }
}

}