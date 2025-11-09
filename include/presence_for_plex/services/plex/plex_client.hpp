#pragma once

#include "presence_for_plex/core/models.hpp"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <functional>
#include <optional>
#include <expected>
#include <nlohmann/json.hpp>

namespace presence_for_plex {
namespace services {

// Forward declarations
class HttpClient;

// Session state callback
using SessionStateCallback = std::function<void(const core::MediaInfo&)>;

// Server connection info for session management
struct ServerConnectionInfo {
    std::string preferred_uri;
    core::PlexToken access_token;
    bool owned = false;
};

// Interface for external metadata services (TMDB, Jikan, etc.)
class IExternalMetadataService {
public:
    virtual ~IExternalMetadataService() = default;

    virtual std::expected<std::string, core::PlexError> fetch_artwork_url(
        const std::string& external_id,
        core::MediaType type
    ) = 0;

    virtual std::expected<void, core::PlexError> enrich_media_info(
        core::MediaInfo& info
    ) = 0;
};

// TMDB service implementation
class TMDBService : public IExternalMetadataService {
public:
    explicit TMDBService(std::shared_ptr<HttpClient> http_client, const std::string& access_token);

    std::expected<std::string, core::PlexError> fetch_artwork_url(
        const std::string& tmdb_id,
        core::MediaType type
    ) override;

    std::expected<void, core::PlexError> enrich_media_info(core::MediaInfo& info) override;

private:
    std::shared_ptr<HttpClient> m_http_client;
    std::string m_access_token;
    static constexpr const char* TMDB_IMAGE_BASE_URL = "https://image.tmdb.org/t/p/w500";
};

// Jikan/MAL service implementation
class JikanService : public IExternalMetadataService {
public:
    explicit JikanService(std::shared_ptr<HttpClient> http_client);

    std::expected<std::string, core::PlexError> fetch_artwork_url(
        const std::string& mal_id,
        core::MediaType type
    ) override;

    std::expected<void, core::PlexError> enrich_media_info(core::MediaInfo& info) override;

private:
    std::expected<std::string, core::PlexError> search_anime_by_title(
        const std::string& title,
        int year = 0
    );

    std::string url_encode(const std::string& value);

    std::shared_ptr<HttpClient> m_http_client;
    static constexpr const char* JIKAN_API_URL = "https://api.jikan.moe/v4/anime";
};

// Unified Plex data access and state management client
// Combines media fetching, caching, and session tracking
class PlexClient {
public:
    explicit PlexClient(
        std::shared_ptr<HttpClient> http_client,
        std::string username = ""
    );

    ~PlexClient() = default;

    // External metadata services (TMDB, Jikan)
    void add_metadata_service(std::unique_ptr<IExternalMetadataService> service);

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
    void update_server_connection(
        const core::ServerId& server_id,
        const ServerConnectionInfo& connection_info
    );

    void process_session_event(
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
    // Cache entry with TTL
    template<typename T>
    struct CacheEntry {
        T data;
        std::chrono::system_clock::time_point timestamp;
        std::chrono::seconds ttl;

        bool is_valid() const {
            auto now = std::chrono::system_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp);
            return elapsed < ttl;
        }
    };

    // Media fetching helpers
    void extract_basic_media_info(const nlohmann::json& metadata, core::MediaInfo& info) const;
    void extract_type_specific_info(const nlohmann::json& metadata, core::MediaInfo& info) const;
    void enrich_with_external_services(core::MediaInfo& info);
    std::map<std::string, std::string> get_standard_headers(const core::PlexToken& token) const;

    // Session management helpers
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

    std::expected<bool, core::PlexError> validate_session_user(
        const std::string& server_uri,
        const core::PlexToken& access_token,
        const core::SessionKey& session_key
    );

    std::expected<std::string, core::PlexError> fetch_session_username(
        const std::string& server_uri,
        const core::PlexToken& access_token,
        const core::SessionKey& session_key
    );

    // Caching helpers
    void cache_media_info(const std::string& key, const core::MediaInfo& info);
    std::optional<core::MediaInfo> get_cached_media_info(const std::string& key);
    void cache_session_user(const std::string& key, const std::string& username);
    std::optional<std::string> get_cached_session_user(const std::string& key);

    // Cache timeouts
    static constexpr std::chrono::seconds MEDIA_CACHE_TIMEOUT{3600};
    static constexpr std::chrono::seconds SESSION_CACHE_TIMEOUT{300};
    static constexpr const char* SESSION_ENDPOINT = "/status/sessions";

    // Dependencies
    std::shared_ptr<HttpClient> m_http_client;
    std::vector<std::unique_ptr<IExternalMetadataService>> m_external_services;

    // Session state
    mutable std::mutex m_sessions_mutex;
    std::map<core::SessionKey, core::MediaInfo> m_active_sessions;
    std::map<core::ServerId, ServerConnectionInfo> m_server_connections;
    std::string m_target_username;
    SessionStateCallback m_session_callback;

    mutable std::mutex m_state_mutex;
    std::optional<core::MediaInfo> m_last_reported_state;

    // Caches
    mutable std::mutex m_cache_mutex;
    std::map<std::string, CacheEntry<core::MediaInfo>> m_media_cache;
    std::map<std::string, CacheEntry<std::string>> m_session_user_cache;
};

} // namespace services
} // namespace presence_for_plex
