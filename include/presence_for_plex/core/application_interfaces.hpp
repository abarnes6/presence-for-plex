#pragma once

#include "presence_for_plex/core/models.hpp"
#include <memory>
#include <atomic>

namespace presence_for_plex {
namespace core {

// Forward declarations
namespace services {
    class MediaService;
    class PresenceService;
}
namespace utils {
    class ThreadPool;
    class TaskScheduler;
}

// Split interfaces following Interface Segregation Principle

// 1. Lifecycle management interface
class IApplicationLifecycle {
public:
    virtual ~IApplicationLifecycle() = default;

    virtual std::expected<void, ApplicationError> initialize() = 0;
    virtual std::expected<void, ApplicationError> start() = 0;
    virtual void stop() = 0;
    virtual void shutdown() = 0;

    virtual ApplicationState get_state() const = 0;
    virtual bool is_running() const = 0;
};

// 2. Service provider interface
class IServiceProvider {
public:
    virtual ~IServiceProvider() = default;

    virtual services::MediaService& get_media_service() = 0;
    virtual services::PresenceService& get_presence_service() = 0;
    virtual const services::MediaService& get_media_service() const = 0;
    virtual const services::PresenceService& get_presence_service() const = 0;
};

// 3. Configuration provider interface
class IConfigurationProvider {
public:
    virtual ~IConfigurationProvider() = default;

    virtual ConfigurationService& get_configuration_service() = 0;
    virtual const ConfigurationService& get_configuration_service() const = 0;
    virtual const ApplicationConfig& get_config() const = 0;
};

// 5. Utility provider interface
class IUtilityProvider {
public:
    virtual ~IUtilityProvider() = default;

    virtual utils::ThreadPool& get_thread_pool() = 0;
    virtual utils::TaskScheduler& get_task_scheduler() = 0;
};

// Main application interface that combines the segregated interfaces
// Clients can depend only on the interfaces they need
class Application : public IApplicationLifecycle,
                   public IServiceProvider,
                   public IConfigurationProvider,
                   public IUtilityProvider {
public:
    virtual ~Application() = default;

    // Additional methods that don't fit into the segregated interfaces
    virtual void set_signal_handler(std::function<void(int)> handler) = 0;
    virtual void handle_error(const std::string& context, const std::exception& e) = 0;
};

// Dependency injection container interface
class IDependencyContainer {
public:
    virtual ~IDependencyContainer() = default;

    template<typename Interface>
    virtual void register_service(std::shared_ptr<Interface> service) = 0;

    template<typename Interface>
    virtual std::shared_ptr<Interface> resolve() = 0;

    template<typename Interface>
    virtual bool is_registered() const = 0;
};

// Factory for creating dependency-injected services
class IServiceFactory {
public:
    virtual ~IServiceFactory() = default;

    template<typename Service, typename... Args>
    virtual std::unique_ptr<Service> create(Args&&... args) = 0;
};

} // namespace core
} // namespace presence_for_plex