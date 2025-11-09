#pragma once

#include "presence_for_plex/utils/logger.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <variant>
#include <system_error>
#include <functional>
#include <expected>

namespace presence_for_plex {
namespace core {

// ============================================================================
// Application-wide types
// ============================================================================

enum class ApplicationState {
    NotInitialized,
    Initializing,
    Running,
    Stopping,
    Stopped,
    Error
};

enum class ApplicationError {
    InitializationFailed,
    ServiceUnavailable,
    ConfigurationError,
    AlreadyRunning,
    ShutdownFailed
};

enum class ConfigError {
    FileNotFound,
    InvalidFormat,
    ValidationError,
    PermissionDenied
};

// ============================================================================
// Service type identifiers
// ============================================================================

enum class MediaServiceType {
    Plex,
    // Future: Jellyfin, Emby, etc.
};

enum class PresenceServiceType {
    Discord,
    // Future: Slack, Teams, etc.
};

// ============================================================================
// Domain-specific types
// ============================================================================

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Buffering,
    BadToken,
    NotInitialized
};

enum class MediaType {
    Movie,
    TVShow,
    Music,
    Unknown
};

enum class PlexError {
    NetworkError,
    AuthenticationError,
    ServerNotFound,
    InvalidResponse,
    ParseError,
    Timeout,
    NotInitialized
};

enum class DiscordError {
    NotConnected,
    IpcError,
    InvalidPayload,
    Timeout,
    ServiceUnavailable
};

// ============================================================================
// Forward declarations
// ============================================================================

class HttpClient;

// Strong types for IDs and tokens
using PlexToken = std::string;
using ClientId = std::string;

struct ServerId {
    std::string value;

    ServerId() = default;
    explicit ServerId(std::string id) : value(std::move(id)) {}

    bool empty() const { return value.empty(); }
    const std::string& get() const { return value; }

    bool operator==(const ServerId& other) const { return value == other.value; }
    bool operator<(const ServerId& other) const { return value < other.value; }
};

struct SessionKey {
    std::string value;

    SessionKey() = default;
    explicit SessionKey(std::string key) : value(std::move(key)) {}

    bool empty() const { return value.empty(); }
    const std::string& get() const { return value; }

    bool operator==(const SessionKey& other) const { return value == other.value; }
    bool operator<(const SessionKey& other) const { return value < other.value; }
};

// Server information
struct PlexServer {
    std::string name;
    ClientId client_identifier;
    std::string local_uri;
    std::string public_uri;
    PlexToken access_token;
    std::chrono::system_clock::time_point last_updated;
    // Note: HTTP client will be managed separately to avoid forward declaration issues
    std::atomic<bool> running{false};
    bool owned = false;

    // Move-only semantics
    PlexServer() = default;
    PlexServer(const PlexServer&) = delete;
    PlexServer& operator=(const PlexServer&) = delete;
    
    PlexServer(PlexServer&& other) noexcept 
        : name(std::move(other.name))
        , client_identifier(std::move(other.client_identifier))
        , local_uri(std::move(other.local_uri))
        , public_uri(std::move(other.public_uri))
        , access_token(std::move(other.access_token))
        , last_updated(other.last_updated)
        , running(other.running.load())
        , owned(other.owned)
    {
        other.running.store(false);
        other.owned = false;
    }
    
    PlexServer& operator=(PlexServer&& other) noexcept {
        if (this != &other) {
            name = std::move(other.name);
            client_identifier = std::move(other.client_identifier);
            local_uri = std::move(other.local_uri);
            public_uri = std::move(other.public_uri);
            access_token = std::move(other.access_token);
            last_updated = other.last_updated;
            running.store(other.running.load());
            owned = other.owned;
            
            other.running.store(false);
            other.owned = false;
        }
        return *this;
    }
};

// Media information with strong typing
struct MediaInfo {
    // General information
    std::string title;               // Title of the media
    std::string original_title;      // Original title (original language)
    MediaType type = MediaType::Unknown;
    std::string art_path;            // Path to art on the server
    int year = 0;                    // Year of release
    std::string summary;             // Summary of the media
    std::vector<std::string> genres; // List of genres

    // External IDs
    std::string imdb_id;             // IMDB ID (if applicable)
    std::string tmdb_id;             // TMDB ID (if applicable)
    std::string tvdb_id;             // TVDB ID (if applicable)
    std::string mal_id;              // MyAnimeList ID (if applicable)

