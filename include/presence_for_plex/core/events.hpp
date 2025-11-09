#pragma once

#include "presence_for_plex/core/models.hpp"
#include <chrono>
#include <optional>
#include <string>

namespace presence_for_plex::core::events {

struct Event {
    std::chrono::steady_clock::time_point timestamp{std::chrono::steady_clock::now()};
    Event() = default;
    virtual ~Event() = default;
};

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

// Consolidated media session event
struct MediaSessionStateChanged : Event {
    enum class ChangeType {
        Started,
        Updated,
        Ended
    };

    ChangeType change_type;
    std::optional<MediaInfo> current_info;   // nullopt for Ended
    std::optional<MediaInfo> previous_info;  // only for Updated
    SessionKey session_key;
    ServerId server_id;

    // Constructor for Started
    static MediaSessionStateChanged started(MediaInfo info, ServerId id) {
        MediaSessionStateChanged event;
        event.change_type = ChangeType::Started;
        event.current_info = std::move(info);
        event.session_key = event.current_info->session_key;
        event.server_id = std::move(id);
        return event;
    }

    // Constructor for Updated
    static MediaSessionStateChanged updated(MediaInfo prev, MediaInfo curr) {
        MediaSessionStateChanged event;
        event.change_type = ChangeType::Updated;
        event.previous_info = std::move(prev);
        event.current_info = std::move(curr);
        event.session_key = event.current_info->session_key;
        event.server_id = event.current_info->server_id;
        return event;
    }

    // Constructor for Ended
    static MediaSessionStateChanged ended(SessionKey key, ServerId id) {
        MediaSessionStateChanged event;
        event.change_type = ChangeType::Ended;
        event.session_key = std::move(key);
        event.server_id = std::move(id);
        return event;
    }

private:
    MediaSessionStateChanged() = default;
};

struct MediaError : Event {
    PlexError error;
    std::string message;
    std::optional<ServerId> server_id;

    MediaError(PlexError err, std::string msg, std::optional<ServerId> id = std::nullopt)
        : error(err), message(std::move(msg)), server_id(std::move(id)) {}
};

// Consolidated server connection event
struct ServerConnectionStateChanged : Event {
    enum class ConnectionState {
        Established,
        Lost,
        Reconnecting
    };

    ConnectionState state;
    ServerId server_id;
    std::string server_name;              // for Established
    std::string reason;                   // for Lost
    int attempt_number = 0;               // for Reconnecting
    std::chrono::seconds next_retry_in{}; // for Reconnecting

    // Constructor for Established
    static ServerConnectionStateChanged established(ServerId id, std::string name) {
        ServerConnectionStateChanged event;
        event.state = ConnectionState::Established;
        event.server_id = std::move(id);
        event.server_name = std::move(name);
        return event;
    }

    // Constructor for Lost
    static ServerConnectionStateChanged lost(ServerId id, std::string reason_msg) {
        ServerConnectionStateChanged event;
        event.state = ConnectionState::Lost;
        event.server_id = std::move(id);
        event.reason = std::move(reason_msg);
        return event;
    }

    // Constructor for Reconnecting
    static ServerConnectionStateChanged reconnecting(ServerId id, int attempt, std::chrono::seconds retry) {
        ServerConnectionStateChanged event;
        event.state = ConnectionState::Reconnecting;
        event.server_id = std::move(id);
        event.attempt_number = attempt;
        event.next_retry_in = retry;
        return event;
    }

private:
    ServerConnectionStateChanged() = default;
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

struct ServiceError : Event {
    std::string service_name;
    std::string error_message;
    bool recoverable;

    ServiceError(std::string name, std::string msg, bool recover = true)
        : service_name(std::move(name)), error_message(std::move(msg)), recoverable(recover) {}
};

// Consolidated authentication event
struct AuthenticationStateChanged : Event {
    enum class AuthState {
        Required,
        Succeeded,
        Failed
    };

    AuthState state;
    std::string service_name;
    std::string auth_url;         // for Required
    std::string user_identifier;  // for Succeeded
    std::string reason;           // for Failed

    // Constructor for Required
    static AuthenticationStateChanged required(std::string name, std::string url) {
        AuthenticationStateChanged event;
        event.state = AuthState::Required;
        event.service_name = std::move(name);
        event.auth_url = std::move(url);
        return event;
    }

    // Constructor for Succeeded
    static AuthenticationStateChanged succeeded(std::string name, std::string user) {
        AuthenticationStateChanged event;
        event.state = AuthState::Succeeded;
        event.service_name = std::move(name);
        event.user_identifier = std::move(user);
        return event;
    }

    // Constructor for Failed
    static AuthenticationStateChanged failed(std::string name, std::string reason_msg) {
        AuthenticationStateChanged event;
        event.state = AuthState::Failed;
        event.service_name = std::move(name);
        event.reason = std::move(reason_msg);
        return event;
    }

private:
    AuthenticationStateChanged() = default;
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

struct UpdateCheckStarted : Event {
    std::string current_version;

    explicit UpdateCheckStarted(std::string ver)
        : current_version(std::move(ver)) {}
};

struct UpdateAvailable : Event {
    std::string current_version;
    std::string latest_version;
    std::string download_url;
    std::string release_notes;

    UpdateAvailable(std::string curr, std::string latest, std::string url, std::string notes)
        : current_version(std::move(curr)), latest_version(std::move(latest)),
          download_url(std::move(url)), release_notes(std::move(notes)) {}
};

struct NoUpdateAvailable : Event {
    std::string current_version;

    explicit NoUpdateAvailable(std::string ver)
        : current_version(std::move(ver)) {}
};

struct UpdateCheckFailed : Event {
    std::string reason;

    explicit UpdateCheckFailed(std::string r)
        : reason(std::move(r)) {}
};

}