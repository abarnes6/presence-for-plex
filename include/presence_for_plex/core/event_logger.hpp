#pragma once

#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <memory>
#include <string>
#include <vector>

namespace presence_for_plex::core {

class EventLogger {
public:
    enum class LogLevel {
        Debug,
        Info,
        Warning,
        Error
    };

    explicit EventLogger(std::shared_ptr<EventBus> bus, LogLevel min_level = LogLevel::Info);
    ~EventLogger();

    void start();
    void stop();

    void set_log_level(LogLevel level);
    LogLevel get_log_level() const;

    void enable_event_type(const std::string& type_name);
    void disable_event_type(const std::string& type_name);
    void set_event_filter(std::function<bool(const std::string&)> filter);

private:
    std::shared_ptr<EventBus> m_event_bus;
    LogLevel m_min_level;
    std::vector<EventBus::HandlerId> m_subscriptions;
    std::function<bool(const std::string&)> m_event_filter;
    bool m_running{false};

    template<typename EventType>
    void subscribe_to_event();

    void log_configuration_event(const events::ConfigurationUpdated& event);
    void log_configuration_error(const events::ConfigurationError& event);
    void log_media_session_started(const events::MediaSessionStarted& event);
    void log_media_session_updated(const events::MediaSessionUpdated& event);
    void log_media_session_ended(const events::MediaSessionEnded& event);
    void log_media_error(const events::MediaError& event);
    void log_server_connection(const events::ServerConnectionEstablished& event);
    void log_server_disconnection(const events::ServerConnectionLost& event);
    void log_presence_updated(const events::PresenceUpdated& event);
    void log_discord_connected(const events::DiscordConnected& event);
    void log_discord_disconnected(const events::DiscordDisconnected& event);
    void log_discord_error(const events::DiscordErrorEvent& event);
    void log_application_state(const events::ApplicationStateChanged& event);
    void log_service_event(const events::ServiceInitialized& event);
    void log_service_error(const events::ServiceError& event);

    std::string format_timestamp(const std::chrono::steady_clock::time_point& tp) const;
    std::string format_media_info(const MediaInfo& info) const;
};

}