    // Additional metadata
    double rating = 0.0;             // Rating (e.g., IMDB/TMDB rating)
    std::string thumb;               // Thumbnail URL
    std::string art;                 // Art URL
    std::string studio;              // Studio/Network name

    // TV Show specific
    std::string grandparent_title;   // Parent title (TV show name)
    std::string grandparent_art;     // Parent art URL (TV show cover)
    std::string grandparent_key;     // Parent ID (TV show ID)
    std::string show_title;          // Show title (alias for grandparent_title)
    int season = 0;                  // Season number
    int episode = 0;                 // Episode number
    int track = 0;                   // Track/episode index

    // Music specific
    std::string album;               // Album title
    std::string artist;              // Artist name

    // Playback information
    std::string username;            // Username of the person watching
    PlaybackState state = PlaybackState::Stopped;
    double progress = 0.0;           // Current progress in seconds
    double duration = 0.0;           // Total duration in seconds
    std::chrono::system_clock::time_point start_time;       // Calculated start based on progress (for progress bar)
    std::chrono::system_clock::time_point session_created_at; // Actual time session was created (never changes)

    // Metadata
    SessionKey session_key;          // Plex session key
    ServerId server_id;              // ID of the server hosting this content
};

// Configuration structures
struct DiscordConfig {
    std::string client_id = "1359742002618564618";
    bool show_buttons = true;
    bool show_progress = true;
    bool show_artwork = true;
    std::chrono::seconds update_interval{15};

    // TV Show format defaults
    std::string tv_details_format = "{show}";
    std::string tv_state_format = "{se} - {title}";
    std::string tv_large_image_text_format = "{title}";

    // Movie format defaults
    std::string movie_details_format = "{title} ({year})";
    std::string movie_state_format = "{genres}";
    std::string movie_large_image_text_format = "{title}";

    // Music format defaults
    std::string music_details_format = "{title}";
    std::string music_state_format = "{artist} - {album}";
    std::string music_large_image_text_format = "{title}";
};

struct PresenceServiceConfig {
    PresenceServiceType type = PresenceServiceType::Discord;
    bool enabled = true;
    DiscordConfig discord;
};

// Plex-specific configuration
struct PlexServiceConfig {
    bool enabled = true;
    std::vector<std::string> server_urls;
    std::chrono::seconds poll_interval{5};
    std::chrono::seconds timeout{30};
    bool auto_discover = true;

    // Media type filters
    bool enable_movies = true;
    bool enable_tv_shows = true;
    bool enable_music = true;
};

// Container for all media service configurations
struct MediaServicesConfig {
    PlexServiceConfig plex;
};

struct ApplicationConfig {
    PresenceServiceConfig presence;
    MediaServicesConfig media_services;

    presence_for_plex::utils::LogLevel log_level = presence_for_plex::utils::LogLevel::Info;
    bool start_at_boot = false;

    // External service tokens and settings
    std::string tmdb_access_token = "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiIzNmMxOTI3ZjllMTlkMzUxZWFmMjAxNGViN2JmYjNkZiIsIm5iZiI6MTc0NTQzMTA3NC4yMjcsInN1YiI6IjY4MDkyYTIyNmUxYTc2OWU4MWVmMGJhOSIsInNjb3BlcyI6WyJhcGlfcmVhZCJdLCJ2ZXJzaW9uIjoxfQ.Td6eAbW7SgQOMmQpRDwVM-_3KIMybGRqWNK8Yqw1Zzs";
    bool enable_tmdb = true;
    bool enable_jikan = true;

    // Version information
    std::string version_string() const;
    int version_major() const;
    int version_minor() const;
    int version_patch() const;
};

// Event system types
template<typename T>
using EventCallback = std::function<void(const T&)>;

using ErrorCallback = std::function<void(std::error_code, const std::string&)>;

} // namespace core
} // namespace presence_for_plex

// Hash specializations for using strong types as map keys
namespace std {
    template<>
    struct hash<presence_for_plex::core::ServerId> {
        std::size_t operator()(const presence_for_plex::core::ServerId& id) const noexcept {
            return std::hash<std::string>{}(id.value);
        }
    };

    template<>
    struct hash<presence_for_plex::core::SessionKey> {
        std::size_t operator()(const presence_for_plex::core::SessionKey& key) const noexcept {
            return std::hash<std::string>{}(key.value);
        }
    };
}