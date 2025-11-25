#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/services/plex/plex_auth.hpp"
#include "presence_for_plex/services/plex/plex_sse.hpp"
#include "presence_for_plex/services/plex/metadata/metadata_service.hpp"
#include <memory>
#include <expected>
#include <atomic>
#include <chrono>
#include <mutex>
#include <map>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>

namespace presence_for_plex {
namespace core {
    class ConfigManager;
}
namespace services {

class HttpClient;

// Server connection info for session management
struct ServerConnectionInfo {
    std::string preferred_uri;
    core::PlexToken access_token;
    bool owned = false;
};

// Session state callback
using SessionStateCallback = std::function<void(const core::MediaInfo&)>;

class Plex {
public:
    Plex(
        std::shared_ptr<PlexAuth> auth,
        std::shared_ptr<PlexSSE> sse,
        std::shared_ptr<HttpClient> http_client,
        std::shared_ptr<core::ConfigManager> config_service
    );

    ~Plex();

    // Lifecycle
    std::expected<void, core::PlexError> start();
    void stop();
    bool is_running() const;

    void set_poll_interval(std::chrono::seconds interval);
    std::chrono::seconds get_poll_interval() const;
    void set_event_bus(std::shared_ptr<core::EventBus> bus);

    // External metadata services (TMDB, Jikan)
    void add_metadata_service(std::unique_ptr<IMetadataService> service);

    // Media access
    std::expected<core::MediaInfo, core::PlexError> get_current_media() const;
    std::expected<std::vector<core::MediaInfo>, core::PlexError> get_active_sessions() const;

    // Server management
    std::expected<void, core::PlexError> add_server(std::unique_ptr<core::PlexServer> server);
    void remove_server(const core::ServerId& server_id);
    std::vector<core::ServerId> get_connected_servers() const;
    bool is_server_connected(const core::ServerId& server_id) const;

private:
    // Server discovery
    std::expected<void, core::PlexError> discover_servers(const std::string& auth_token);
    std::expected<void, core::PlexError> parse_server_json(const std::string& json_response);
    std::expected<void, core::PlexError> add_manual_server(const std::string& server_url, const core::PlexToken& auth_token);

    // SSE event handling
    void handle_sse_event(const core::ServerId& server_id, const std::string& event);

    // Media fetching
    std::expected<core::MediaInfo, core::PlexError> fetch_media_details(
        const std::string& server_uri,
        const core::PlexToken& access_token,
        const std::string& media_key
    );

    std::expected<void, core::PlexError> fetch_grandparent_metadata(
        const std::string& server_uri,
        const core::PlexToken& access_token,
        core::MediaInfo& info
    );

    // Session management
    void process_session_event(const core::ServerId& server_id, const nlohmann::json& notification);
    void update_server_connection(const core::ServerId& server_id, const ServerConnectionInfo& connection_info);
    void update_session_info(const core::ServerId& server_id, const core::SessionKey& session_key,
                             const std::string& state, const std::string& media_key, int64_t view_offset);
    void update_playback_state(core::MediaInfo& info, const std::string& state, int64_t view_offset);
    bool should_process_session(const core::ServerId& server_id, const core::SessionKey& session_key);
    core::MediaInfo find_most_recent_session() const;
    std::expected<bool, core::PlexError> validate_session_user(const std::string& server_uri,
        const core::PlexToken& access_token, const core::SessionKey& session_key);
    std::expected<std::string, core::PlexError> fetch_session_username(const std::string& server_uri,
        const core::PlexToken& access_token, const core::SessionKey& session_key);

    // Helpers
    void extract_basic_media_info(const nlohmann::json& metadata, core::MediaInfo& info) const;
    void extract_type_specific_info(const nlohmann::json& metadata, core::MediaInfo& info) const;
    void enrich_with_external_services(core::MediaInfo& info);
    std::map<std::string, std::string> get_standard_headers(const core::PlexToken& token) const;
    bool is_media_type_enabled(core::MediaType type) const;

    // Caching
    template<typename T>
    struct CacheEntry {
        T data;
        std::chrono::system_clock::time_point timestamp;
        std::chrono::seconds ttl;
        bool is_valid() const {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now() - timestamp);
            return elapsed < ttl;
        }
    };

    void cache_media_info(const std::string& key, const core::MediaInfo& info);
    std::optional<core::MediaInfo> get_cached_media_info(const std::string& key);
    void cache_session_user(const std::string& key, const std::string& username);
    std::optional<std::string> get_cached_session_user(const std::string& key);

    static constexpr std::chrono::seconds MEDIA_CACHE_TIMEOUT{3600};
    static constexpr std::chrono::seconds SESSION_CACHE_TIMEOUT{300};
    static constexpr const char* SESSION_ENDPOINT = "/status/sessions";

    // Event publishing
    void publish_media_updated(const core::MediaInfo& old_info, const core::MediaInfo& new_info);
    void publish_server_connected(const core::ServerId& server_id, const std::string& name);
    void publish_server_disconnected(const core::ServerId& server_id, const std::string& reason);
    void publish_media_error(core::PlexError error, const std::string& message,
                             const std::optional<core::ServerId>& server_id = std::nullopt);

    // Dependencies
    std::shared_ptr<PlexAuth> m_auth;
    std::shared_ptr<PlexSSE> m_sse;
    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::ConfigManager> m_config_service;
    std::shared_ptr<core::EventBus> m_event_bus;
    std::vector<std::unique_ptr<IMetadataService>> m_metadata_services;

    // State
    std::atomic<bool> m_running{false};
    std::chrono::seconds m_poll_interval{5};
    std::string m_target_username;
    core::MediaInfo m_last_media_state{};

    // Session state
    mutable std::mutex m_sessions_mutex;
    std::map<core::SessionKey, core::MediaInfo> m_active_sessions;
    std::map<core::ServerId, ServerConnectionInfo> m_server_connections;

    // Server tokens
    struct ServerTokenInfo {
        core::PlexToken token;
        bool owned;
    };
    std::map<core::ServerId, ServerTokenInfo> m_server_tokens;
    std::mutex m_server_tokens_mutex;

    // Caches
    mutable std::mutex m_cache_mutex;
    std::map<std::string, CacheEntry<core::MediaInfo>> m_media_cache;
    std::map<std::string, CacheEntry<std::string>> m_session_user_cache;
};

} // namespace services
} // namespace presence_for_plex
