#pragma once

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

// Validation error types
enum class ValidationError {
    // MediaInfo errors
    EmptyTitle,
    InvalidDuration,
    ProgressOutOfBounds,
    MissingEpisodeInfo,
    MissingSeasonInfo,

    // Config errors
    InvalidUpdateInterval,
    InvalidPollInterval,
    EmptyClientId,
    InvalidServerUrl,
    EmptyServerName,
    EmptyAuthToken,

    // General
    InvalidFormat,
    MissingRequiredField
};

// Configuration validation limits
namespace ConfigLimits {
    constexpr auto MIN_UPDATE_INTERVAL = std::chrono::seconds(1);
    constexpr auto MAX_UPDATE_INTERVAL = std::chrono::seconds(300);
    constexpr auto MIN_POLL_INTERVAL = std::chrono::seconds(1);
    constexpr auto MAX_POLL_INTERVAL = std::chrono::seconds(60);
    constexpr auto MIN_TIMEOUT = std::chrono::seconds(5);
    constexpr auto MAX_TIMEOUT = std::chrono::seconds(300);
}

// Forward declarations
class HttpClient;

// Log levels for configuration
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

// Helper functions for LogLevel
inline std::string to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error: return "error";
        case LogLevel::Critical: return "critical";
        default: return "info";
    }
}

inline LogLevel log_level_from_string(const std::string& str) {
    if (str == "trace") return LogLevel::Trace;
    if (str == "debug") return LogLevel::Debug;
    if (str == "info") return LogLevel::Info;
    if (str == "warning") return LogLevel::Warning;
    if (str == "error") return LogLevel::Error;
    if (str == "critical") return LogLevel::Critical;
    return LogLevel::Info; // Default
}

enum class PlaybackState {
    Stopped,       // No active session
    Playing,       // Media is playing
    Paused,        // Media is paused
    Buffering,     // Media is buffering
    BadToken,      // Server configuration issue
    NotInitialized // Server not initialized
};

enum class MediaType {
    Movie,
    TVShow,
    Music,
    Unknown
};

// Error types for better error handling
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

enum class ConfigError {
    FileNotFound,
    InvalidFormat,
    ValidationError,
    PermissionDenied
};

// Strong types for IDs and tokens
struct PlexToken {
    std::string value;

    PlexToken() = default;
    explicit PlexToken(std::string token) : value(std::move(token)) {}

    bool empty() const { return value.empty(); }
    const std::string& get() const { return value; }
};

struct ClientId {
    std::string value;

    ClientId() = default;
    explicit ClientId(std::string id) : value(std::move(id)) {}

    bool empty() const { return value.empty(); }
    const std::string& get() const { return value; }
};

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
    std::chrono::system_clock::time_point start_time;

    // Metadata
    SessionKey session_key;          // Plex session key
    ServerId server_id;              // ID of the server hosting this content

    // Validation methods
    bool is_valid() const;  // Legacy method for compatibility
    std::expected<void, ValidationError> validate() const;
};

// Configuration structures
struct DiscordConfig {
    std::string client_id = "1359742002618564618";  // Default Discord client ID
    bool show_buttons = true;
    bool show_progress = true;
    bool show_artwork = true;
    std::chrono::seconds update_interval{15};
    std::string details_format = "{title}";
    std::string state_format = "{state}";

    bool is_valid() const;  // Legacy method for compatibility
    std::expected<void, ValidationError> validate() const;
};

struct PlexConfig {
    // For now, we'll handle servers separately in runtime, and just store basic config here
    std::vector<std::string> server_urls;  // Basic URLs, actual PlexServer objects created at runtime
    std::chrono::seconds poll_interval{5};
    std::chrono::seconds timeout{30};
    bool auto_discover = true;

    bool is_valid() const;  // Legacy method for compatibility
    std::expected<void, ValidationError> validate() const;
};

struct ApplicationConfig {
    DiscordConfig discord;
    PlexConfig plex;
    LogLevel log_level = LogLevel::Info;
    bool start_minimized = false;

    // External service tokens and settings
    std::string tmdb_access_token = "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiIzNmMxOTI3ZjllMTlkMzUxZWFmMjAxNGViN2JmYjNkZiIsIm5iZiI6MTc0NTQzMTA3NC4yMjcsInN1YiI6IjY4MDkyYTIyNmUxYTc2OWU4MWVmMGJhOSIsInNjb3BlcyI6WyJhcGlfcmVhZCJdLCJ2ZXJzaW9uIjoxfQ.Td6eAbW7SgQOMmQpRDwVM-_3KIMybGRqWNK8Yqw1Zzs";
    bool enable_tmdb = true;
    bool enable_jikan = true;

    // Version information
    std::string version_string() const;
    int version_major() const;
    int version_minor() const;
    int version_patch() const;

    bool is_valid() const;  // Legacy method for compatibility
    std::expected<void, ValidationError> validate() const;
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
    struct hash<presence_for_plex::core::ClientId> {
        std::size_t operator()(const presence_for_plex::core::ClientId& id) const noexcept {
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