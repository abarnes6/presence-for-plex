#pragma once

#include "presence_for_plex/services/connection_manager.hpp"
#include "presence_for_plex/services/discord_ipc.hpp"
#include <memory>
#include <string>

namespace presence_for_plex::services {

/**
 * @brief Discord-specific connection strategy
 *
 * Implements the IConnectionStrategy interface for Discord Rich Presence.
 * Handles Discord-specific connection logic and health checking.
 */
class DiscordConnectionStrategy : public IConnectionStrategy {
public:
    explicit DiscordConnectionStrategy(std::string client_id);

    ~DiscordConnectionStrategy() override = default;

    // IConnectionStrategy implementation
    bool connect() override;
    bool is_connected() const override;
    void disconnect() override;
    bool send_health_check() override;

    /**
     * @brief Get the underlying Discord IPC instance
     * @return Pointer to DiscordIPC instance, or nullptr if not available
     */
    DiscordIPC* get_ipc() const { return m_ipc.get(); }

    /**
     * @brief Get the client ID used for connection
     */
    const std::string& get_client_id() const { return m_client_id; }

private:
    std::string m_client_id;
    std::unique_ptr<DiscordIPC> m_ipc;
};

} // namespace presence_for_plex::services