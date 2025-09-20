#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/services/media_service.hpp"
#include "presence_for_plex/services/presence_service.hpp"
#include "presence_for_plex/platform/ui_service.hpp"
#include "presence_for_plex/platform/system_service.hpp"
#include "presence_for_plex/utils/threading.hpp"

#include <memory>
#include <atomic>
#include <unordered_map>

namespace presence_for_plex {
namespace core {

// Application error types
enum class ApplicationError {
    InitializationFailed,
    ServiceUnavailable,
    ConfigurationError,
    AlreadyRunning,
    ShutdownFailed
};

// Application state
enum class ApplicationState {
    NotInitialized,
    Initializing,
    Running,
    Stopping,
    Stopped,
    Error
};

// Configuration service interface
class ConfigurationService {
public:
    virtual ~ConfigurationService() = default;

    // Configuration management
    virtual std::expected<void, ConfigError> load_configuration() = 0;
    virtual std::expected<void, ConfigError> save_configuration() = 0;
    virtual std::expected<void, ConfigError> reload_configuration() = 0;

    // Configuration access
    virtual const ApplicationConfig& get_config() const = 0;
    virtual std::expected<void, ConfigError> update_config(const ApplicationConfig& config) = 0;

    // Validation
    virtual std::expected<void, ConfigError> validate_config(const ApplicationConfig& config) const = 0;

    // Change notifications
    using ConfigChangeCallback = std::function<void(const ApplicationConfig&)>;
    virtual void set_change_callback(ConfigChangeCallback callback) = 0;

    // Plex-specific configuration methods
    virtual std::string get_plex_auth_token() const = 0;
    virtual void set_plex_auth_token(const std::string& token) = 0;
    virtual std::string get_plex_client_identifier() const = 0;
    virtual std::string get_plex_username() const = 0;
    virtual void set_plex_username(const std::string& username) = 0;

    // Server management methods
    virtual const std::unordered_map<std::string, std::unique_ptr<PlexServer>>& get_plex_servers() const = 0;

    // Factory method
    static std::unique_ptr<ConfigurationService> create(const std::filesystem::path& config_path);
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
    virtual services::MediaService& get_media_service() = 0;
    virtual services::PresenceService& get_presence_service() = 0;
    virtual platform::UiService& get_ui_service() = 0;
    virtual ConfigurationService& get_configuration_service() = 0;

    // Utilities
    virtual utils::ThreadPool& get_thread_pool() = 0;
    virtual utils::TaskScheduler& get_task_scheduler() = 0;
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
