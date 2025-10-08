#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/core/events.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <expected>
#include <vector>

namespace presence_for_plex {
namespace core {
    class ConfigurationService;
    class AuthenticationService;
}
namespace services {

using core::MediaInfo;
using core::PlexError;
using core::PlaybackState;
using core::ServerId;
using core::EventBus;

// Abstract interface for media services (Plex, Jellyfin, etc.)
class MediaService {
public:
    virtual ~MediaService() = default;

    // Lifecycle management
    virtual std::expected<void, PlexError> start() = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;

    // Configuration
    virtual void set_poll_interval(std::chrono::seconds interval) = 0;
    virtual std::chrono::seconds get_poll_interval() const = 0;

    // Event bus integration
    virtual void set_event_bus(std::shared_ptr<EventBus> bus) = 0;

    // Media information access
    virtual std::expected<MediaInfo, PlexError> get_current_media() const = 0;
    virtual std::expected<std::vector<MediaInfo>, PlexError> get_active_sessions() const = 0;

    // Server management
    virtual std::expected<void, PlexError> add_server(std::unique_ptr<core::PlexServer> server) = 0;
    virtual void remove_server(const ServerId& server_id) = 0;
    virtual std::vector<ServerId> get_connected_servers() const = 0;

    // Health monitoring
    virtual bool is_server_connected(const ServerId& server_id) const = 0;

protected:
    std::shared_ptr<EventBus> m_event_bus;

    // Event publishing helpers
    void publish_media_started(const MediaInfo& info, const ServerId& server_id);
    void publish_media_updated(const MediaInfo& old_info, const MediaInfo& new_info);
    void publish_media_ended(const core::SessionKey& key, const ServerId& server_id);
    void publish_server_connected(const ServerId& server_id, const std::string& name);
    void publish_server_disconnected(const ServerId& server_id, const std::string& reason);
    void publish_media_error(PlexError error, const std::string& message, const std::optional<ServerId>& server_id = std::nullopt);
};

// Factory for creating media service implementations
class MediaServiceFactory {
public:
    static std::expected<std::unique_ptr<MediaService>, core::ConfigError>
    create(core::MediaServiceType type,
           std::shared_ptr<core::ConfigurationService> config_service,
           std::shared_ptr<core::AuthenticationService> auth_service,
           std::shared_ptr<EventBus> event_bus);
};

} // namespace services
} // namespace presence_for_plex