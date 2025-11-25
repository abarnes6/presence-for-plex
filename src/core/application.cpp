#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/services/plex/plex.hpp"
#include "presence_for_plex/services/plex/plex_auth.hpp"
#include "presence_for_plex/services/plex/plex_sse.hpp"
#include "presence_for_plex/services/plex/metadata/tmdb.hpp"
#include "presence_for_plex/services/plex/metadata/jikan.hpp"
#include "presence_for_plex/services/discord/discord_presence_service.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/services/update_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#ifdef USE_QT_UI
#include "presence_for_plex/platform/qt/qt_settings_dialog.hpp"
#include "presence_for_plex/platform/qt/qt_ui_service.hpp"
#include <QDialog>
#endif
#include "presence_for_plex/platform/ui_service.hpp"
#include "presence_for_plex/platform/system_service.hpp"
#include "version.h"

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
        LOG_DEBUG("Application", "Application created");
    }

    ~ApplicationImpl() override {
        if (m_running) {
            stop();
            shutdown();
        }
        LOG_DEBUG("Application", "Application destroyed");
    }

    std::expected<void, ApplicationError> initialize() override {
        LOG_DEBUG("Application", "Initializing...");

        if (m_state != ApplicationState::NotInitialized) {
            LOG_WARNING("Application", "Already initialized");
            return std::unexpected(ApplicationError::AlreadyRunning);
        }

        m_state = ApplicationState::Initializing;

        try {
            if (!initialize_configuration()) {
                return std::unexpected(ApplicationError::ConfigurationError);
            }

            initialize_ui_service();
            initialize_plex_service();
            initialize_discord_service();
            initialize_update_service();
            connect_services();

            m_state = ApplicationState::Running;
            LOG_DEBUG("Application", "Initialization complete");
            return {};

        } catch (const std::exception& e) {
            LOG_ERROR("Application", "Initialization failed: " + std::string(e.what()));
            m_state = ApplicationState::Error;
            return std::unexpected(ApplicationError::InitializationFailed);
        }
    }

    std::expected<void, ApplicationError> start() override {
        LOG_DEBUG("Application", "Starting services...");

        if (m_state != ApplicationState::Running) {
            LOG_ERROR("Application", "Not initialized");
            return std::unexpected(ApplicationError::InitializationFailed);
        }

        try {
            m_running = true;
            m_shutdown_requested = false;

            initialize_system_tray();
            start_services();

            LOG_DEBUG("Application", "Services started");
            return {};

        } catch (const std::exception& e) {
            LOG_ERROR("Application", "Start failed: " + std::string(e.what()));
            return std::unexpected(ApplicationError::InitializationFailed);
        }
    }

    void stop() override {
        LOG_INFO("Application", "Stopping...");
        m_state = ApplicationState::Stopping;
        m_running = false;
        m_shutdown_requested = true;
    }

    void shutdown() override {
        LOG_INFO("Application", "Shutting down...");

        cleanup_event_subscriptions();
        wait_for_service_tasks();
        stop_services();

        m_state = ApplicationState::Stopped;
        LOG_INFO("Application", "Shutdown complete");
    }

    ApplicationState get_state() const override {
        return m_state;
    }

    bool is_running() const override {
        return m_running && !m_shutdown_requested;
    }

    void run() override {
        LOG_INFO("Application", "Main loop started");
        while (is_running()) {
            run_once();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        LOG_INFO("Application", "Main loop ended");
    }

    void run_once() override {
        if (m_ui_service) {
            m_ui_service->process_events();
        }
    }

    void quit() override {
        LOG_INFO("Application", "Quitting");
        stop();
        shutdown();
    }

    std::expected<std::reference_wrapper<services::Plex>, ApplicationError> get_plex_service() override {
        if (!m_plex_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_plex_service);
    }

    std::expected<std::reference_wrapper<services::DiscordPresenceService>, ApplicationError> get_discord_service() override {
        if (!m_discord_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_discord_service);
    }

    std::expected<std::reference_wrapper<platform::UiService>, ApplicationError> get_ui_service() override {
        if (!m_ui_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_ui_service);
    }

    std::expected<std::shared_ptr<ConfigManager>, ApplicationError> get_configuration_service() override {
        if (!m_config_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return m_config_service;
    }

    std::expected<std::shared_ptr<const ConfigManager>, ApplicationError> get_configuration_service() const override {
        if (!m_config_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return m_config_service;
    }

    std::expected<std::reference_wrapper<const ApplicationConfig>, ApplicationError> get_config() const override {
        if (!m_config_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(m_config_service->get());
    }

    std::expected<std::reference_wrapper<services::PlexAuth>, ApplicationError> get_authentication_service() override {
        if (!m_auth_service) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_auth_service);
    }

    std::expected<std::reference_wrapper<EventBus>, ApplicationError> get_event_bus() override {
        if (!m_event_bus) {
            return std::unexpected(ApplicationError::ServiceUnavailable);
        }
        return std::ref(*m_event_bus);
    }

    void check_for_updates() override {
        if (!m_update_service) {
            LOG_WARNING("Application", "Update service not available");
            return;
        }

        LOG_INFO("Application", "Checking for updates...");

        m_service_futures.push_back(std::async(std::launch::async, [this]() {
            auto result = m_update_service->check_for_updates();
            if (!result) {
                LOG_ERROR("Application", "Update check failed");
                return;
            }

            const auto& update_info = *result;
            if (update_info.update_available) {
                LOG_INFO("Application", "Update available: " + update_info.latest_version);
            } else {
                LOG_INFO("Application", "No updates available");
            }
        }));
    }

private:
    bool initialize_configuration() {
        // Initialize configuration manager
        m_config_service = std::make_shared<ConfigManager>();
        m_config_service->set_event_bus(m_event_bus);

        if (!m_config_service->load()) {
            LOG_WARNING("Application", "Using default configuration");
        }

        // Initialize authentication service (PlexAuth needs HttpClient)
        std::shared_ptr<services::HttpClient> http_client = services::create_http_client();
        m_auth_service = std::make_shared<services::PlexAuth>(http_client);
        if (!m_auth_service) {
            LOG_ERROR("Application", "Failed to create authentication service");
            m_state = ApplicationState::Error;
            return false;
        }

        return true;
    }

    void initialize_ui_service() {
        m_ui_service = std::make_unique<platform::qt::QtUiService>();
        if (!m_ui_service) {
            LOG_WARNING("Application", "UI service creation failed");
            return;
        }

        if (!m_ui_service->initialize()) {
            LOG_WARNING("Application", "UI initialization failed");
            m_ui_service.reset();
            return;
        }

        LOG_DEBUG("Application", "UI service initialized");
    }

    void initialize_plex_service() {
        const auto& config = m_config_service->get();

        if (!config.plex.enabled) {
            LOG_INFO("Application", "Plex disabled in configuration");
            return;
        }

        LOG_DEBUG("Application", "Initializing Plex");

        try {
            std::shared_ptr<services::HttpClient> http_client = services::create_http_client();

            // Create SSE manager
            auto sse = std::make_shared<services::PlexSSE>(http_client, m_auth_service);

            // Create Plex service
            m_plex_service = std::make_unique<services::Plex>(
                m_auth_service, sse, http_client, m_config_service
            );

            // Add metadata services
            if (!config.tmdb_access_token.empty()) {
                m_plex_service->add_metadata_service(
                    std::make_unique<services::TMDB>(http_client, config.tmdb_access_token));
            }
            m_plex_service->add_metadata_service(std::make_unique<services::Jikan>(http_client));

            m_plex_service->set_event_bus(m_event_bus);
            LOG_DEBUG("Application", "Plex initialized");
        } catch (const std::exception& e) {
            LOG_ERROR("Application", "Plex initialization failed: " + std::string(e.what()));
        }
    }

    void initialize_discord_service() {
        const auto& config = m_config_service->get();

        if (!config.discord.enabled) {
            LOG_INFO("Application", "Discord disabled in configuration");
            return;
        }

        auto service_result = services::DiscordPresenceService::create(config);

        if (service_result) {
            m_discord_service = std::move(*service_result);

            // Configure the service
            m_discord_service->set_show_buttons(config.discord.discord.show_buttons);
            m_discord_service->set_show_progress(config.discord.discord.show_progress);
            m_discord_service->set_show_artwork(config.discord.discord.show_artwork);

            // Set format templates
            m_discord_service->set_tv_details_format(config.discord.discord.tv_details_format);
            m_discord_service->set_tv_state_format(config.discord.discord.tv_state_format);
            m_discord_service->set_tv_large_image_text_format(config.discord.discord.tv_large_image_text_format);
            m_discord_service->set_movie_details_format(config.discord.discord.movie_details_format);
            m_discord_service->set_movie_state_format(config.discord.discord.movie_state_format);
            m_discord_service->set_movie_large_image_text_format(config.discord.discord.movie_large_image_text_format);
            m_discord_service->set_music_details_format(config.discord.discord.music_details_format);
            m_discord_service->set_music_state_format(config.discord.discord.music_state_format);
            m_discord_service->set_music_large_image_text_format(config.discord.discord.music_large_image_text_format);

            m_discord_service->set_event_bus(m_event_bus);
            LOG_DEBUG("Application", "Discord initialized");
        } else {
            LOG_ERROR("Application", "Discord initialization failed");
        }
    }

    void initialize_update_service() {
        // Create HTTP client for update service
        auto http_client = services::create_http_client();

        m_update_service = std::make_unique<services::GitHubUpdateService>(
            "abarnes6",
            "presence-for-plex",
            VERSION_STRING,
            std::move(http_client)
        );

        m_update_service->set_event_bus(m_event_bus);
        LOG_DEBUG("Application", "Update service initialized");
    }

    void connect_services() {
        if (!m_plex_service || !m_discord_service) {
            return;
        }

        // Subscribe to media session state changes
        auto media_sub = m_event_bus->subscribe<events::MediaSessionStateChanged>(
            [this](const events::MediaSessionStateChanged& event) {
                // Update Discord for Started and Updated states
                if (event.change_type != events::MediaSessionStateChanged::ChangeType::Ended) {
                    if (event.current_info) {
                        LOG_DEBUG("Application", "Updating Discord");
                        if (!m_discord_service->update_from_media(*event.current_info)) {
                            LOG_WARNING("Application", "Discord update failed");
                        }
                    }
                }
            }
        );
        m_event_subscriptions.push_back(media_sub);

        // Subscribe to configuration updates
        auto config_sub = m_event_bus->subscribe<events::ConfigurationUpdated>(
            [this](const events::ConfigurationUpdated& event) {
                LOG_INFO("Application", "Configuration updated");

                // Handle service enable/disable state changes
                handle_service_config_changes(event.previous_config, event.new_config);

                // Services that need runtime config updates can listen to this event
                // directly via the event bus, rather than coupling the application
                // to specific service implementations
            }
        );
        m_event_subscriptions.push_back(config_sub);

        LOG_DEBUG("Application", "Services connected");
    }

    void start_services() {
        if (m_plex_service) {
            m_service_futures.push_back(
                std::async(std::launch::async, [this]() {
                    if (!m_plex_service->start()) {
                        LOG_WARNING("Application", "Plex start failed");
                    } else {
                        LOG_DEBUG("Application", "Plex started");
                    }
                })
            );
        }

        if (m_discord_service) {
            m_service_futures.push_back(
                std::async(std::launch::async, [this]() {
                    if (!m_discord_service->initialize()) {
                        LOG_WARNING("Application", "Discord start failed");
                    } else {
                        LOG_DEBUG("Application", "Discord started");
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

    void handle_service_config_changes(const ApplicationConfig& old_config, const ApplicationConfig& new_config) {
        // Handle Plex enable/disable
        if (old_config.plex.enabled != new_config.plex.enabled) {
            if (new_config.plex.enabled) {
                LOG_INFO("Application", "Enabling Plex");
                initialize_plex_service();
                if (m_plex_service && m_running) {
                    m_service_futures.push_back(
                        std::async(std::launch::async, [this]() {
                            if (!m_plex_service->start()) {
                                LOG_WARNING("Application", "Plex start failed");
                            } else {
                                LOG_INFO("Application", "Plex started");
                            }
                        })
                    );
                }
            } else {
                LOG_INFO("Application", "Disabling Plex");
                if (m_plex_service) {
                    m_plex_service->stop();
                    m_plex_service.reset();
                }
            }
        }

        // Handle Discord enable/disable
        if (old_config.discord.enabled != new_config.discord.enabled) {
            if (new_config.discord.enabled) {
                LOG_INFO("Application", "Enabling Discord");
                initialize_discord_service();
                if (m_discord_service && m_running) {
                    m_service_futures.push_back(
                        std::async(std::launch::async, [this]() {
                            if (!m_discord_service->initialize()) {
                                LOG_WARNING("Application", "Discord start failed");
                            } else {
                                LOG_INFO("Application", "Discord started");
                            }
                        })
                    );
                }
            } else {
                LOG_INFO("Application", "Disabling Discord");
                if (m_discord_service) {
                    m_discord_service->shutdown();
                    m_discord_service.reset();
                }
            }
        }
    }

    void stop_services() {
        if (m_plex_service) {
            m_plex_service->stop();
            LOG_INFO("Application", "Plex stopped");
        }

        if (m_discord_service) {
            m_discord_service->shutdown();
            LOG_INFO("Application", "Discord stopped");
        }

        if (m_system_tray) {
            m_system_tray->hide();
            m_system_tray->shutdown();
            m_system_tray.reset();
            LOG_INFO("Application", "System tray stopped");
        }

        if (m_ui_service) {
            m_ui_service->shutdown();
            LOG_INFO("Application", "UI service stopped");
        }
    }

    void initialize_system_tray() {
        if (!m_ui_service || !m_ui_service->supports_system_tray()) {
            return;
        }

        m_system_tray = m_ui_service->create_system_tray();
        if (!m_system_tray || !m_system_tray->initialize()) {
            LOG_WARNING("Application", "System tray initialization failed");
            return;
        }

        (void)m_system_tray->set_icon(":/icons/app_icon");
        (void)m_system_tray->set_tooltip("Presence for Plex");
        setup_tray_menu();
        m_system_tray->show();
        LOG_DEBUG("Application", "System tray created");
    }

    void show_settings_dialog() {
        if (!m_config_service) {
            LOG_ERROR("Application", "Config service not available");
            return;
        }

        const auto& current_config = m_config_service->get();

        auto* dialog = new platform::qt::QtSettingsDialog(current_config);
        int result = dialog->exec();

        if (result == QDialog::Accepted) {
            auto new_config = dialog->get_config();

            // Handle autostart changes
            if (current_config.start_at_boot != new_config.start_at_boot) {
                auto autostart_manager = platform::AutostartManager::create("PresenceForPlex");

                if (new_config.start_at_boot) {
                    auto autostart_result = autostart_manager->enable_autostart();
                    if (!autostart_result) {
                        LOG_ERROR("Application", "Failed to enable autostart");
                    }
                } else {
                    auto autostart_result = autostart_manager->disable_autostart();
                    if (!autostart_result) {
                        LOG_ERROR("Application", "Failed to disable autostart");
                    }
                }
            }

            auto update_result = m_config_service->update(new_config);
            if (update_result) {
                LOG_INFO("Application", "Configuration updated successfully");
            } else {
                LOG_ERROR("Application", "Failed to update configuration");
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
            LOG_INFO("Application", "Opening settings dialog");
            show_settings_dialog();
        };
        menu_items.push_back(settings_item);

        platform::MenuItem update_item;
        update_item.id = "check_update";
        update_item.label = "Check for Updates";
        update_item.action = [this]() {
            LOG_INFO("Application", "Update check from tray");
            check_for_updates();
        };
        menu_items.push_back(update_item);

        menu_items.push_back(separator);

        platform::MenuItem exit_item;
        exit_item.id = "exit";
        exit_item.label = "Exit";
        exit_item.action = [this]() {
            LOG_INFO("Application", "Exit from tray");
            quit();
        };
        menu_items.push_back(exit_item);

        if (!m_system_tray->set_menu(menu_items)) {
            LOG_WARNING("Application", "Tray menu setup failed");
        }
    }

    std::atomic<ApplicationState> m_state;
    std::atomic<bool> m_running;
    std::atomic<bool> m_shutdown_requested;

    std::shared_ptr<ConfigManager> m_config_service;
    std::shared_ptr<services::PlexAuth> m_auth_service;
    std::unique_ptr<services::Plex> m_plex_service;
    std::unique_ptr<services::DiscordPresenceService> m_discord_service;
    std::unique_ptr<services::GitHubUpdateService> m_update_service;
    std::unique_ptr<platform::UiService> m_ui_service;
    std::unique_ptr<platform::SystemTray> m_system_tray;
    std::shared_ptr<EventBus> m_event_bus;
    std::vector<EventBus::HandlerId> m_event_subscriptions;
    std::vector<std::future<void>> m_service_futures;
};

std::expected<std::unique_ptr<Application>, ApplicationError> create_application() {
    LOG_DEBUG("Application", "Creating application");

    try {
        return std::make_unique<ApplicationImpl>();
    } catch (const std::exception& e) {
        LOG_ERROR("Application", "Creation failed: " + std::string(e.what()));
        return std::unexpected(ApplicationError::InitializationFailed);
    }
}

} // namespace core
} // namespace presence_for_plex
