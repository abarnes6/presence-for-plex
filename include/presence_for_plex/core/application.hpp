#pragma once

#include "presence_for_plex/core/models.hpp"
#include <memory>
#include <expected>
#include <functional>
#include <filesystem>
#include <string>

// Forward declarations
namespace presence_for_plex {
namespace core {
    class EventBus;
}

namespace services {
    class PlexService;
    class DiscordPresenceService;
    class PlexAuthStorage;
}

namespace platform {
    class UiService;
}
}

namespace presence_for_plex {
namespace core {

// Configuration manager
class ConfigManager {
public:
    explicit ConfigManager(const std::filesystem::path& config_path = {});
    ~ConfigManager();

    // Core operations
    std::expected<void, ConfigError> load();
    std::expected<void, ConfigError> save();

    // Configuration access
    const ApplicationConfig& get() const;
    std::expected<void, ConfigError> update(const ApplicationConfig& config);

    // Event notifications
    void set_event_bus(std::shared_ptr<EventBus> bus);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// Main application interface
class Application {
public:
    virtual ~Application() = default;

    // Lifecycle management
    virtual std::expected<void, ApplicationError> initialize() = 0;
    virtual std::expected<void, ApplicationError> start() = 0;
    virtual void stop() = 0;
    virtual void shutdown() = 0;

    // State management
    virtual ApplicationState get_state() const = 0;
    virtual bool is_running() const = 0;

    // Event loop
    virtual void run() = 0;
    virtual void run_once() = 0;
    virtual void quit() = 0;

    // Service access
    virtual std::expected<std::reference_wrapper<services::PlexService>, ApplicationError> get_media_service() = 0;
    virtual std::expected<std::reference_wrapper<services::DiscordPresenceService>, ApplicationError> get_presence_service() = 0;
    virtual std::expected<std::reference_wrapper<platform::UiService>, ApplicationError> get_ui_service() = 0;
    virtual std::expected<std::shared_ptr<ConfigManager>, ApplicationError> get_configuration_service() = 0;
    virtual std::expected<std::shared_ptr<const ConfigManager>, ApplicationError> get_configuration_service() const = 0;
    virtual std::expected<std::reference_wrapper<const ApplicationConfig>, ApplicationError> get_config() const = 0;
    virtual std::expected<std::reference_wrapper<services::PlexAuthStorage>, ApplicationError> get_authentication_service() = 0;

    // Additional features
    virtual void check_for_updates() = 0;

    // Event bus access
    virtual std::expected<std::reference_wrapper<EventBus>, ApplicationError> get_event_bus() = 0;
};

// Application creation
std::expected<std::unique_ptr<Application>, ApplicationError> create_application();

} // namespace core
} // namespace presence_for_plex
