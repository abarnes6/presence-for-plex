#pragma once

#include "presence_for_plex/core/models.hpp"
#include <functional>
#include <memory>
#include <expected>

namespace presence_for_plex {
namespace services {

using core::MediaInfo;
using core::PlexError;
using core::PlaybackState;
using core::ServerId;

// Callback types for media events
using MediaStateCallback = std::function<void(const MediaInfo&)>;
using MediaErrorCallback = std::function<void(PlexError, const std::string&)>;
using MediaConnectionStateCallback = std::function<void(const ServerId&, bool)>;

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

    // Event callbacks
    virtual void set_media_state_callback(MediaStateCallback callback) = 0;
    virtual void set_error_callback(MediaErrorCallback callback) = 0;
    virtual void set_connection_callback(MediaConnectionStateCallback callback) = 0;

    // Media information access
    virtual std::expected<MediaInfo, PlexError> get_current_media() const = 0;
    virtual std::expected<std::vector<MediaInfo>, PlexError> get_active_sessions() const = 0;

    // Server management
    virtual std::expected<void, PlexError> add_server(std::unique_ptr<core::PlexServer> server) = 0;
    virtual void remove_server(const ServerId& server_id) = 0;
    virtual std::vector<ServerId> get_connected_servers() const = 0;

    // Health monitoring
    virtual bool is_server_connected(const ServerId& server_id) const = 0;
    virtual std::expected<void, PlexError> test_connection(const ServerId& server_id) = 0;

protected:
    // Template method pattern for subclasses
    virtual void on_media_state_changed(const MediaInfo& old_state, const MediaInfo& new_state) = 0;
    virtual void on_connection_state_changed(const ServerId& server_id, bool connected) = 0;
    virtual void on_error_occurred(PlexError error, const std::string& message) = 0;
};

} // namespace services
} // namespace presence_for_plex