#include "presence_for_plex/services/presence_service.hpp"
#include "presence_for_plex/core/events_impl.hpp"

namespace presence_for_plex::services {

void PresenceService::publish_presence_updated(const PresenceData& data) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::PresenceUpdated{data});
    }
}

void PresenceService::publish_presence_cleared(const std::string& reason) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::PresenceCleared{reason});
    }
}

void PresenceService::publish_discord_connected(const std::string& app_id) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::DiscordConnected{app_id});
    }
}

void PresenceService::publish_discord_disconnected(const std::string& reason, bool will_retry) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::DiscordDisconnected{reason, will_retry});
    }
}

void PresenceService::publish_discord_error(DiscordError error, const std::string& message) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::DiscordErrorEvent{error, message});
    }
}

}