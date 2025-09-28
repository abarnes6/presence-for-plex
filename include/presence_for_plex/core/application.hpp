#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/core/authentication_service.hpp"
#include "presence_for_plex/core/runtime_state.hpp"
#include "presence_for_plex/services/media_service.hpp"
#include "presence_for_plex/services/presence_service.hpp"
#include "presence_for_plex/platform/ui_service.hpp"
#include "presence_for_plex/platform/system_service.hpp"
#include "presence_for_plex/utils/threading.hpp"

#include <memory>
#include <atomic>
#include <unordered_map>
#include <expected>
#include <functional>
#include <optional>
#include <filesystem>
#include <string>

namespace presence_for_plex {
namespace core {

// Application error types - defined in events.hpp if included
#ifndef PRESENCE_FOR_PLEX_EVENTS_HPP_INCLUDED
enum class ApplicationError {
    InitializationFailed,
    ServiceUnavailable,
    ConfigurationError,
    AlreadyRunning,
    ShutdownFailed
};

enum class ApplicationState {
    NotInitialized,
    Initializing,
    Running,
    Stopping,
    Stopped,
    Error
};
#endif

// Configuration service interface - simplified for clarity
class ConfigurationService {
public:
    virtual ~ConfigurationService() = default;

    // Core operations
    virtual std::expected<void, ConfigError> load() = 0;
    virtual std::expected<void, ConfigError> save() = 0;

    // Configuration access
    virtual const ApplicationConfig& get() const = 0;
    virtual std::expected<void, ConfigError> update(const ApplicationConfig& config) = 0;

    // Event notifications
    virtual void set_event_bus(std::shared_ptr<EventBus> bus) = 0;

    // Factory
    static std::unique_ptr<ConfigurationService> create(
        const std::filesystem::path& config_path = {},
        std::shared_ptr<EventBus> event_bus = nullptr
    );
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
    virtual std::expected<std::reference_wrapper<services::MediaService>, ApplicationError> get_media_service() = 0;
    virtual std::expected<std::reference_wrapper<services::PresenceService>, ApplicationError> get_presence_service() = 0;
    virtual std::expected<std::reference_wrapper<platform::UiService>, ApplicationError> get_ui_service() = 0;
    virtual std::expected<std::reference_wrapper<ConfigurationService>, ApplicationError> get_configuration_service() = 0;
    virtual std::expected<std::reference_wrapper<const ConfigurationService>, ApplicationError> get_configuration_service() const = 0;
    virtual std::expected<std::reference_wrapper<const ApplicationConfig>, ApplicationError> get_config() const = 0;
    virtual std::expected<std::reference_wrapper<AuthenticationService>, ApplicationError> get_authentication_service() = 0;

    // Event bus access
    virtual std::expected<std::reference_wrapper<EventBus>, ApplicationError> get_event_bus() = 0;

    // Utilities
    virtual std::expected<std::reference_wrapper<utils::ThreadPool>, ApplicationError> get_thread_pool() = 0;
    virtual std::expected<std::reference_wrapper<utils::TaskScheduler>, ApplicationError> get_task_scheduler() = 0;
};

// Application factory
class ApplicationFactory {
public:
    virtual ~ApplicationFactory() = default;

    // Factory methods
    static std::expected<std::unique_ptr<Application>, ApplicationError> create_default_application(
        const std::filesystem::path& config_path = {}
    );

    static std::expected<std::unique_ptr<Application>, ApplicationError> create_application_with_config(
        const ApplicationConfig& config
    );
};

// Dependency injection container interface
class ServiceContainer {
public:
    virtual ~ServiceContainer() = default;

    // Service registration
    template<typename Interface, typename Implementation, typename... Args>
    void register_singleton(Args&&... args);

    template<typename Interface, typename Implementation, typename... Args>
    void register_transient(Args&&... args);

    template<typename Interface>
    void register_instance(std::shared_ptr<Interface> instance);

    // Service resolution
    template<typename Interface>
    std::shared_ptr<Interface> resolve();

    template<typename Interface>
    std::optional<std::shared_ptr<Interface>> try_resolve();

    // Container management
    virtual void clear() = 0;
    virtual bool contains(const std::type_info& type) const = 0;

    // Factory method
    static std::unique_ptr<ServiceContainer> create();
};

} // namespace core
} // namespace presence_for_plex
