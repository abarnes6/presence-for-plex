#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/services/plex/plex_service_impl.hpp"
#include "presence_for_plex/services/update_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/threading.hpp"
#include "presence_for_plex/platform/qt/qt_settings_dialog.hpp"
#include "version.h"

#include <QDialog>
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
            initialize_update_service();
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

    std::expected<std::reference_wrapper<services::MediaService>, ApplicationError> get_media_service() override {
        if (!m_media_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_media_service);
    }

    std::expected<std::reference_wrapper<services::PresenceService>, ApplicationError> get_presence_service() override {
        if (!m_presence_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_presence_service);
    }

    std::expected<std::reference_wrapper<platform::UiService>, ApplicationError> get_ui_service() override {
        if (!m_ui_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_ui_service);
    }

    std::expected<std::reference_wrapper<ConfigurationService>, ApplicationError> get_configuration_service() override {
        if (!m_config_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_config_service);
    }

    std::expected<std::reference_wrapper<const ConfigurationService>, ApplicationError> get_configuration_service() const override {
        if (!m_config_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_config_service);
    }

    std::expected<std::reference_wrapper<const ApplicationConfig>, ApplicationError> get_config() const override {
        if (!m_config_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(m_config_service->get());
    }

    std::expected<std::reference_wrapper<AuthenticationService>, ApplicationError> get_authentication_service() override {
        if (!m_auth_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_auth_service);
    }

    std::expected<std::reference_wrapper<utils::ThreadPool>, ApplicationError> get_thread_pool() override {
        if (!m_thread_pool) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_thread_pool);
    }

    std::expected<std::reference_wrapper<EventBus>, ApplicationError> get_event_bus() override {
        if (!m_event_bus) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_event_bus);
    }

    void check_for_updates() override {
        if (!m_update_service) {
            PLEX_LOG_WARNING("Application", "Update service not available");
            return;
        }

        PLEX_LOG_INFO("Application", "Checking for updates...");

        m_thread_pool->submit([this]() {
            auto result = m_update_service->check_for_updates();
            if (!result) {
                PLEX_LOG_ERROR("Application", "Update check failed");
                return;
            }

            const auto& update_info = *result;
            if (update_info.update_available) {
                PLEX_LOG_INFO("Application", "Update available: " + update_info.latest_version);
            } else {
                PLEX_LOG_INFO("Application", "No updates available");
            }
        });
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
    }

    void initialize_ui_service() {
        m_ui_service = platform::UiService::create_default();
        if (!m_ui_service) {
            PLEX_LOG_WARNING("Application", "UI not supported on this platform");
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

        auto service_result = factory->create_service(
            services::PresenceServiceFactory::ServiceType::Discord,
            m_config_service->get()
        );

        if (service_result) {
            m_presence_service = std::move(*service_result);
            m_presence_service->set_event_bus(m_event_bus);
            PLEX_LOG_INFO("Application", "Discord service initialized");
        } else {
            PLEX_LOG_ERROR("Application", "Discord service creation failed");
        }
    }

    void initialize_update_service() {
        m_update_service = services::UpdateServiceFactory::create_github_service(
            "abarnes6",
            "presence-for-plex",
            VERSION_STRING
        );

        if (m_update_service) {
            m_update_service->set_event_bus(m_event_bus);
            PLEX_LOG_INFO("Application", "Update service initialized");
        } else {
            PLEX_LOG_WARNING("Application", "Update service creation failed");
        }
    }

    void connect_services() {
        if (!m_media_service || !m_presence_service) {
            return;
        }

        // Subscribe to media session updates
        auto media_sub = m_event_bus->subscribe<events::MediaSessionUpdated>(
            [this](const events::MediaSessionUpdated& event) {
                PLEX_LOG_DEBUG("Application", "Updating Discord presence");
                if (!m_presence_service->update_from_media(event.current_info)) {
                    PLEX_LOG_WARNING("Application", "Discord update failed");
                }
            }
        );
        m_event_subscriptions.push_back(media_sub);

        // Subscribe to configuration updates
        auto config_sub = m_event_bus->subscribe<events::ConfigurationUpdated>(
            [](const events::ConfigurationUpdated& /* event */) {
                PLEX_LOG_INFO("Application", "Configuration updated");

                // Services that need runtime config updates can listen to this event
                // directly via the event bus, rather than coupling the application
                // to specific service implementations
            }
        );
        m_event_subscriptions.push_back(config_sub);

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
        // Don't block on futures during shutdown - services are already stopping
        // and waiting here adds unnecessary delay. Just clear the futures.
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

        (void)m_system_tray->set_icon(":/icons/app_icon");
        (void)m_system_tray->set_tooltip("Presence for Plex");
        setup_tray_menu();
        m_system_tray->show();
        PLEX_LOG_INFO("Application", "System tray created");
    }

    void show_settings_dialog() {
        if (!m_config_service) {
            PLEX_LOG_ERROR("Application", "Config service not available");
            return;
        }

        const auto& current_config = m_config_service->get();

        auto* dialog = new platform::qt::QtSettingsDialog(current_config);
        int result = dialog->exec();

        if (result == QDialog::Accepted) {
            auto new_config = dialog->get_config();

            auto update_result = m_config_service->update(new_config);
            if (update_result) {
                PLEX_LOG_INFO("Application", "Configuration updated successfully");
            } else {
                PLEX_LOG_ERROR("Application", "Failed to update configuration");
            }
        }

        dialog->deleteLater();
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
        settings_item.enabled = true;
        settings_item.action = [this]() {
            PLEX_LOG_INFO("Application", "Opening settings dialog");
            show_settings_dialog();
        };
        menu_items.push_back(settings_item);

        platform::MenuItem update_item;
        update_item.id = "check_update";
        update_item.label = "Check for Updates";
        update_item.action = [this]() {
            PLEX_LOG_INFO("Application", "Update check from tray");
            check_for_updates();
        };
        menu_items.push_back(update_item);

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
    std::unique_ptr<services::MediaService> m_media_service;
    std::unique_ptr<services::PresenceService> m_presence_service;
    std::unique_ptr<services::UpdateService> m_update_service;
    std::unique_ptr<platform::UiService> m_ui_service;
    std::unique_ptr<platform::SystemTray> m_system_tray;
    std::unique_ptr<utils::ThreadPool> m_thread_pool;
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
