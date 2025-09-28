#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace presence_for_plex::core {

enum class ApplicationState : int;
enum class ApplicationError : int;
enum class ConfigError : int;
enum class PlexError : int;
enum class DiscordError : int;

struct MediaInfo;
struct ApplicationConfig;
struct SessionKey;
struct ServerId;
struct PlexServer;

}

namespace presence_for_plex::services {

struct PresenceData;

}

namespace presence_for_plex::platform {

enum class NotificationType : int;

}