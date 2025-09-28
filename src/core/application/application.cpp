#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/services/plex/plex_service_impl.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/threading.hpp"

#include <chrono>
#include <thread>

namespace presence_for_plex {
namespace core {

class ApplicationImpl : public Application {
public:
    ApplicationImpl()
        : m_state(ApplicationState::NotInitialized),
          m_running(false),
          m_shutdown_requested(false),
          m_runtime_state(std::make_unique<RuntimeState>()),
          m_event_bus(std::make_shared<EventBus>()) {
        PLEX_LOG_DEBUG("Application", "Application created");
    }

    ~ApplicationImpl() override {
        if (m_running) {
            stop();
            shutdown();
        }
        PLEX_LOG_DEBUG("Application", "Application destroyed");
    }

    std::expected<void, ApplicationError> initialize() override {
        PLEX_LOG_INFO("Application", "Initializing...");

        if (m_state != ApplicationState::NotInitialized) {
            PLEX_LOG_WARNING("Application", "Already initialized");
            return std::unexpected(ApplicationError::AlreadyRunning);
        }

        m_state = ApplicationState::Initializing;

        try {
            if (!initialize_configuration()) {
                return std::unexpected(ApplicationError::ConfigurationError);
            }

            initialize_thread_pool();
            initialize_ui_service();
            initialize_media_service();
            initialize_presence_service();
            connect_services();

            m_state = ApplicationState::Running;
            PLEX_LOG_INFO("Application", "Initialization complete");
            return {};

        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("Application", "Initialization failed: " + std::string(e.what()));
            m_state = ApplicationState::Error;
            return std::unexpected(ApplicationError::InitializationFailed);
        }
    }

    std::expected<void, ApplicationError> start() override {
        PLEX_LOG_INFO("Application", "Starting services...");

        if (m_state != ApplicationState::Running) {
            PLEX_LOG_ERROR("Application", "Not initialized");
            return std::unexpected(ApplicationError::InitializationFailed);
        }

        try {
            m_running = true;
            m_shutdown_requested = false;

            initialize_system_tray();
            start_services();

            PLEX_LOG_INFO("Application", "Services started");
            return {};

        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("Application", "Start failed: " + std::string(e.what()));
            return std::unexpected(ApplicationError::InitializationFailed);
        }
    }

    void stop() override {
        PLEX_LOG_INFO("Application", "Stopping...");
        m_state = ApplicationState::Stopping;
        m_running = false;
        m_shutdown_requested = true;
    }

    void shutdown() override {
        PLEX_LOG_INFO("Application", "Shutting down...");

        cleanup_event_subscriptions();
        wait_for_service_tasks();
        stop_services();

        if (m_thread_pool) {
            m_thread_pool->shutdown();
        }

        m_state = ApplicationState::Stopped;
        PLEX_LOG_INFO("Application", "Shutdown complete");
    }

    ApplicationState get_state() const override {
        return m_state;
    }

    bool is_running() const override {
        return m_running && !m_shutdown_requested;
    }

