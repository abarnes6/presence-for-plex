#pragma once
#define PRESENCE_FOR_PLEX_EVENTS_HPP_INCLUDED

#include "presence_for_plex/core/events_fwd.hpp"
#include "presence_for_plex/core/models.hpp"
#include <chrono>
#include <optional>
#include <string>

namespace presence_for_plex::core {
// Forward declarations
enum class ApplicationState {
    NotInitialized,
    Initializing,
    Running,
    Stopping,
    Stopped,
    Error
};

enum class ApplicationError {
    InitializationFailed,
    ServiceUnavailable,
    ConfigurationError,
    AlreadyRunning,
    ShutdownFailed
};
}

namespace presence_for_plex::core::events {

using core::ApplicationState;
using core::ApplicationError;
using core::ConfigError;
using core::PlexError;
using core::DiscordError;
using core::MediaInfo;
using core::ApplicationConfig;
using core::SessionKey;
using core::ServerId;

struct ConfigurationUpdated : Event {
    ApplicationConfig previous_config;
    ApplicationConfig new_config;

    ConfigurationUpdated(ApplicationConfig prev, ApplicationConfig curr)
        : previous_config(std::move(prev)), new_config(std::move(curr)) {}
};

struct ConfigurationError : Event {
    ConfigError error;
    std::string message;

    ConfigurationError(ConfigError err, std::string msg)
        : error(err), message(std::move(msg)) {}
};

struct MediaSessionStarted : Event {
    MediaInfo media_info;
    ServerId server_id;

    MediaSessionStarted(MediaInfo info, ServerId id)
        : media_info(std::move(info)), server_id(std::move(id)) {}
};

struct MediaSessionUpdated : Event {
    MediaInfo previous_info;
    MediaInfo current_info;

    MediaSessionUpdated(MediaInfo prev, MediaInfo curr)
        : previous_info(std::move(prev)), current_info(std::move(curr)) {}
};

struct MediaSessionEnded : Event {
    SessionKey session_key;
    ServerId server_id;

    MediaSessionEnded(SessionKey key, ServerId id)
        : session_key(std::move(key)), server_id(std::move(id)) {}
};

struct MediaPlaybackPaused : Event {
    MediaInfo media_info;

    explicit MediaPlaybackPaused(MediaInfo info)
        : media_info(std::move(info)) {}
};

struct MediaPlaybackResumed : Event {
    MediaInfo media_info;

    explicit MediaPlaybackResumed(MediaInfo info)
        : media_info(std::move(info)) {}
};

struct MediaError : Event {
    PlexError error;
    std::string message;
    std::optional<ServerId> server_id;

    MediaError(PlexError err, std::string msg, std::optional<ServerId> id = std::nullopt)
        : error(err), message(std::move(msg)), server_id(std::move(id)) {}
};

struct ServerConnectionEstablished : Event {
    ServerId server_id;
    std::string server_name;

    ServerConnectionEstablished(ServerId id, std::string name)
        : server_id(std::move(id)), server_name(std::move(name)) {}
};

struct ServerConnectionLost : Event {
    ServerId server_id;
    std::string reason;

    ServerConnectionLost(ServerId id, std::string r)
        : server_id(std::move(id)), reason(std::move(r)) {}
};

struct ServerReconnecting : Event {
    ServerId server_id;
    int attempt_number;
    std::chrono::seconds next_retry_in;

    ServerReconnecting(ServerId id, int attempt, std::chrono::seconds retry)
        : server_id(std::move(id)), attempt_number(attempt), next_retry_in(retry) {}
};


struct PresenceCleared : Event {
    std::string reason;

    explicit PresenceCleared(std::string r = "")
        : reason(std::move(r)) {}
};

struct DiscordConnected : Event {
    std::string client_id;

    explicit DiscordConnected(std::string id)
        : client_id(std::move(id)) {}
};

struct DiscordDisconnected : Event {
    std::string reason;
    bool will_retry;

    DiscordDisconnected(std::string r, bool retry)
        : reason(std::move(r)), will_retry(retry) {}
};

struct DiscordErrorEvent : Event {
    DiscordError error;
    std::string message;

    DiscordErrorEvent(DiscordError err, std::string msg)
        : error(err), message(std::move(msg)) {}
};

struct ApplicationStateChanged : Event {
    ApplicationState previous_state;
    ApplicationState current_state;

    ApplicationStateChanged(ApplicationState prev, ApplicationState curr)
        : previous_state(prev), current_state(curr) {}
};

struct ApplicationStarting : Event {
    std::string version;

    explicit ApplicationStarting(std::string ver)
        : version(std::move(ver)) {}
};

struct ApplicationReady : Event {
    std::chrono::milliseconds startup_time;

    explicit ApplicationReady(std::chrono::milliseconds time)
        : startup_time(time) {}
};

struct ApplicationShuttingDown : Event {
    std::string reason;

    explicit ApplicationShuttingDown(std::string r = "User requested")
        : reason(std::move(r)) {}
};

struct ApplicationErrorEvent : Event {
    ApplicationError error;
    std::string message;
    bool fatal;

    ApplicationErrorEvent(ApplicationError err, std::string msg, bool is_fatal = false)
        : error(err), message(std::move(msg)), fatal(is_fatal) {}
};

struct ServiceRegistered : Event {
    std::string service_name;
    std::string service_type;

    ServiceRegistered(std::string name, std::string type)
        : service_name(std::move(name)), service_type(std::move(type)) {}
};

struct ServiceInitialized : Event {
    std::string service_name;

    explicit ServiceInitialized(std::string name)
        : service_name(std::move(name)) {}
};

struct ServiceError : Event {
    std::string service_name;
    std::string error_message;
    bool recoverable;

    ServiceError(std::string name, std::string msg, bool recover = true)
        : service_name(std::move(name)), error_message(std::move(msg)), recoverable(recover) {}
};

struct AuthenticationRequired : Event {
    std::string service_name;
    std::string auth_url;

    AuthenticationRequired(std::string name, std::string url)
        : service_name(std::move(name)), auth_url(std::move(url)) {}
};

struct AuthenticationSucceeded : Event {
    std::string service_name;
    std::string user_identifier;

    AuthenticationSucceeded(std::string name, std::string user)
        : service_name(std::move(name)), user_identifier(std::move(user)) {}
};

struct AuthenticationFailed : Event {
    std::string service_name;
    std::string reason;

    AuthenticationFailed(std::string name, std::string r)
        : service_name(std::move(name)), reason(std::move(r)) {}
};


struct HealthCheckSucceeded : Event {
    std::string service_name;
    std::chrono::milliseconds response_time;

    HealthCheckSucceeded(std::string name, std::chrono::milliseconds time)
        : service_name(std::move(name)), response_time(time) {}
};

struct HealthCheckFailed : Event {
    std::string service_name;
    std::string reason;

    HealthCheckFailed(std::string name, std::string r)
        : service_name(std::move(name)), reason(std::move(r)) {}
};

}