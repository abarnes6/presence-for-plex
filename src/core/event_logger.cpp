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

}