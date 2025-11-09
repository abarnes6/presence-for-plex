#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/services/plex/plex_auth_storage.hpp"
#include "presence_for_plex/services/plex/plex_authenticator.hpp"
#include "presence_for_plex/services/plex/plex_client.hpp"
#include "presence_for_plex/services/plex/plex_connection_manager.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include <memory>
#include <expected>
#include <atomic>
#include <chrono>
#include <mutex>
#include <map>
#include <nlohmann/json.hpp>

namespace presence_for_plex {
namespace core {
    class ConfigManager;
}
namespace services {

// Plex media service
class PlexService {
public:
    // Constructor with dependency injection
    PlexService(
        std::shared_ptr<PlexAuthenticator> authenticator,
        std::shared_ptr<PlexConnectionManager> connection_manager,
        std::shared_ptr<PlexClient> client,
        std::shared_ptr<HttpClient> http_client,
        std::shared_ptr<core::ConfigManager> config_service,
        std::shared_ptr<PlexAuthStorage> auth_service
    );

    ~PlexService();

    // Lifecycle management
    std::expected<void, core::PlexError> start();
    void stop();
    bool is_running() const;

    void set_poll_interval(std::chrono::seconds interval);
    std::chrono::seconds get_poll_interval() const;

    void set_event_bus(std::shared_ptr<core::EventBus> bus);

    std::expected<core::MediaInfo, core::PlexError> get_current_media() const;
    std::expected<std::vector<core::MediaInfo>, core::PlexError> get_active_sessions() const;

    std::expected<void, core::PlexError> add_server(std::unique_ptr<core::PlexServer> server);
    void remove_server(const core::ServerId& server_id);
    std::vector<core::ServerId> get_connected_servers() const;

    bool is_server_connected(const core::ServerId& server_id) const;

protected:
    void on_media_state_changed(const core::MediaInfo& old_state, const core::MediaInfo& new_state);
    void on_connection_state_changed(const core::ServerId& server_id, bool connected);
    void on_error_occurred(core::PlexError error, const std::string& message);

private:
    // Server discovery and configuration
    std::expected<void, core::PlexError> discover_servers(const std::string& auth_token);
    std::expected<void, core::PlexError> parse_server_json(const std::string& json_response, const std::string& auth_token);
    std::expected<void, core::PlexError> add_manual_server(const std::string& server_url, const core::PlexToken& auth_token);

    // Handle SSE events from connection manager
    void handle_sse_event(const core::ServerId& server_id, const std::string& event);

    // Media filtering based on configuration
    bool is_media_type_enabled(core::MediaType type) const;

    // Injected dependencies
    std::shared_ptr<PlexAuthenticator> m_authenticator;
    std::shared_ptr<PlexConnectionManager> m_connection_manager;
    std::shared_ptr<PlexClient> m_client;
    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::ConfigManager> m_config_service;
    std::shared_ptr<PlexAuthStorage> m_auth_service;
    std::shared_ptr<core::EventBus> m_event_bus;

    // Service state
    std::atomic<bool> m_running{false};
    std::chrono::seconds m_poll_interval{5};

    // Event publishing helpers
    void publish_media_started(const core::MediaInfo& info, const core::ServerId& server_id);
    void publish_media_updated(const core::MediaInfo& old_info, const core::MediaInfo& new_info);
    void publish_media_ended(const core::SessionKey& key, const core::ServerId& server_id);
    void publish_server_connected(const core::ServerId& server_id, const std::string& name);
    void publish_server_disconnected(const core::ServerId& server_id, const std::string& reason);
    void publish_media_error(core::PlexError error, const std::string& message, const std::optional<core::ServerId>& server_id = std::nullopt);

    // Configuration
    std::string m_plex_username;
    std::string m_tmdb_access_token;

    // Track last media state for proper state transitions
    core::MediaInfo m_last_media_state{};

    // Track server tokens and ownership for connection callbacks
    struct ServerTokenInfo {
        core::PlexToken token;
        bool owned;
    };
    std::map<core::ServerId, ServerTokenInfo> m_server_tokens;
    std::mutex m_server_tokens_mutex;
};


} // namespace services
} // namespace presence_for_plex
