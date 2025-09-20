#pragma once

#include "presence_for_plex/core/models.hpp"
#include <string>
#include <map>
#include <mutex>
#include <chrono>
#include <optional>

namespace presence_for_plex {
namespace services {

// Cache entry template
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

// Interface for cache management following SRP
class IPlexCacheManager {
public:
    virtual ~IPlexCacheManager() = default;

    // Media info cache
    virtual void cache_media_info(const std::string& key, const core::MediaInfo& info) = 0;
    virtual std::optional<core::MediaInfo> get_cached_media_info(const std::string& key) = 0;

    // TMDB artwork cache
    virtual void cache_tmdb_artwork(const std::string& tmdb_id, const std::string& art_path) = 0;
    virtual std::optional<std::string> get_cached_tmdb_artwork(const std::string& tmdb_id) = 0;

    // MAL ID cache
    virtual void cache_mal_id(const std::string& key, const std::string& mal_id) = 0;
    virtual std::optional<std::string> get_cached_mal_id(const std::string& key) = 0;

    // Session user cache
    virtual void cache_session_user(const std::string& key, const std::string& username) = 0;
    virtual std::optional<std::string> get_cached_session_user(const std::string& key) = 0;

    // Server URI cache
    virtual void cache_server_uri(const std::string& server_id, const std::string& uri) = 0;
    virtual std::optional<std::string> get_cached_server_uri(const std::string& server_id) = 0;

    // Clear all caches
    virtual void clear_all() = 0;
};

// Concrete implementation
class PlexCacheManager : public IPlexCacheManager {
public:
    PlexCacheManager();
    ~PlexCacheManager() override = default;

    void cache_media_info(const std::string& key, const core::MediaInfo& info) override;
    std::optional<core::MediaInfo> get_cached_media_info(const std::string& key) override;

    void cache_tmdb_artwork(const std::string& tmdb_id, const std::string& art_path) override;
    std::optional<std::string> get_cached_tmdb_artwork(const std::string& tmdb_id) override;

    void cache_mal_id(const std::string& key, const std::string& mal_id) override;
    std::optional<std::string> get_cached_mal_id(const std::string& key) override;

    void cache_session_user(const std::string& key, const std::string& username) override;
    std::optional<std::string> get_cached_session_user(const std::string& key) override;

    void cache_server_uri(const std::string& server_id, const std::string& uri) override;
    std::optional<std::string> get_cached_server_uri(const std::string& server_id) override;

    void clear_all() override;

private:
    // Cache timeouts
    static constexpr std::chrono::seconds TMDB_CACHE_TIMEOUT{86400};   // 24 hours
    static constexpr std::chrono::seconds MAL_CACHE_TIMEOUT{86400};    // 24 hours
    static constexpr std::chrono::seconds MEDIA_CACHE_TIMEOUT{3600};   // 1 hour
    static constexpr std::chrono::seconds SESSION_CACHE_TIMEOUT{300};  // 5 minutes

    mutable std::mutex m_mutex;
    std::map<std::string, CacheEntry<core::MediaInfo>> m_media_info_cache;
    std::map<std::string, CacheEntry<std::string>> m_tmdb_artwork_cache;
    std::map<std::string, CacheEntry<std::string>> m_mal_id_cache;
    std::map<std::string, CacheEntry<std::string>> m_session_user_cache;
    std::map<std::string, CacheEntry<std::string>> m_server_uri_cache;
};

} // namespace services
} // namespace presence_for_plex