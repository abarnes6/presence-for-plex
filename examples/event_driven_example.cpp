#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/core/event_logger.hpp"
#include "presence_for_plex/utils/logging.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace presence_for_plex::core;
using namespace presence_for_plex::core::events;

class MediaWatcher {
public:
    MediaWatcher(std::shared_ptr<EventBus> bus) : m_event_bus(bus) {
        m_subscription = m_event_bus->subscribe<MediaSessionStarted>(
            [this](const MediaSessionStarted& event) {
                handle_media_started(event);
            });
    }

private:
    void handle_media_started(const MediaSessionStarted& event) {
        std::cout << "Media started: " << event.media_info.title
                  << " on server " << event.server_id.value << std::endl;

        auto app_event = ApplicationStateChanged{
            ApplicationState::Running,
            ApplicationState::Running
        };
        m_event_bus->publish(app_event);
    }

    std::shared_ptr<EventBus> m_event_bus;
    EventBus::HandlerId m_subscription;
};

class ConnectionMonitor {
public:
    ConnectionMonitor(std::shared_ptr<EventBus> bus) : m_event_bus(bus) {
        m_server_connected_sub = m_event_bus->subscribe<ServerConnectionEstablished>(
            [this](const ServerConnectionEstablished& event) {
                std::cout << "Server connected: " << event.server_name
                          << " (" << event.server_id.value << ")" << std::endl;
                m_connected_servers++;
                report_status();
            });

        m_server_disconnected_sub = m_event_bus->subscribe<ServerConnectionLost>(
            [this](const ServerConnectionLost& event) {
                std::cout << "Server disconnected: " << event.server_id.value
                          << " - " << event.reason << std::endl;
                m_connected_servers--;
                report_status();
            });
    }

private:
    void report_status() {
        std::cout << "Currently connected servers: " << m_connected_servers << std::endl;
    }

    std::shared_ptr<EventBus> m_event_bus;
    EventBus::HandlerId m_server_connected_sub;
    EventBus::HandlerId m_server_disconnected_sub;
    int m_connected_servers = 0;
};

class EventDrivenApplication {
public:
    EventDrivenApplication()
        : m_event_bus(std::make_shared<EventBus>()),
          m_event_logger(std::make_unique<EventLogger>(m_event_bus)),
          m_event_debugger(std::make_unique<EventDebugger>(m_event_bus)) {
    }

    void initialize() {
        std::cout << "Initializing event-driven application..." << std::endl;

        m_event_logger->start();
        m_event_debugger->start_recording();

        m_media_watcher = std::make_unique<MediaWatcher>(m_event_bus);
        m_connection_monitor = std::make_unique<ConnectionMonitor>(m_event_bus);

        m_event_bus->publish(ApplicationStarting{"1.0.0"});

        m_event_bus->publish(ApplicationStateChanged{
            ApplicationState::NotInitialized,
            ApplicationState::Initializing
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        m_event_bus->publish(ApplicationStateChanged{
            ApplicationState::Initializing,
            ApplicationState::Running
        });

        m_event_bus->publish(ApplicationReady{std::chrono::milliseconds(100)});
    }

    void simulate_activity() {
        std::cout << "\n=== Simulating server connections ===" << std::endl;

        m_event_bus->publish(ServerConnectionEstablished{
            ServerId{"server-1"},
            "Home Plex Server"
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        m_event_bus->publish(ServerConnectionEstablished{
            ServerId{"server-2"},
            "Remote Plex Server"
        });

        std::cout << "\n=== Simulating media playback ===" << std::endl;

        MediaInfo movie;
        movie.title = "The Matrix";
        movie.type = MediaType::Movie;
        movie.year = 1999;
        movie.playback_state = PlaybackState::Playing;

        m_event_bus->publish(MediaSessionStarted{
            movie,
            ServerId{"server-1"}
        });

        std::this_thread::sleep_for(std::chrono::seconds(2));

        MediaInfo updated_movie = movie;
        updated_movie.playback_state = PlaybackState::Paused;

        m_event_bus->publish(MediaSessionUpdated{
            movie,
            updated_movie
        });

        std::cout << "\n=== Simulating error conditions ===" << std::endl;

        m_event_bus->publish(MediaError{
            PlexError::NetworkError,
            "Connection timeout",
            ServerId{"server-2"}
        });

        m_event_bus->publish(ServerConnectionLost{
            ServerId{"server-2"},
            "Network timeout"
        });

        std::this_thread::sleep_for(std::chrono::seconds(1));

        m_event_bus->publish(ServerReconnecting{
            ServerId{"server-2"},
            1,
            std::chrono::seconds(5)
        });
    }

    void demonstrate_async_events() {
        std::cout << "\n=== Demonstrating async event publishing ===" << std::endl;

        for (int i = 0; i < 5; ++i) {
            m_event_bus->publish_async(ServiceInitialized{
                "AsyncService-" + std::to_string(i)
            });
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    void demonstrate_metrics() {
        std::cout << "\n=== Event Metrics ===" << std::endl;

        auto metrics = std::make_unique<EventMetrics>(m_event_bus);
        metrics->start_collecting();

        for (int i = 0; i < 10; ++i) {
            m_event_bus->publish(HealthCheckSucceeded{
                "service-" + std::to_string(i),
                std::chrono::milliseconds(50 + i * 10)
            });
        }

        metrics->print_summary();
    }

    void shutdown() {
        std::cout << "\n=== Shutting down ===" << std::endl;

        m_event_bus->publish(ApplicationShuttingDown{"User requested"});

        m_event_bus->publish(ApplicationStateChanged{
            ApplicationState::Running,
            ApplicationState::Stopping
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        m_event_bus->publish(ApplicationStateChanged{
            ApplicationState::Stopping,
            ApplicationState::Stopped
        });

        m_event_logger->stop();
        m_event_debugger->stop_recording();

        std::cout << "Event history count: " << m_event_debugger->get_event_count() << std::endl;

        auto history = m_event_debugger->get_filtered_history("Media");
        std::cout << "Media-related events: " << history.size() << std::endl;

        m_event_debugger->dump_to_file("/tmp/event_history.log");
        std::cout << "Event history dumped to /tmp/event_history.log" << std::endl;
    }

private:
    std::shared_ptr<EventBus> m_event_bus;
    std::unique_ptr<EventLogger> m_event_logger;
    std::unique_ptr<EventDebugger> m_event_debugger;
    std::unique_ptr<MediaWatcher> m_media_watcher;
    std::unique_ptr<ConnectionMonitor> m_connection_monitor;
};

int main() {
    std::cout << "Event-Driven Architecture Example\n";
    std::cout << "==================================\n\n";

    try {
        EventDrivenApplication app;

        app.initialize();
        app.simulate_activity();
        app.demonstrate_async_events();
        app.demonstrate_metrics();
        app.shutdown();

        std::cout << "\nExample completed successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}