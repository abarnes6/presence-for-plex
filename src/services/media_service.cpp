#include "presence_for_plex/services/media_service.hpp"

namespace presence_for_plex::services {

void MediaService::publish_media_started(const MediaInfo& info, const ServerId& server_id) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::MediaSessionStarted{info, server_id});
    }
}

void MediaService::publish_media_updated(const MediaInfo& old_info, const MediaInfo& new_info) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::MediaSessionUpdated{old_info, new_info});
    }
}

void MediaService::publish_media_ended(const core::SessionKey& key, const ServerId& server_id) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::MediaSessionEnded{key, server_id});
    }
}

void MediaService::publish_server_connected(const ServerId& server_id, const std::string& name) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::ServerConnectionEstablished{server_id, name});
    }
}

void MediaService::publish_server_disconnected(const ServerId& server_id, const std::string& reason) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::ServerConnectionLost{server_id, reason});
    }
}

void MediaService::publish_media_error(PlexError error, const std::string& message, const std::optional<ServerId>& server_id) {
    if (m_event_bus) {
        m_event_bus->publish(core::events::MediaError{error, message, server_id});
    }
}

}