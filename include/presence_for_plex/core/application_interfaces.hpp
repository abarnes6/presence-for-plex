#pragma once

#include "presence_for_plex/core/models.hpp"
#include <memory>
#include <atomic>
#include <expected>
#include <functional>

namespace presence_for_plex {
namespace core {

// Forward declarations
namespace services {
    class MediaService;
    class PresenceService;
}
namespace utils {
    class ThreadPool;
}

class EventBus;
class ConfigurationService;
class AuthenticationService;

// Main application interface
class Application {
public:
    virtual ~Application() = default;

    // Lifecycle management
    virtual std::expected<void, ApplicationError> initialize() = 0;
    virtual std::expected<void, ApplicationError> start() = 0;
    virtual void stop() = 0;
    virtual void shutdown() = 0;

    virtual ApplicationState get_state() const = 0;
    virtual bool is_running() const = 0;

    // Service access
    virtual std::expected<std::reference_wrapper<services::MediaService>, ApplicationError> get_media_service() = 0;
    virtual std::expected<std::reference_wrapper<services::PresenceService>, ApplicationError> get_presence_service() = 0;
    virtual std::expected<std::reference_wrapper<const services::MediaService>, ApplicationError> get_media_service() const = 0;
    virtual std::expected<std::reference_wrapper<const services::PresenceService>, ApplicationError> get_presence_service() const = 0;

    // Utilities
    virtual utils::ThreadPool& get_thread_pool() = 0;

    // Configuration access
    virtual ConfigurationService& get_configuration_service() = 0;
    virtual const ConfigurationService& get_configuration_service() const = 0;
    virtual const ApplicationConfig& get_config() const = 0;

    // Additional methods
    virtual void set_signal_handler(std::function<void(int)> handler) = 0;
    virtual void handle_error(const std::string& context, const std::exception& e) = 0;
};

} // namespace core
} // namespace presence_for_plex