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

class EventDebugger {
public:
    struct EventRecord {
        std::string type_name;
        std::chrono::steady_clock::time_point timestamp;
        std::string details;
        size_t sequence_number;
    };

    explicit EventDebugger(std::shared_ptr<EventBus> bus, size_t max_history = 1000);
    ~EventDebugger();

    void start_recording();
    void stop_recording();
    bool is_recording() const;

    std::vector<EventRecord> get_history() const;
    std::vector<EventRecord> get_filtered_history(const std::string& type_filter) const;
    void clear_history();

    size_t get_event_count() const;
    size_t get_event_count_by_type(const std::string& type_name) const;

    void dump_to_file(const std::filesystem::path& path) const;

private:
    std::shared_ptr<EventBus> m_event_bus;
    std::vector<EventRecord> m_history;
    size_t m_max_history;
    std::atomic<size_t> m_sequence_counter{0};
    std::vector<EventBus::HandlerId> m_subscriptions;
    mutable std::mutex m_history_mutex;
    bool m_recording{false};

    template<typename EventType>
    void record_event(const EventType& event);

    void add_record(const std::string& type_name, const std::string& details);
};

class EventMetrics {
public:
    struct Metrics {
        size_t total_events{0};
        size_t events_per_minute{0};
        std::unordered_map<std::string, size_t> event_counts;
        std::chrono::milliseconds average_processing_time{0};
        std::chrono::milliseconds max_processing_time{0};
    };

    explicit EventMetrics(std::shared_ptr<EventBus> bus);
    ~EventMetrics();

    void start_collecting();
    void stop_collecting();
    void reset();

    Metrics get_metrics() const;
    void print_summary() const;

private:
    std::shared_ptr<EventBus> m_event_bus;
    mutable std::mutex m_metrics_mutex;
    Metrics m_metrics;
    std::chrono::steady_clock::time_point m_start_time;
    std::vector<EventBus::HandlerId> m_subscriptions;
    bool m_collecting{false};

    template<typename EventType>
    void track_event(const EventType& event);

    void update_event_count(const std::string& type_name);
};

}