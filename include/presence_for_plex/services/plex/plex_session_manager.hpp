#pragma once

#include "presence_for_plex/core/models.hpp"
#include <memory>
#include <map>
#include <mutex>
#include <functional>
#include <nlohmann/json.hpp>
#include <expected>

namespace presence_for_plex {
namespace services {

// Forward declarations
class HttpClient;
class IPlexCacheManager;
class IPlexMediaFetcher;

// Server connection info for session management
struct ServerConnectionInfo {
    std::string preferred_uri;
    core::PlexToken access_token;
    bool owned = false;
};

// Session state callback
using SessionStateCallback = std::function<void(const core::MediaInfo&)>;

// Interface for session management following SRP
class IPlexSessionManager {
public:
    virtual ~IPlexSessionManager() = default;

    // Session management
    virtual void process_play_session_notification(
        const core::ServerId& server_id,
        const nlohmann::json& notification
    ) = 0;

    virtual std::optional<core::MediaInfo> get_current_playback() const = 0;
    virtual std::expected<std::vector<core::MediaInfo>, core::PlexError> get_active_sessions() const = 0;

    // Session filtering
    virtual void set_target_username(const std::string& username) = 0;
    virtual std::string get_target_username() const = 0;

    // Callbacks
    virtual void set_session_state_callback(SessionStateCallback callback) = 0;

    // Server connection management
    virtual void update_server_connection(
        const core::ServerId& server_id,
        const ServerConnectionInfo& connection_info) = 0;

    // Cleanup
    virtual void clear_all() = 0;
    virtual void remove_sessions_for_server(const core::ServerId& server_id) = 0;
};

// Session validation helper
class SessionValidator {
public:
    explicit SessionValidator(
        std::shared_ptr<HttpClient> http_client,
        std::shared_ptr<IPlexCacheManager> cache_manager
    );

    // Validate if session belongs to target user
    std::expected<bool, core::PlexError> validate_session_user(
        const std::string& server_uri,
        const core::PlexToken& access_token,
        const core::SessionKey& session_key,
        const std::string& target_username
    );

    // Fetch username for a session
    std::expected<std::string, core::PlexError> fetch_session_username(
        const std::string& server_uri,
        const core::PlexToken& access_token,
        const core::SessionKey& session_key
    );

private:
    std::map<std::string, std::string> get_standard_headers(const core::PlexToken& token) const;

    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<IPlexCacheManager> m_cache_manager;
    static constexpr const char* SESSION_ENDPOINT = "/status/sessions";
};

// Concrete implementation
class PlexSessionManager : public IPlexSessionManager {
public:
    PlexSessionManager();

    // Dependency injection for services
    void set_dependencies(
        std::shared_ptr<HttpClient> http_client,
        std::shared_ptr<IPlexCacheManager> cache_manager,
        std::shared_ptr<IPlexMediaFetcher> media_fetcher
    );

    // Server connection management
    void update_server_connection(
        const core::ServerId& server_id,
        const ServerConnectionInfo& connection_info
    ) override;

    void process_play_session_notification(
        const core::ServerId& server_id,
        const nlohmann::json& notification
    ) override;

    std::optional<core::MediaInfo> get_current_playback() const override;
    std::expected<std::vector<core::MediaInfo>, core::PlexError> get_active_sessions() const override;

    void set_target_username(const std::string& username) override;
    std::string get_target_username() const override;

    void set_session_state_callback(SessionStateCallback callback) override;

    void clear_all() override;
    void remove_sessions_for_server(const core::ServerId& server_id) override;

private:
    void update_session_info(
        const core::ServerId& server_id,
        const core::SessionKey& session_key,
        const std::string& state,
        const std::string& media_key,
        int64_t view_offset
    );

    void update_playback_state(
        core::MediaInfo& info,
        const std::string& state,
        int64_t view_offset
    );

    bool should_process_session(
        const core::ServerId& server_id,
        const core::SessionKey& session_key
    );

    core::MediaInfo find_most_recent_session() const;

    mutable std::mutex m_sessions_mutex;
    std::map<core::SessionKey, core::MediaInfo> m_active_sessions;
    std::map<core::ServerId, ServerConnectionInfo> m_server_connections;

    // Dependencies
    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<IPlexCacheManager> m_cache_manager;
    std::shared_ptr<IPlexMediaFetcher> m_media_fetcher;
    std::unique_ptr<SessionValidator> m_session_validator;

    // Configuration
    std::string m_target_username;

    // Callbacks
    SessionStateCallback m_session_callback;

    // State tracking
    mutable std::mutex m_state_mutex;
    std::optional<core::MediaInfo> m_last_reported_state;
};

} // namespace services
} // namespace presence_for_plex
