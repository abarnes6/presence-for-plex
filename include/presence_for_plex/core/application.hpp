#pragma once

#include "presence_for_plex/utils/expected.hpp"
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

// Event system interface
template<typename EventType>
class EventBus {
public:
    using EventHandler = std::function<void(const EventType&)>;
    using SubscriptionId = std::uint64_t;

    virtual ~EventBus() = default;

    virtual SubscriptionId subscribe(EventHandler handler) = 0;
    virtual void unsubscribe(SubscriptionId id) = 0;
    virtual void publish(const EventType& event) = 0;
    virtual void clear() = 0;

    template<typename T>
    static std::unique_ptr<EventBus<T>> create();
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
    virtual void add_plex_server(const std::string& name, const std::string& client_id,
                               const std::string& local_uri, const std::string& public_uri,
                               const std::string& access_token, bool owned) = 0;
    virtual void clear_plex_servers() = 0;
    virtual void set_plex_servers(const std::vector<core::ServerId>& server_ids) = 0;
    virtual const std::unordered_map<std::string, std::unique_ptr<PlexServer>>& get_plex_servers() const = 0;

    // Factory method
    static std::unique_ptr<ConfigurationService> create(const std::filesystem::path& config_path);
};

// Lifecycle interface for managing component startup/shutdown
class LifecycleManager {
public:
    virtual ~LifecycleManager() = default;

    enum class Phase {
        Initialize,
        Start,
        Stop,
        Shutdown
    };

    using LifecycleCallback = std::function<std::expected<void, std::error_code>()>;

    virtual void register_callback(Phase phase, int priority, LifecycleCallback callback) = 0;
    virtual std::expected<void, std::error_code> execute_phase(Phase phase) = 0;

    static std::unique_ptr<LifecycleManager> create();
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

    // Event system access
    virtual EventBus<MediaStateChangedEvent>& get_media_event_bus() = 0;
    virtual EventBus<DiscordConnectionEvent>& get_discord_event_bus() = 0;
    virtual EventBus<PlexConnectionEvent>& get_plex_event_bus() = 0;
    virtual EventBus<ApplicationStartedEvent>& get_app_event_bus() = 0;

    // Utilities
    virtual utils::ThreadPool& get_thread_pool() = 0;
    virtual utils::TaskScheduler& get_task_scheduler() = 0;

protected:
    // Template method pattern hooks
    virtual void on_application_started() = 0;
    virtual void on_application_stopping() = 0;
    virtual void on_media_state_changed(const MediaStateChangedEvent& event) = 0;
    virtual void on_discord_connection_changed(const DiscordConnectionEvent& event) = 0;
    virtual void on_plex_connection_changed(const PlexConnectionEvent& event) = 0;
    virtual void on_error_occurred(std::error_code error, const std::string& message) = 0;
};

// Application builder for dependency injection
class ApplicationBuilder {
public:
    ApplicationBuilder() = default;

    // Service injection
    ApplicationBuilder& with_media_service(std::unique_ptr<services::MediaService> service);
    ApplicationBuilder& with_presence_service(std::unique_ptr<services::PresenceService> service);
    ApplicationBuilder& with_ui_service(std::unique_ptr<platform::UiService> service);
    ApplicationBuilder& with_configuration_service(std::unique_ptr<ConfigurationService> service);

    // Configuration
    ApplicationBuilder& with_config_path(const std::filesystem::path& path);
    ApplicationBuilder& with_thread_pool_size(size_t size);
    ApplicationBuilder& with_log_level(const std::string& level);

    // Factory methods
    ApplicationBuilder& use_default_services();
    ApplicationBuilder& use_mock_services(); // For testing

    // Build the application
    std::expected<std::unique_ptr<Application>, ApplicationError> build();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
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

    static std::unique_ptr<ApplicationBuilder> create_builder();
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
