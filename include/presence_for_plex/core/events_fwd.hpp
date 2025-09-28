#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace presence_for_plex::core::events {

struct Event {
    std::chrono::steady_clock::time_point timestamp{std::chrono::steady_clock::now()};
    Event() = default;
    virtual ~Event() = default;
};

struct ConfigurationUpdated;
struct ConfigurationError;
struct MediaSessionStarted;
struct MediaSessionUpdated;
struct MediaSessionEnded;
struct MediaPlaybackPaused;
struct MediaPlaybackResumed;
struct MediaError;
struct ServerConnectionEstablished;
struct ServerConnectionLost;
struct ServerReconnecting;
struct PresenceUpdated;
struct PresenceCleared;
struct DiscordConnected;
struct DiscordDisconnected;
struct DiscordErrorEvent;
struct ApplicationStateChanged;
struct ApplicationStarting;
struct ApplicationReady;
struct ApplicationShuttingDown;
struct ApplicationErrorEvent;
struct ServiceRegistered;
struct ServiceInitialized;
struct ServiceError;
struct AuthenticationRequired;
struct AuthenticationSucceeded;
struct AuthenticationFailed;
struct UserNotificationRequested;
struct HealthCheckSucceeded;
struct HealthCheckFailed;

}