#pragma once

#include "presence_for_plex/services/media_service.hpp"
#include "presence_for_plex/services/plex/plex_authenticator.hpp"
#include "presence_for_plex/services/plex/plex_cache_manager.hpp"
#include "presence_for_plex/services/plex/plex_connection_manager.hpp"
#include "presence_for_plex/services/plex/plex_media_fetcher.hpp"
#include "presence_for_plex/services/plex/plex_session_manager.hpp"
#include "presence_for_plex/services/network_service.hpp"
#include <memory>

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
        std::shared_ptr<IPlexAuthenticator> authenticator,
        std::shared_ptr<IPlexCacheManager> cache_manager,
        std::shared_ptr<IPlexConnectionManager> connection_manager,
        std::shared_ptr<IPlexMediaFetcher> media_fetcher,
        std::shared_ptr<IPlexSessionManager> session_manager,
        std::shared_ptr<HttpClient> http_client,
        std::shared_ptr<core::ConfigurationService> config_service
    );

    ~PlexServiceImpl() override;

    // MediaService interface implementation
    std::expected<void, core::PlexError> start() override;
    void stop() override;
    bool is_running() const override;

    void set_poll_interval(std::chrono::seconds interval) override;
    std::chrono::seconds get_poll_interval() const override;

    void set_media_state_callback(MediaStateCallback callback) override;
    void set_error_callback(MediaErrorCallback callback) override;
    void set_connection_callback(MediaConnectionStateCallback callback) override;

    std::expected<core::MediaInfo, core::PlexError> get_current_media() const override;
    std::expected<std::vector<core::MediaInfo>, core::PlexError> get_active_sessions() const override;

    std::expected<void, core::PlexError> add_server(std::unique_ptr<core::PlexServer> server) override;
    void remove_server(const core::ServerId& server_id) override;
    std::vector<core::ServerId> get_connected_servers() const override;

    bool is_server_connected(const core::ServerId& server_id) const override;
    std::expected<void, core::PlexError> test_connection(const core::ServerId& server_id) override;

protected:
    void on_media_state_changed(const core::MediaInfo& old_state, const core::MediaInfo& new_state) override;
    void on_connection_state_changed(const core::ServerId& server_id, bool connected) override;
    void on_error_occurred(core::PlexError error, const std::string& message) override;

private:
    // Server discovery and configuration
    std::expected<void, core::PlexError> discover_servers(const std::string& auth_token);
    bool parse_server_json(const std::string& json_response, const std::string& auth_token);
    void load_configured_servers();
    void validate_server_configurations();

    // Handle SSE events from connection manager
    void handle_sse_event(const core::ServerId& server_id, const std::string& event);
    void process_play_session_notification(const core::ServerId& server_id, const nlohmann::json& notification);

    // Injected dependencies (following DIP)
    std::shared_ptr<IPlexAuthenticator> m_authenticator;
    std::shared_ptr<IPlexCacheManager> m_cache_manager;
    std::shared_ptr<IPlexConnectionManager> m_connection_manager;
    std::shared_ptr<IPlexMediaFetcher> m_media_fetcher;
    std::shared_ptr<IPlexSessionManager> m_session_manager;
    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::ConfigurationService> m_config_service;

    // Service state
    std::atomic<bool> m_running{false};
    std::chrono::seconds m_poll_interval{5};

    // Callbacks
    MediaStateCallback m_state_callback;
    MediaErrorCallback m_error_callback;
    MediaConnectionStateCallback m_connection_callback;

    // Configuration
    std::string m_plex_username;
    std::string m_tmdb_access_token;
};

// Builder for PlexServiceImpl (following Builder pattern)
class PlexServiceBuilder {
public:
    PlexServiceBuilder();

    PlexServiceBuilder& with_authenticator(std::shared_ptr<IPlexAuthenticator> auth);
    PlexServiceBuilder& with_cache_manager(std::shared_ptr<IPlexCacheManager> cache);
    PlexServiceBuilder& with_connection_manager(std::shared_ptr<IPlexConnectionManager> conn);
    PlexServiceBuilder& with_media_fetcher(std::shared_ptr<IPlexMediaFetcher> fetcher);
    PlexServiceBuilder& with_session_manager(std::shared_ptr<IPlexSessionManager> session);
    PlexServiceBuilder& with_http_client(std::shared_ptr<HttpClient> client);
    PlexServiceBuilder& with_configuration_service(std::shared_ptr<core::ConfigurationService> config);

    std::unique_ptr<PlexServiceImpl> build();

private:
    std::shared_ptr<IPlexAuthenticator> m_authenticator;
    std::shared_ptr<IPlexCacheManager> m_cache_manager;
    std::shared_ptr<IPlexConnectionManager> m_connection_manager;
    std::shared_ptr<IPlexMediaFetcher> m_media_fetcher;
    std::shared_ptr<IPlexSessionManager> m_session_manager;
    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::ConfigurationService> m_config_service;
};

} // namespace services
} // namespace presence_for_plex
