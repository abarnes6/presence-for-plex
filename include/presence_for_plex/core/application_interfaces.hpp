#pragma once

#include "presence_for_plex/core/models.hpp"
#include <memory>
#include <atomic>
#include <expected>
#include <expected>
#include <typeindex>

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

    virtual std::expected<std::reference_wrapper<services::MediaService>, ApplicationError> get_media_service() = 0;
    virtual std::expected<std::reference_wrapper<services::PresenceService>, ApplicationError> get_presence_service() = 0;
    virtual std::expected<std::reference_wrapper<const services::MediaService>, ApplicationError> get_media_service() const = 0;
    virtual std::expected<std::reference_wrapper<const services::PresenceService>, ApplicationError> get_presence_service() const = 0;
};

// 3. Utility provider interface
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
                   public IUtilityProvider {
public:
    virtual ~Application() = default;

    // Configuration access
    virtual ConfigurationService& get_configuration_service() = 0;
    virtual const ConfigurationService& get_configuration_service() const = 0;
    virtual const ApplicationConfig& get_config() const = 0;

    // Additional methods that don't fit into the segregated interfaces
    virtual void set_signal_handler(std::function<void(int)> handler) = 0;
    virtual void handle_error(const std::string& context, const std::exception& e) = 0;
};

// Dependency injection container interface
class IDependencyContainer {
public:
    virtual ~IDependencyContainer() = default;

    template<typename Interface>
    void register_service(std::shared_ptr<Interface> service) {
        register_service_impl(std::type_index(typeid(Interface)), std::move(service));
    }

    template<typename Interface>
    std::shared_ptr<Interface> resolve() {
        auto resolved = resolve_impl(std::type_index(typeid(Interface)));
        return std::static_pointer_cast<Interface>(resolved);
    }

    template<typename Interface>
    bool is_registered() const {
        return is_registered_impl(std::type_index(typeid(Interface)));
    }

protected:
    virtual void register_service_impl(std::type_index type, std::shared_ptr<void> service) = 0;
    virtual std::shared_ptr<void> resolve_impl(std::type_index type) = 0;
    virtual bool is_registered_impl(std::type_index type) const = 0;
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