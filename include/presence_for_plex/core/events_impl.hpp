#pragma once

#include <string>
#include <utility>

#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/services/discord/discord_presence_service.hpp"
#include "presence_for_plex/platform/ui_service.hpp"

namespace presence_for_plex::core::events {

using services::PresenceData;
using platform::NotificationType;

struct PresenceUpdated : Event {
    PresenceData presence_data;

    explicit PresenceUpdated(PresenceData data)
        : presence_data(std::move(data)) {}
};

struct UserNotificationRequested : Event {
    std::string title;
    std::string message;
    NotificationType type;

    UserNotificationRequested(std::string t, std::string msg, NotificationType notification_type = NotificationType::Info)
        : title(std::move(t)), message(std::move(msg)), type(notification_type) {}
};

}