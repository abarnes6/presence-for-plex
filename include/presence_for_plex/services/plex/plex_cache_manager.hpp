#pragma once

#include "presence_for_plex/core/models.hpp"
#include <string>
#include <map>
#include <mutex>
#include <chrono>
#include <optional>

namespace presence_for_plex {
namespace services {

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

class PlexCacheManager {
public:
    PlexCacheManager();
    ~PlexCacheManager() = default;

    void cache_media_info(const std::string& key, const core::MediaInfo& info);
    std::optional<core::MediaInfo> get_cached_media_info(const std::string& key);

    void cache_tmdb_artwork(const std::string& tmdb_id, const std::string& art_path);
    std::optional<std::string> get_cached_tmdb_artwork(const std::string& tmdb_id);

    void cache_mal_id(const std::string& key, const std::string& mal_id);
    std::optional<std::string> get_cached_mal_id(const std::string& key);

    void cache_session_user(const std::string& key, const std::string& username);
    std::optional<std::string> get_cached_session_user(const std::string& key);

    void cache_server_uri(const std::string& server_id, const std::string& uri);
    std::optional<std::string> get_cached_server_uri(const std::string& server_id);

    void clear_all();

private:
    static constexpr std::chrono::seconds TMDB_CACHE_TIMEOUT{86400};
    static constexpr std::chrono::seconds MAL_CACHE_TIMEOUT{86400};
    static constexpr std::chrono::seconds MEDIA_CACHE_TIMEOUT{3600};
    static constexpr std::chrono::seconds SESSION_CACHE_TIMEOUT{300};

    mutable std::mutex m_mutex;
    std::map<std::string, CacheEntry<core::MediaInfo>> m_media_info_cache;
    std::map<std::string, CacheEntry<std::string>> m_tmdb_artwork_cache;
    std::map<std::string, CacheEntry<std::string>> m_mal_id_cache;
    std::map<std::string, CacheEntry<std::string>> m_session_user_cache;
    std::map<std::string, CacheEntry<std::string>> m_server_uri_cache;
};

} // namespace services
} // namespace presence_for_plex
