#include "presence_for_plex/services/plex/plex_cache_manager.hpp"
#include "presence_for_plex/utils/logger.hpp"

namespace presence_for_plex {
namespace services {

PlexCacheManager::PlexCacheManager() {
    PLEX_LOG_INFO("PlexCache", "Creating cache manager");
}

void PlexCacheManager::cache_media_info(const std::string& key, const core::MediaInfo& info) {
    PLEX_LOG_DEBUG("PlexCache", "cache_media_info() called for: " + info.title);
    std::lock_guard<std::mutex> lock(m_mutex);

    CacheEntry<core::MediaInfo> entry;
    entry.data = info;
    entry.timestamp = std::chrono::system_clock::now();
    entry.ttl = MEDIA_CACHE_TIMEOUT;

    m_media_info_cache[key] = entry;

    PLEX_LOG_DEBUG("PlexCache", "Cached media info for key: " + key);
}

std::optional<core::MediaInfo> PlexCacheManager::get_cached_media_info(const std::string& key) {
    PLEX_LOG_DEBUG("PlexCache", "get_cached_media_info() called for key: " + key.substr(0, 50) + "...");
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_media_info_cache.find(key);
    if (it != m_media_info_cache.end() && it->second.is_valid()) {
        PLEX_LOG_DEBUG("PlexCache", "Cache hit for media info: " + key);
        return it->second.data;
    }

    PLEX_LOG_DEBUG("PlexCache", "Cache miss for media info: " + key);
    return std::nullopt;
}

void PlexCacheManager::cache_tmdb_artwork(const std::string& tmdb_id, const std::string& art_path) {
    std::lock_guard<std::mutex> lock(m_mutex);

    CacheEntry<std::string> entry;
    entry.data = art_path;
    entry.timestamp = std::chrono::system_clock::now();
    entry.ttl = TMDB_CACHE_TIMEOUT;

    m_tmdb_artwork_cache[tmdb_id] = entry;

    PLEX_LOG_DEBUG("PlexCache", "Cached TMDB artwork for ID: " + tmdb_id);
}

std::optional<std::string> PlexCacheManager::get_cached_tmdb_artwork(const std::string& tmdb_id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_tmdb_artwork_cache.find(tmdb_id);
    if (it != m_tmdb_artwork_cache.end() && it->second.is_valid()) {
        PLEX_LOG_DEBUG("PlexCache", "Cache hit for TMDB artwork: " + tmdb_id);
        return it->second.data;
    }

    PLEX_LOG_DEBUG("PlexCache", "Cache miss for TMDB artwork: " + tmdb_id);
    return std::nullopt;
}

void PlexCacheManager::cache_mal_id(const std::string& key, const std::string& mal_id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    CacheEntry<std::string> entry;
    entry.data = mal_id;
    entry.timestamp = std::chrono::system_clock::now();
    entry.ttl = MAL_CACHE_TIMEOUT;

    m_mal_id_cache[key] = entry;

    PLEX_LOG_DEBUG("PlexCache", "Cached MAL ID for key: " + key);
}

std::optional<std::string> PlexCacheManager::get_cached_mal_id(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_mal_id_cache.find(key);
    if (it != m_mal_id_cache.end() && it->second.is_valid()) {
        PLEX_LOG_DEBUG("PlexCache", "Cache hit for MAL ID: " + key);
        return it->second.data;
    }

    PLEX_LOG_DEBUG("PlexCache", "Cache miss for MAL ID: " + key);
    return std::nullopt;
}

void PlexCacheManager::cache_session_user(const std::string& key, const std::string& username) {
    std::lock_guard<std::mutex> lock(m_mutex);

    CacheEntry<std::string> entry;
    entry.data = username;
    entry.timestamp = std::chrono::system_clock::now();
    entry.ttl = SESSION_CACHE_TIMEOUT;

    m_session_user_cache[key] = entry;

    PLEX_LOG_DEBUG("PlexCache", "Cached session user for key: " + key);
}

std::optional<std::string> PlexCacheManager::get_cached_session_user(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_session_user_cache.find(key);
    if (it != m_session_user_cache.end() && it->second.is_valid()) {
        PLEX_LOG_DEBUG("PlexCache", "Cache hit for session user: " + key);
        return it->second.data;
    }

    PLEX_LOG_DEBUG("PlexCache", "Cache miss for session user: " + key);
    return std::nullopt;
}

void PlexCacheManager::cache_server_uri(const std::string& server_id, const std::string& uri) {
    std::lock_guard<std::mutex> lock(m_mutex);

    CacheEntry<std::string> entry;
    entry.data = uri;
    entry.timestamp = std::chrono::system_clock::now();
    entry.ttl = SESSION_CACHE_TIMEOUT; // Reuse session timeout

    m_server_uri_cache[server_id] = entry;

    PLEX_LOG_DEBUG("PlexCache", "Cached server URI for ID: " + server_id);
}

std::optional<std::string> PlexCacheManager::get_cached_server_uri(const std::string& server_id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_server_uri_cache.find(server_id);
    if (it != m_server_uri_cache.end() && it->second.is_valid()) {
        PLEX_LOG_DEBUG("PlexCache", "Cache hit for server URI: " + server_id);
        return it->second.data;
    }

    PLEX_LOG_DEBUG("PlexCache", "Cache miss for server URI: " + server_id);
    return std::nullopt;
}

void PlexCacheManager::clear_all() {
    PLEX_LOG_DEBUG("PlexCache", "clear_all() called");
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t media_count = m_media_info_cache.size();
    size_t tmdb_count = m_tmdb_artwork_cache.size();
    size_t mal_count = m_mal_id_cache.size();
    size_t session_count = m_session_user_cache.size();
    size_t server_count = m_server_uri_cache.size();

    m_media_info_cache.clear();
    m_tmdb_artwork_cache.clear();
    m_mal_id_cache.clear();
    m_session_user_cache.clear();
    m_server_uri_cache.clear();

    PLEX_LOG_INFO("PlexCache", "All caches cleared - Media: " + std::to_string(media_count) + ", TMDB: " + std::to_string(tmdb_count) + ", MAL: " + std::to_string(mal_count) + ", Sessions: " + std::to_string(session_count) + ", Servers: " + std::to_string(server_count));
}

} // namespace services
} // namespace presence_for_plex