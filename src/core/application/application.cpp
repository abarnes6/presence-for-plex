#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/services/media_service.hpp"
#include "presence_for_plex/services/presence_service.hpp"
#include "presence_for_plex/platform/ui_service.hpp"
#include "presence_for_plex/platform/system_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/threading.hpp"
#include "presence_for_plex/services/plex/plex_service_impl.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>

namespace presence_for_plex {
namespace core {

// Concrete implementation of Application interface
class ApplicationImpl : public Application {
public:
    ApplicationImpl()
        : m_state(ApplicationState::NotInitialized),
          m_running(false),
          m_shutdown_requested(false) {
        PLEX_LOG_DEBUG("Application", "ApplicationImpl constructed");
    }

    ~ApplicationImpl() override {
        if (m_running.load()) {
            stop();
            shutdown();
        }
        PLEX_LOG_DEBUG("Application", "ApplicationImpl destroyed");
    }

    std::expected<void, ApplicationError> initialize() override {
        PLEX_LOG_INFO("Application", "Initializing application...");

        if (m_state != ApplicationState::NotInitialized) {
            PLEX_LOG_WARNING("Application", "Application already initialized");
            return std::unexpected<ApplicationError>(ApplicationError::AlreadyRunning);
        }

        m_state = ApplicationState::Initializing;

        try {
            // Initialize configuration service
            m_config_service = ConfigurationService::create("");
            if (!m_config_service) {
                PLEX_LOG_ERROR("Application", "Failed to create configuration service");
                m_state = ApplicationState::Error;
                return std::unexpected<ApplicationError>(ApplicationError::ConfigurationError);
            }

            // Load configuration
            auto config_result = m_config_service->load_configuration();
            if (!config_result) {
                PLEX_LOG_WARNING("Application", "Failed to load configuration, using defaults");
            }

            // Initialize thread pool
            m_thread_pool = std::make_unique<utils::ThreadPool>(4);
            m_task_scheduler = std::make_unique<utils::TaskScheduler>();

            // Initialize services
            PLEX_LOG_INFO("Application", "Initializing services...");

            // Initialize platform services first
            auto ui_factory = platform::UiServiceFactory::create_default_factory();
            if (ui_factory) {
                m_ui_service = ui_factory->create_service();
                if (m_ui_service) {
                    auto ui_init_result = m_ui_service->initialize();
                    if (ui_init_result) {
                        PLEX_LOG_INFO("Application", "UI service initialized");
                    } else {
                        PLEX_LOG_WARNING("Application", "Failed to initialize UI service");
                    }
                } else {
                    PLEX_LOG_WARNING("Application", "Failed to create UI service");
                }
            } else {
                PLEX_LOG_WARNING("Application", "UI service not supported on this platform");
            }

            // Initialize media service (Plex)
            m_media_service = services::PlexServiceBuilder()
                .with_configuration_service(std::shared_ptr<ConfigurationService>(m_config_service.get(), [](ConfigurationService*){}))
                .build();
            PLEX_LOG_INFO("Application", "Media service (Plex) initialized");

            // Initialize presence service (Discord)
            auto presence_factory = services::PresenceServiceFactory::create_default_factory();
            if (presence_factory) {
                m_presence_service = presence_factory->create_service(
                    services::PresenceServiceFactory::ServiceType::Discord,
                    m_config_service->get_config()
                );
                PLEX_LOG_INFO("Application", "Presence service (Discord) initialized");
            } else {
                PLEX_LOG_WARNING("Application", "Failed to create presence service factory");
            }

            PLEX_LOG_INFO("Application", "Application initialized successfully");
            m_state = ApplicationState::Running;
            return {};

        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("Application", "Exception during initialization: " + std::string(e.what()));
            m_state = ApplicationState::Error;
            return std::unexpected<ApplicationError>(ApplicationError::InitializationFailed);
        }
    }

