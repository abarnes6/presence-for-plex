#include "presence_for_plex/services/presence_service.hpp"

namespace presence_for_plex {
namespace services {

// This file serves as a wrapper/facade for the Discord service
// The actual implementation is in discord_presence_manager.cpp

class DiscordClient {
public:
    DiscordClient(const core::ApplicationConfig& config) {
        auto factory = PresenceServiceFactory::create_default_factory();
        m_service = factory->create_service(
            PresenceServiceFactory::ServiceType::Discord,
            config
        );
    }

    ~DiscordClient() = default;

    bool initialize() {
        if (!m_service) {
            return false;
        }

        auto result = m_service->initialize();
        return result.has_value();
    }

    void shutdown() {
        if (m_service) {
            m_service->shutdown();
        }
    }

    bool is_connected() const {
        return m_service && m_service->is_connected();
    }

    bool update_presence_from_media(const core::MediaInfo& media) {
        if (!m_service) {
            return false;
        }

        auto result = m_service->update_from_media(media);
        return result.has_value();
    }

    bool clear_presence() {
        if (!m_service) {
            return false;
        }

        auto result = m_service->clear_presence();
        return result.has_value();
    }

    void set_callbacks(
        std::function<void(bool)> connection_callback,
        std::function<void(core::DiscordError, const std::string&)> error_callback) {

        if (m_service) {
            m_service->set_connection_callback(connection_callback);
            m_service->set_error_callback(error_callback);
        }
    }

private:
    std::unique_ptr<PresenceService> m_service;
};

} // namespace services
} // namespace presence_for_plex
