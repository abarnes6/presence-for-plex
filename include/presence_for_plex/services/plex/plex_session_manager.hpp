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
class PlexCacheManager;
class PlexMediaFetcher;

// Server connection info for session management
struct ServerConnectionInfo {
    std::string preferred_uri;
    core::PlexToken access_token;
    bool owned = false;
};

// Session state callback
using SessionStateCallback = std::function<void(const core::MediaInfo&)>;

// Session validation helper
class SessionValidator {
public:
    explicit SessionValidator(
        std::shared_ptr<HttpClient> http_client,
        std::shared_ptr<PlexCacheManager> cache_manager
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
    std::shared_ptr<PlexCacheManager> m_cache_manager;
    static constexpr const char* SESSION_ENDPOINT = "/status/sessions";
};

class PlexSessionManager {
public:
    PlexSessionManager();

    // Dependency injection for services
    void set_dependencies(
        std::shared_ptr<HttpClient> http_client,
        std::shared_ptr<PlexCacheManager> cache_manager,
        std::shared_ptr<PlexMediaFetcher> media_fetcher
    );

    // Server connection management
    void update_server_connection(
        const core::ServerId& server_id,
        const ServerConnectionInfo& connection_info
    );

    void process_play_session_notification(
        const core::ServerId& server_id,
        const nlohmann::json& notification
    );

    std::optional<core::MediaInfo> get_current_playback() const;
    std::expected<std::vector<core::MediaInfo>, core::PlexError> get_active_sessions() const;

    void set_target_username(const std::string& username);
    std::string get_target_username() const;

    void set_session_state_callback(SessionStateCallback callback);

    void clear_all();
    void remove_sessions_for_server(const core::ServerId& server_id);

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
    std::shared_ptr<PlexCacheManager> m_cache_manager;
    std::shared_ptr<PlexMediaFetcher> m_media_fetcher;
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