    std::expected<void, ApplicationError> start() override {
        PLEX_LOG_INFO("Application", "Starting application...");

        if (m_state != ApplicationState::Running) {
            PLEX_LOG_ERROR("Application", "Application not initialized");
            return std::unexpected<ApplicationError>(ApplicationError::InitializationFailed);
        }

        try {
            m_running.store(true);
            m_shutdown_requested.store(false);

            // Start services
            PLEX_LOG_INFO("Application", "Starting services...");

            // Start media service
            if (m_media_service) {
                auto media_result = m_media_service->start();
                if (!media_result) {
                    PLEX_LOG_WARNING("Application", "Failed to start media service");
                } else {
                    PLEX_LOG_INFO("Application", "Media service started successfully");
                }
            }

            // Start presence service
            if (m_presence_service) {
                auto presence_result = m_presence_service->initialize();
                if (!presence_result) {
                    PLEX_LOG_WARNING("Application", "Failed to start presence service");
                } else {
                    PLEX_LOG_INFO("Application", "Presence service started successfully");
                }
            }

            // Initialize system tray
            initialize_system_tray();

            PLEX_LOG_INFO("Application", "Application started successfully");
            return {};

        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("Application", "Exception during start: " + std::string(e.what()));
            return std::unexpected<ApplicationError>(ApplicationError::InitializationFailed);
        }
    }

    void stop() override {
        PLEX_LOG_INFO("Application", "Stopping application...");
        m_state = ApplicationState::Stopping;
        m_running.store(false);
        m_shutdown_requested.store(true);
    }

    void shutdown() override {
        PLEX_LOG_INFO("Application", "Shutting down application...");

        // Stop services gracefully
        PLEX_LOG_INFO("Application", "Stopping services...");

        if (m_media_service) {
            m_media_service->stop();
            PLEX_LOG_INFO("Application", "Media service stopped");
        }

        if (m_presence_service) {
            m_presence_service->shutdown();
            PLEX_LOG_INFO("Application", "Presence service stopped");
        }

        // Hide and cleanup system tray first
        if (m_system_tray) {
            m_system_tray->hide();
            m_system_tray->shutdown();
            m_system_tray.reset();
            PLEX_LOG_INFO("Application", "System tray stopped");
        }

        if (m_ui_service) {
            m_ui_service->shutdown();
            PLEX_LOG_INFO("Application", "UI service stopped");
        }

        // Stop all services gracefully
        if (m_thread_pool) {
            m_thread_pool->shutdown();
        }

        m_state = ApplicationState::Stopped;
        PLEX_LOG_INFO("Application", "Application shutdown complete");
    }

    ApplicationState get_state() const override {
        return m_state;
    }

    bool is_running() const override {
        return m_running.load() && !m_shutdown_requested.load();
    }

