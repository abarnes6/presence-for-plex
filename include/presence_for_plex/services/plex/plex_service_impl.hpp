#pragma once

#include "presence_for_plex/services/media_service.hpp"
#include "presence_for_plex/core/authentication_service.hpp"
#include "presence_for_plex/services/plex/plex_authenticator.hpp"
#include "presence_for_plex/services/plex/plex_cache_manager.hpp"
#include "presence_for_plex/services/plex/plex_connection_manager.hpp"
#include "presence_for_plex/services/plex/plex_media_fetcher.hpp"
#include "presence_for_plex/services/plex/plex_session_manager.hpp"
#include "presence_for_plex/services/network_service.hpp"
#include <memory>
#include <expected>
#include <atomic>
#include <chrono>
#include <mutex>
#include <map>
#include <nlohmann/json.hpp>

namespace presence_for_plex {
namespace core {
    class ConfigurationService;
}
namespace services {

// PlexService using composition and dependency injection
class PlexServiceImpl : public MediaService {
public:
    // Constructor with dependency injection
    PlexServiceImpl(
        std::shared_ptr<PlexAuthenticator> authenticator,
        std::shared_ptr<PlexCacheManager> cache_manager,
        std::shared_ptr<PlexConnectionManager> connection_manager,
        std::shared_ptr<PlexMediaFetcher> media_fetcher,
        std::shared_ptr<PlexSessionManager> session_manager,
        std::shared_ptr<HttpClient> http_client,
        std::shared_ptr<core::ConfigurationService> config_service,
        std::shared_ptr<core::AuthenticationService> auth_service
    );

    ~PlexServiceImpl() override;

    // MediaService interface implementation
    std::expected<void, core::PlexError> start() override;
    void stop() override;
    bool is_running() const override;

    void set_poll_interval(std::chrono::seconds interval) override;
    std::chrono::seconds get_poll_interval() const override;

    void set_event_bus(std::shared_ptr<core::EventBus> bus) override;

    std::expected<core::MediaInfo, core::PlexError> get_current_media() const override;
    std::expected<std::vector<core::MediaInfo>, core::PlexError> get_active_sessions() const override;

    std::expected<void, core::PlexError> add_server(std::unique_ptr<core::PlexServer> server) override;
    void remove_server(const core::ServerId& server_id) override;
    std::vector<core::ServerId> get_connected_servers() const override;

    bool is_server_connected(const core::ServerId& server_id) const override;

protected:
    void on_media_state_changed(const core::MediaInfo& old_state, const core::MediaInfo& new_state);
    void on_connection_state_changed(const core::ServerId& server_id, bool connected);
    void on_error_occurred(core::PlexError error, const std::string& message);

private:
    // Server discovery and configuration
    std::expected<void, core::PlexError> discover_servers(const std::string& auth_token);
    std::expected<void, core::PlexError> parse_server_json(const std::string& json_response, const std::string& auth_token);

    // Handle SSE events from connection manager
    void handle_sse_event(const core::ServerId& server_id, const std::string& event);

    // Injected dependencies
    std::shared_ptr<PlexAuthenticator> m_authenticator;
    std::shared_ptr<PlexCacheManager> m_cache_manager;
    std::shared_ptr<PlexConnectionManager> m_connection_manager;
    std::shared_ptr<PlexMediaFetcher> m_media_fetcher;
    std::shared_ptr<PlexSessionManager> m_session_manager;
    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::ConfigurationService> m_config_service;
    std::shared_ptr<core::AuthenticationService> m_auth_service;

    // Service state
    std::atomic<bool> m_running{false};
    std::chrono::seconds m_poll_interval{5};

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

// Builder for PlexServiceImpl (following Builder pattern)
class PlexServiceBuilder {
public:
    PlexServiceBuilder();

    PlexServiceBuilder& with_authenticator(std::shared_ptr<PlexAuthenticator> auth);
    PlexServiceBuilder& with_cache_manager(std::shared_ptr<PlexCacheManager> cache);
    PlexServiceBuilder& with_connection_manager(std::shared_ptr<PlexConnectionManager> conn);
    PlexServiceBuilder& with_media_fetcher(std::shared_ptr<PlexMediaFetcher> fetcher);
    PlexServiceBuilder& with_session_manager(std::shared_ptr<PlexSessionManager> session);
    PlexServiceBuilder& with_http_client(std::shared_ptr<HttpClient> client);
    PlexServiceBuilder& with_configuration_service(std::shared_ptr<core::ConfigurationService> config);
    PlexServiceBuilder& with_authentication_service(std::shared_ptr<core::AuthenticationService> auth);

    std::unique_ptr<PlexServiceImpl> build();

private:
    std::shared_ptr<PlexAuthenticator> m_authenticator;
    std::shared_ptr<PlexCacheManager> m_cache_manager;
    std::shared_ptr<PlexConnectionManager> m_connection_manager;
    std::shared_ptr<PlexMediaFetcher> m_media_fetcher;
    std::shared_ptr<PlexSessionManager> m_session_manager;
    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::ConfigurationService> m_config_service;
    std::shared_ptr<core::AuthenticationService> m_auth_service;
};

} // namespace services
} // namespace presence_for_plex
