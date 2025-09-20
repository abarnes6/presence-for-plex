#include "presence_for_plex/services/plex/plex_session_manager.hpp"
#include "presence_for_plex/services/plex/plex_cache_manager.hpp"
#include "presence_for_plex/services/plex/plex_media_fetcher.hpp"
#include "presence_for_plex/services/network_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <nlohmann/json.hpp>

namespace presence_for_plex {
namespace services {

using json = nlohmann::json;

// SessionValidator implementation
SessionValidator::SessionValidator(
    std::shared_ptr<HttpClient> http_client,
    std::shared_ptr<IPlexCacheManager> cache_manager)
    : m_http_client(std::move(http_client))
    , m_cache_manager(std::move(cache_manager)) {
}

std::expected<bool, core::PlexError> SessionValidator::validate_session_user(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    const core::SessionKey& session_key,
    const std::string& target_username) {

    PLEX_LOG_DEBUG("SessionValidator", "validate_session_user() called for session: " + session_key.get() + ", target: " + target_username);

    if (target_username.empty()) {
        PLEX_LOG_DEBUG("SessionValidator", "No target username specified, allowing all sessions");
        return true; // No filtering required
    }

    auto username_result = fetch_session_username(server_uri, access_token, session_key);
    if (!username_result.has_value()) {
        return std::unexpected<core::PlexError>(username_result.error());
    }

    bool is_valid = username_result.value() == target_username;
    PLEX_LOG_DEBUG("SessionValidator", "Session " + session_key.get() +
                   " user validation: " + (is_valid ? "PASS" : "FAIL") +
                   " (user: " + username_result.value() + ", target: " + target_username + ")");

    return is_valid;
}

std::expected<std::string, core::PlexError> SessionValidator::fetch_session_username(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    const core::SessionKey& session_key) {

    // Check cache first
    std::string cache_key = server_uri + session_key.get();
    auto cached_username = m_cache_manager->get_cached_session_user(cache_key);
    if (cached_username.has_value()) {
        PLEX_LOG_DEBUG("SessionValidator", "Using cached username for session: " + session_key.get());
        return cached_username.value();
    }

    PLEX_LOG_DEBUG("SessionValidator", "Fetching username for session: " + session_key.get());

    std::string url = server_uri + SESSION_ENDPOINT;
    auto headers_map = get_standard_headers(access_token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get(url, headers);
    if (!response || !response->is_success()) {
        PLEX_LOG_ERROR("SessionValidator", "Failed to fetch session information");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    try {
        auto json_response = json::parse(response->body);

        if (!json_response.contains("MediaContainer")) {
            PLEX_LOG_ERROR("SessionValidator", "Invalid session response format");
            return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
        }

        if (json_response["MediaContainer"].contains("size") &&
            json_response["MediaContainer"]["size"].get<int>() == 0) {
            PLEX_LOG_DEBUG("SessionValidator", "No active sessions found");
            return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
        }

        if (!json_response["MediaContainer"].contains("Metadata")) {
            PLEX_LOG_DEBUG("SessionValidator", "No session metadata found");
            return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
        }

        // Find the matching session by sessionKey
        for (const auto& session : json_response["MediaContainer"]["Metadata"]) {
            if (session.contains("sessionKey") &&
                session["sessionKey"].get<std::string>() == session_key.get()) {

                // Extract user info
                if (session.contains("User") && session["User"].contains("title")) {
                    std::string username = session["User"]["title"].get<std::string>();
                    PLEX_LOG_INFO("SessionValidator", "Found user for session " + session_key.get() + ": " + username);

                    // Cache the result
                    m_cache_manager->cache_session_user(cache_key, username);
                    return username;
                }
                break;
            }
        }

        PLEX_LOG_WARNING("SessionValidator", "Session not found or no user info: " + session_key.get());
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);

    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("SessionValidator", "Error parsing session data: " + std::string(e.what()));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }
}

std::map<std::string, std::string> SessionValidator::get_standard_headers(const core::PlexToken& token) const {
    std::map<std::string, std::string> headers = {
        {"X-Plex-Product", "Presence For Plex"},
        {"X-Plex-Version", "1.0.0"},
        {"X-Plex-Client-Identifier", "presence-for-plex"},
        {"X-Plex-Platform", "Linux"},
        {"X-Plex-Device", "PC"},
        {"Accept", "application/json"}
    };

    if (!token.empty()) {
        headers["X-Plex-Token"] = token.get();
    }

    return headers;
}

// PlexSessionManager implementation
PlexSessionManager::PlexSessionManager()
    : m_target_username("") {

    PLEX_LOG_INFO("PlexSessionManager", "Creating session manager");
}

void PlexSessionManager::set_dependencies(
    std::shared_ptr<HttpClient> http_client,
    std::shared_ptr<IPlexCacheManager> cache_manager,
    std::shared_ptr<IPlexMediaFetcher> media_fetcher) {

    m_http_client = http_client;
    m_cache_manager = cache_manager;
    m_media_fetcher = media_fetcher;

    m_session_validator = std::make_unique<SessionValidator>(http_client, cache_manager);

    PLEX_LOG_DEBUG("PlexSessionManager", "Dependencies injected");
}

void PlexSessionManager::update_server_connection(
    const core::ServerId& server_id,
    const ServerConnectionInfo& connection_info) {

    PLEX_LOG_DEBUG("PlexSessionManager", "update_server_connection() called for server: " + server_id.get() + ", URI: " + connection_info.preferred_uri);

    std::lock_guard<std::mutex> lock(m_sessions_mutex);
    m_server_connections[server_id] = connection_info;

    PLEX_LOG_DEBUG("PlexSessionManager", "Updated connection info for server: " + server_id.get());
}

void PlexSessionManager::process_play_session_notification(
    const core::ServerId& server_id,
    const nlohmann::json& notification) {

    PLEX_LOG_DEBUG("PlexSessionManager", "Processing PlaySessionStateNotification");

    // Extract essential session information
    std::string session_key_str = notification.value("sessionKey", "");
    std::string state = notification.value("state", "");
    std::string media_key = notification.value("key", "");
    int64_t view_offset = notification.value("viewOffset", 0);

    if (session_key_str.empty()) {
        PLEX_LOG_WARNING("PlexSessionManager", "Session notification missing sessionKey");
        return;
    }

    core::SessionKey session_key(session_key_str);
    PLEX_LOG_DEBUG("PlexSessionManager", "Processing session " + session_key.get() + " state: " + state);

    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    if (state == "playing" || state == "paused" || state == "buffering") {
        update_session_info(server_id, session_key, state, media_key, view_offset);
    } else if (state == "stopped") {
        // Remove the session if it exists
        auto session_it = m_active_sessions.find(session_key);
        if (session_it != m_active_sessions.end()) {
            PLEX_LOG_INFO("PlexSessionManager", "Removing stopped session: " + session_key.get());
            m_active_sessions.erase(session_it);

            // Notify callback about state change if this was the current session
            if (m_session_callback) {
                auto current = find_most_recent_session();
                m_session_callback(current);
            }
        }
    }
}

std::optional<core::MediaInfo> PlexSessionManager::get_current_playback() const {
    PLEX_LOG_DEBUG("PlexSessionManager", "get_current_playback() called");
    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    if (m_active_sessions.empty()) {
        PLEX_LOG_DEBUG("PlexSessionManager", "No active sessions");
        return std::nullopt;
    }

    auto current = find_most_recent_session();
    if (current.state == core::PlaybackState::Stopped) {
        return std::nullopt;
    }

    PLEX_LOG_DEBUG("PlexSessionManager", "Current playback: " + current.title +
                   " (state: " + std::to_string(static_cast<int>(current.state)) + ")");

    return current;
}

std::expected<std::vector<core::MediaInfo>, core::PlexError> PlexSessionManager::get_active_sessions() const {
    PLEX_LOG_DEBUG("PlexSessionManager", "get_active_sessions() called, total sessions in map: " + std::to_string(m_active_sessions.size()));
    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    std::vector<core::MediaInfo> sessions;
    sessions.reserve(m_active_sessions.size());

    for (const auto& [session_key, info] : m_active_sessions) {
        if (info.state != core::PlaybackState::Stopped) {
            sessions.push_back(info);
        }
    }

    return sessions;
}

void PlexSessionManager::set_target_username(const std::string& username) {
    PLEX_LOG_DEBUG("PlexSessionManager", "set_target_username() called with: " + username);
    m_target_username = username;
    PLEX_LOG_INFO("PlexSessionManager", "Target username set to: " + username);
}

std::string PlexSessionManager::get_target_username() const {
    return m_target_username;
}

void PlexSessionManager::set_session_state_callback(SessionStateCallback callback) {
    m_session_callback = std::move(callback);
}

void PlexSessionManager::clear_all() {
    std::lock_guard<std::mutex> sessions_lock(m_sessions_mutex);
    std::lock_guard<std::mutex> state_lock(m_state_mutex);

    m_active_sessions.clear();
    m_server_connections.clear();
    m_last_reported_state.reset();

    PLEX_LOG_INFO("PlexSessionManager", "All sessions cleared");
}

void PlexSessionManager::remove_sessions_for_server(const core::ServerId& server_id) {
    PLEX_LOG_DEBUG("PlexSessionManager", "remove_sessions_for_server() called for: " + server_id.get());
    std::lock_guard<std::mutex> lock(m_sessions_mutex);

    auto it = m_active_sessions.begin();
    while (it != m_active_sessions.end()) {
        if (it->second.server_id.get() == server_id.get()) {
            PLEX_LOG_DEBUG("PlexSessionManager", "Removing session for server " + server_id.get() + ": " + it->first.get());
            it = m_active_sessions.erase(it);
        } else {
            ++it;
        }
    }

    m_server_connections.erase(server_id);
}

void PlexSessionManager::update_session_info(
    const core::ServerId& server_id,
    const core::SessionKey& session_key,
    const std::string& state,
    const std::string& media_key,
    int64_t view_offset) {

    // Check if we should process this session (user filtering)
    if (!should_process_session(server_id, session_key)) {
        PLEX_LOG_DEBUG("PlexSessionManager", "Skipping session (user filter): " + session_key.get());
        return;
    }

    // Get server connection info
    auto server_it = m_server_connections.find(server_id);
    if (server_it == m_server_connections.end()) {
        PLEX_LOG_ERROR("PlexSessionManager", "No connection info for server: " + server_id.get());
        return;
    }

    const auto& connection_info = server_it->second;

    // Fetch or use cached media info
    core::MediaInfo info;
    bool has_existing_session = m_active_sessions.find(session_key) != m_active_sessions.end();

    if (has_existing_session) {
        // Update existing session
        info = m_active_sessions[session_key];
        PLEX_LOG_DEBUG("PlexSessionManager", "Updating existing session: " + session_key.get());
    } else if (m_media_fetcher) {
        // Fetch new media details
        auto fetch_result = m_media_fetcher->fetch_media_details(
            connection_info.preferred_uri,
            connection_info.access_token,
            media_key
        );

        if (!fetch_result.has_value()) {
            PLEX_LOG_ERROR("PlexSessionManager", "Failed to fetch media details for session: " + session_key.get());
            return;
        }

        info = fetch_result.value();

        // For TV shows, fetch grandparent metadata if needed
        if (info.type == core::MediaType::TVShow && !info.grandparent_key.empty()) {
            (void)m_media_fetcher->fetch_grandparent_metadata(
                connection_info.preferred_uri,
                connection_info.access_token,
                info
            );
        }

        PLEX_LOG_DEBUG("PlexSessionManager", "Fetched new media info for session: " + session_key.get());
    } else {
        PLEX_LOG_ERROR("PlexSessionManager", "No media fetcher available");
        return;
    }

    // Update playback state
    update_playback_state(info, state, view_offset);

    // Update session and server information
    info.session_key = session_key;
    info.server_id = server_id;

    // Store the updated info
    bool is_new_session = !has_existing_session;
    m_active_sessions[session_key] = info;

    PLEX_LOG_INFO("PlexSessionManager",
                  (is_new_session ? "Added" : "Updated") + std::string(" session ") + session_key.get() +
                  ": " + info.title + " (" + std::to_string(info.progress) + "/" +
                  std::to_string(info.duration) + "s)");

    // Notify callback about state change
    if (m_session_callback) {
        m_session_callback(info);
    }
}

void PlexSessionManager::update_playback_state(
    core::MediaInfo& info,
    const std::string& state,
    int64_t view_offset) {

    if (state == "playing") {
        info.state = core::PlaybackState::Playing;
    } else if (state == "paused") {
        info.state = core::PlaybackState::Paused;
    } else if (state == "buffering") {
        info.state = core::PlaybackState::Buffering;
    } else {
        info.state = core::PlaybackState::Stopped;
    }

    info.progress = static_cast<double>(view_offset) / 1000.0; // Convert from milliseconds to seconds
    info.start_time = std::chrono::system_clock::now() -
                     std::chrono::seconds(static_cast<long>(info.progress));
}

bool PlexSessionManager::should_process_session(
    const core::ServerId& server_id,
    const core::SessionKey& session_key) {

    // Get server connection info
    auto server_it = m_server_connections.find(server_id);
    if (server_it == m_server_connections.end()) {
        PLEX_LOG_WARNING("PlexSessionManager", "No connection info found for server: " + server_id.get());
        return false;
    }

    const auto& connection_info = server_it->second;

    PLEX_LOG_DEBUG("PlexSessionManager", "Checking session " + session_key.get() +
                   " for server " + server_id.get() +
                   " (owned: " + std::to_string(connection_info.owned) +
                   ", target_user: " + m_target_username + ")");

    // For owned servers, validate the user
    if (connection_info.owned && m_session_validator) {
        PLEX_LOG_DEBUG("PlexSessionManager", "Validating session user for owned server");
        auto validation_result = m_session_validator->validate_session_user(
            connection_info.preferred_uri,
            connection_info.access_token,
            session_key,
            m_target_username
        );

        if (!validation_result.has_value()) {
            PLEX_LOG_DEBUG("PlexSessionManager", "Session validation failed: " + session_key.get());
            return false;
        }

        bool should_process = validation_result.value();
        return should_process;
    }

    // For shared servers, process all sessions
    PLEX_LOG_DEBUG("PlexSessionManager", "Processing all sessions for shared server");
    return true;
}

core::MediaInfo PlexSessionManager::find_most_recent_session() const {
    core::MediaInfo newest;
    newest.state = core::PlaybackState::Stopped;

    auto newest_time = std::chrono::system_clock::time_point::min();

    for (const auto& [session_key, info] : m_active_sessions) {
        if (info.state == core::PlaybackState::Playing ||
            info.state == core::PlaybackState::Paused ||
            info.state == core::PlaybackState::Buffering) {

            if (info.start_time > newest_time) {
                newest = info;
                newest_time = info.start_time;
            }
        }
    }

    return newest;
}

} // namespace services
} // namespace presence_for_plex