    void run() override {
        PLEX_LOG_INFO("Application", "Starting main application run loop");
        while (is_running()) {
            run_once();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        PLEX_LOG_INFO("Application", "Main application run loop ended");
    }

    void run_once() override {
#ifdef _WIN32
        // Process UI events if we have a UI service
        if (m_ui_service) {
            m_ui_service->process_events();
        }
#endif
    }

    void quit() override {
        PLEX_LOG_INFO("Application", "Quit requested");
        stop();
        shutdown();
    }

    // Service access
    services::MediaService& get_media_service() override {
        if (!m_media_service) {
            throw std::runtime_error("MediaService not initialized");
        }
        return *m_media_service;
    }

    services::PresenceService& get_presence_service() override {
        if (!m_presence_service) {
            throw std::runtime_error("PresenceService not initialized");
        }
        return *m_presence_service;
    }

    platform::UiService& get_ui_service() override {
        if (!m_ui_service) {
            throw std::runtime_error("UiService not initialized");
        }
        return *m_ui_service;
    }

    ConfigurationService& get_configuration_service() override {
        if (!m_config_service) {
            throw std::runtime_error("ConfigurationService not initialized");
        }
        return *m_config_service;
    }

    // Utilities
    utils::ThreadPool& get_thread_pool() override {
        if (!m_thread_pool) {
            throw std::runtime_error("ThreadPool not initialized");
        }
        return *m_thread_pool;
    }

    utils::TaskScheduler& get_task_scheduler() override {
        if (!m_task_scheduler) {
            throw std::runtime_error("TaskScheduler not initialized");
        }
        return *m_task_scheduler;
    }

private:
    void initialize_system_tray() {
        if (!m_ui_service || !m_ui_service->supports_system_tray()) {
            PLEX_LOG_DEBUG("Application", "System tray not supported or UI service not available");
            return;
        }

        m_system_tray = m_ui_service->create_system_tray();
        if (!m_system_tray) {
            PLEX_LOG_WARNING("Application", "Failed to create system tray");
            return;
        }

        auto tray_init_result = m_system_tray->initialize();
        if (!tray_init_result) {
            PLEX_LOG_WARNING("Application", "Failed to initialize system tray");
            return;
        }

        // Set the application icon
        std::string icon_path = ":/icons/app_icon";
        auto icon_result = m_system_tray->set_icon(icon_path);
        if (!icon_result) {
            PLEX_LOG_WARNING("Application", "Failed to set system tray icon");
        }

        // Set tooltip
        auto tooltip_result = m_system_tray->set_tooltip("Presence for Plex");
        if (!tooltip_result) {
            PLEX_LOG_WARNING("Application", "Failed to set system tray tooltip");
        }

        // Setup tray menu
        setup_tray_menu();

        // Show the tray icon
        m_system_tray->show();
        PLEX_LOG_INFO("Application", "System tray created and shown");
    }

    void setup_tray_menu() {
        if (!m_system_tray) {
            return;
        }

        std::vector<platform::MenuItem> menu_items;

        // Status item
        platform::MenuItem status_item;
        status_item.id = "status";
        status_item.label = "Status: Running";
        status_item.enabled = false;
        menu_items.push_back(status_item);

        // Separator
        platform::MenuItem separator;
        separator.type = platform::MenuItemType::Separator;
        menu_items.push_back(separator);

        // Settings item (placeholder for future implementation)
        platform::MenuItem settings_item;
        settings_item.id = "settings";
        settings_item.label = "Settings...";
        settings_item.enabled = false;  // Disabled for now
        menu_items.push_back(settings_item);

        // Another separator
        menu_items.push_back(separator);

        // Exit item
        platform::MenuItem exit_item;
        exit_item.id = "exit";
        exit_item.label = "Exit";
        exit_item.action = [this]() {
            PLEX_LOG_INFO("Application", "Exit requested from system tray");
            this->quit();
        };
        menu_items.push_back(exit_item);

        auto menu_result = m_system_tray->set_menu(menu_items);
        if (!menu_result) {
            PLEX_LOG_WARNING("Application", "Failed to set system tray menu");
        }
    }

    // Application state
    std::atomic<ApplicationState> m_state;
    std::atomic<bool> m_running;
    std::atomic<bool> m_shutdown_requested;

    // Core services
    std::unique_ptr<ConfigurationService> m_config_service;

    // Business services
    std::unique_ptr<services::MediaService> m_media_service;
    std::unique_ptr<services::PresenceService> m_presence_service;

    // Platform services
    std::unique_ptr<platform::UiService> m_ui_service;
    std::unique_ptr<platform::SystemTray> m_system_tray;

    // Utilities
    std::unique_ptr<utils::ThreadPool> m_thread_pool;
    std::unique_ptr<utils::TaskScheduler> m_task_scheduler;
};

// Factory implementation
std::expected<std::unique_ptr<Application>, ApplicationError> ApplicationFactory::create_default_application(
    const std::filesystem::path& config_path
) {
	(void)config_path;
    PLEX_LOG_INFO("ApplicationFactory", "Creating default application instance");

    try {
        auto app = std::make_unique<ApplicationImpl>();
        return std::expected<std::unique_ptr<Application>, ApplicationError>(std::move(app));
    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("ApplicationFactory", "Failed to create application: " + std::string(e.what()));
        return std::unexpected<ApplicationError>(ApplicationError::InitializationFailed);
    }
}

std::expected<std::unique_ptr<Application>, ApplicationError> ApplicationFactory::create_application_with_config(
    const ApplicationConfig& config
) {
	(void)config;
    PLEX_LOG_INFO("ApplicationFactory", "Creating application with custom config");

    try {
        auto app = std::make_unique<ApplicationImpl>();
        return std::expected<std::unique_ptr<Application>, ApplicationError>(std::move(app));
    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("ApplicationFactory", "Failed to create application with config: " + std::string(e.what()));
        return std::unexpected<ApplicationError>(ApplicationError::InitializationFailed);
    }
}

} // namespace core
} // namespace presence_for_plex