    void run() override {
        PLEX_LOG_INFO("Application", "Main loop started");
        while (is_running()) {
            run_once();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        PLEX_LOG_INFO("Application", "Main loop ended");
    }

    void run_once() override {
        if (m_ui_service) {
            m_ui_service->process_events();
        }
    }

    void quit() override {
        PLEX_LOG_INFO("Application", "Quitting");
        stop();
        shutdown();
    }

    services::MediaService& get_media_service() override {
        if (!m_media_service) throw std::runtime_error("MediaService not initialized");
        return *m_media_service;
    }

    services::PresenceService& get_presence_service() override {
        if (!m_presence_service) throw std::runtime_error("PresenceService not initialized");
        return *m_presence_service;
    }

    platform::UiService& get_ui_service() override {
        if (!m_ui_service) throw std::runtime_error("UiService not initialized");
        return *m_ui_service;
    }

    ConfigurationService& get_configuration_service() override {
        if (!m_config_service) throw std::runtime_error("ConfigurationService not initialized");
        return *m_config_service;
    }

    AuthenticationService& get_authentication_service() override {
        if (!m_auth_service) throw std::runtime_error("AuthenticationService not initialized");
        return *m_auth_service;
    }

    utils::ThreadPool& get_thread_pool() override {
        if (!m_thread_pool) throw std::runtime_error("ThreadPool not initialized");
        return *m_thread_pool;
    }

    utils::TaskScheduler& get_task_scheduler() override {
        if (!m_task_scheduler) throw std::runtime_error("TaskScheduler not initialized");
        return *m_task_scheduler;
    }

    EventBus& get_event_bus() override {
        if (!m_event_bus) throw std::runtime_error("EventBus not initialized");
        return *m_event_bus;
    }

private:
    bool initialize_configuration() {
        // Initialize configuration service
        m_config_service = ConfigurationService::create("", m_event_bus);
        if (!m_config_service) {
            PLEX_LOG_ERROR("Application", "Failed to create configuration service");
            m_state = ApplicationState::Error;
            return false;
        }

        if (!m_config_service->load()) {
            PLEX_LOG_WARNING("Application", "Using default configuration");
        }

        // Initialize authentication service
        m_auth_service = AuthenticationService::create();
        if (!m_auth_service) {
            PLEX_LOG_ERROR("Application", "Failed to create authentication service");
            m_state = ApplicationState::Error;
            return false;
        }

        return true;
    }

    void initialize_thread_pool() {
        m_thread_pool = std::make_unique<utils::ThreadPool>(4);
        m_task_scheduler = std::make_unique<utils::TaskScheduler>();
    }

    void initialize_ui_service() {
        auto ui_factory = platform::UiServiceFactory::create_default_factory();
        if (!ui_factory) {
            PLEX_LOG_WARNING("Application", "UI not supported on this platform");
            return;
        }

        m_ui_service = ui_factory->create_service();
        if (!m_ui_service) {
            PLEX_LOG_WARNING("Application", "UI service creation failed");
            return;
        }

        if (!m_ui_service->initialize()) {
            PLEX_LOG_WARNING("Application", "UI initialization failed");
            m_ui_service.reset();
            return;
        }

        PLEX_LOG_INFO("Application", "UI service initialized");
    }

    void initialize_media_service() {
        m_media_service = services::PlexServiceBuilder()
            .with_configuration_service(std::shared_ptr<ConfigurationService>(
                m_config_service.get(), [](ConfigurationService*){}))
            .with_authentication_service(std::shared_ptr<AuthenticationService>(
                m_auth_service.get(), [](AuthenticationService*){}))
            .build();

        if (m_media_service) {
            m_media_service->set_event_bus(m_event_bus);
            PLEX_LOG_INFO("Application", "Plex service initialized");
        }
    }

    void initialize_presence_service() {
        auto factory = services::PresenceServiceFactory::create_default_factory();
        if (!factory) {
            PLEX_LOG_WARNING("Application", "Presence factory creation failed");
            return;
        }

        m_presence_service = factory->create_service(
            services::PresenceServiceFactory::ServiceType::Discord,
            m_config_service->get()
        );

        if (m_presence_service) {
            m_presence_service->set_event_bus(m_event_bus);
            PLEX_LOG_INFO("Application", "Discord service initialized");
        }
    }

    void connect_services() {
        if (!m_media_service || !m_presence_service) {
            return;
        }

        auto subscription_id = m_event_bus->subscribe<events::MediaSessionUpdated>(
            [this](const events::MediaSessionUpdated& event) {
                PLEX_LOG_DEBUG("Application", "Updating Discord presence");
                if (!m_presence_service->update_from_media(event.current_info)) {
                    PLEX_LOG_WARNING("Application", "Discord update failed");
                }
            }
        );

        m_event_subscriptions.push_back(subscription_id);
        PLEX_LOG_INFO("Application", "Services connected");
    }

    void start_services() {
        if (m_media_service) {
            m_service_futures.push_back(
                m_thread_pool->submit([this]() {
                    if (!m_media_service->start()) {
                        PLEX_LOG_WARNING("Application", "Media service start failed");
                    } else {
                        PLEX_LOG_INFO("Application", "Media service started");
                    }
                })
            );
        }

        if (m_presence_service) {
            m_service_futures.push_back(
                m_thread_pool->submit([this]() {
                    if (!m_presence_service->initialize()) {
                        PLEX_LOG_WARNING("Application", "Presence service start failed");
                    } else {
                        PLEX_LOG_INFO("Application", "Presence service started");
                    }
                })
            );
        }
    }

    void cleanup_event_subscriptions() {
        if (!m_event_bus) return;

        for (auto id : m_event_subscriptions) {
            m_event_bus->unsubscribe(id);
        }
        m_event_subscriptions.clear();
    }

    void wait_for_service_tasks() {
        for (auto& future : m_service_futures) {
            if (future.valid()) {
                try {
                    future.wait();
                } catch (const std::exception& e) {
                    PLEX_LOG_WARNING("Application", "Task wait error: " + std::string(e.what()));
                }
            }
        }
        m_service_futures.clear();
    }

    void stop_services() {
        if (m_media_service) {
            m_media_service->stop();
            PLEX_LOG_INFO("Application", "Media service stopped");
        }

        if (m_presence_service) {
            m_presence_service->shutdown();
            PLEX_LOG_INFO("Application", "Presence service stopped");
        }

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
    }

    void initialize_system_tray() {
        if (!m_ui_service || !m_ui_service->supports_system_tray()) {
            return;
        }

        m_system_tray = m_ui_service->create_system_tray();
        if (!m_system_tray || !m_system_tray->initialize()) {
            PLEX_LOG_WARNING("Application", "System tray initialization failed");
            return;
        }

        m_system_tray->set_icon(":/icons/app_icon");
        m_system_tray->set_tooltip("Presence for Plex");
        setup_tray_menu();
        m_system_tray->show();
        PLEX_LOG_INFO("Application", "System tray created");
    }

    void setup_tray_menu() {
        if (!m_system_tray) return;

        std::vector<platform::MenuItem> menu_items;

        platform::MenuItem status_item;
        status_item.id = "status";
        status_item.label = "Status: Running";
        status_item.enabled = false;
        menu_items.push_back(status_item);

        platform::MenuItem separator;
        separator.type = platform::MenuItemType::Separator;
        menu_items.push_back(separator);

        platform::MenuItem settings_item;
        settings_item.id = "settings";
        settings_item.label = "Settings...";
        settings_item.enabled = false;
        menu_items.push_back(settings_item);

        menu_items.push_back(separator);

        platform::MenuItem exit_item;
        exit_item.id = "exit";
        exit_item.label = "Exit";
        exit_item.action = [this]() {
            PLEX_LOG_INFO("Application", "Exit from tray");
            quit();
        };
        menu_items.push_back(exit_item);

        if (!m_system_tray->set_menu(menu_items)) {
            PLEX_LOG_WARNING("Application", "Tray menu setup failed");
        }
    }

    std::atomic<ApplicationState> m_state;
    std::atomic<bool> m_running;
    std::atomic<bool> m_shutdown_requested;

    std::unique_ptr<ConfigurationService> m_config_service;
    std::unique_ptr<AuthenticationService> m_auth_service;
    std::unique_ptr<RuntimeState> m_runtime_state;
    std::unique_ptr<services::MediaService> m_media_service;
    std::unique_ptr<services::PresenceService> m_presence_service;
    std::unique_ptr<platform::UiService> m_ui_service;
    std::unique_ptr<platform::SystemTray> m_system_tray;
    std::unique_ptr<utils::ThreadPool> m_thread_pool;
    std::unique_ptr<utils::TaskScheduler> m_task_scheduler;
    std::shared_ptr<EventBus> m_event_bus;
    std::vector<EventBus::HandlerId> m_event_subscriptions;
    std::vector<std::future<void>> m_service_futures;
};

std::expected<std::unique_ptr<Application>, ApplicationError>
ApplicationFactory::create_default_application(const std::filesystem::path& config_path) {
    (void)config_path;
    PLEX_LOG_INFO("ApplicationFactory", "Creating application");

    try {
        return std::make_unique<ApplicationImpl>();
    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("ApplicationFactory", "Creation failed: " + std::string(e.what()));
        return std::unexpected(ApplicationError::InitializationFailed);
    }
}

std::expected<std::unique_ptr<Application>, ApplicationError>
ApplicationFactory::create_application_with_config(const ApplicationConfig& config) {
    (void)config;
    PLEX_LOG_INFO("ApplicationFactory", "Creating configured application");

    try {
        return std::make_unique<ApplicationImpl>();
    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("ApplicationFactory", "Creation failed: " + std::string(e.what()));
        return std::unexpected(ApplicationError::InitializationFailed);
    }
}

} // namespace core
} // namespace presence_for_plex
