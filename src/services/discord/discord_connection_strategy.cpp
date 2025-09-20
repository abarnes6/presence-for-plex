#include "presence_for_plex/services/discord_connection_strategy.hpp"
#include "presence_for_plex/utils/logger.hpp"

namespace presence_for_plex::services {

DiscordConnectionStrategy::DiscordConnectionStrategy(std::string client_id)
    : m_client_id(std::move(client_id)) {

    if (m_client_id.empty()) {
        throw std::invalid_argument("Discord client ID cannot be empty");
    }

    m_ipc = std::make_unique<DiscordIPC>(m_client_id);
    PLEX_LOG_DEBUG("DiscordConnectionStrategy", "Initialized with client ID: " + m_client_id);
}

bool DiscordConnectionStrategy::connect() {
    if (!m_ipc) {
        PLEX_LOG_ERROR("DiscordConnectionStrategy", "IPC instance is null");
        return false;
    }

    PLEX_LOG_DEBUG("DiscordConnectionStrategy", "Attempting to connect to Discord");

    try {
        bool success = m_ipc->connect();
        if (success) {
            PLEX_LOG_INFO("DiscordConnectionStrategy", "Successfully connected to Discord");
        } else {
            PLEX_LOG_DEBUG("DiscordConnectionStrategy", "Failed to connect to Discord");
        }
        return success;
    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("DiscordConnectionStrategy",
            "Exception during connection: " + std::string(e.what()));
        return false;
    }
}

bool DiscordConnectionStrategy::is_connected() const {
    return m_ipc && m_ipc->is_connected();
}

void DiscordConnectionStrategy::disconnect() {
    if (m_ipc) {
        PLEX_LOG_DEBUG("DiscordConnectionStrategy", "Disconnecting from Discord");
        m_ipc->disconnect();
    }
}

bool DiscordConnectionStrategy::send_health_check() {
    if (!is_connected()) {
        PLEX_LOG_DEBUG("DiscordConnectionStrategy", "Cannot send health check: not connected");
        return false;
    }

    try {
        bool success = m_ipc->send_ping();
        PLEX_LOG_DEBUG("DiscordConnectionStrategy",
            "Health check (ping): " + std::string(success ? "SUCCESS" : "FAILED"));
        return success;
    } catch (const std::exception& e) {
        PLEX_LOG_WARNING("DiscordConnectionStrategy",
            "Health check exception: " + std::string(e.what()));
        return false;
    }
}

} // namespace presence_for_plex::services