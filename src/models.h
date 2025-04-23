#pragma once
#include "http_client.h"
#include <string>
#include <shared_mutex>
#include <memory>
#include <mutex>
#include <ctime>

// Define the PlexServer struct
struct PlexServer
{
    std::string name;
    std::string clientIdentifier;
    std::string localUri;
    std::string publicUri;
    std::string accessToken;
    std::chrono::system_clock::time_point lastUpdated;
    std::unique_ptr<HttpClient> httpClient;
    std::atomic<bool> running;
    bool owned = false;
};

enum class PlaybackState
{
    Stopped,   // No active session
    Playing,   // Media is playing
    Paused,    // Media is paused
    Buffering, // Media is buffering
    BadToken   // Server configuration issue
};

enum class MediaType
{
    Movie,
    TVShow,
    Unknown
};

enum class LinkType
{
    IMDB,
    MAL,
    TMDB,
    TVDB,
    Unknown
};

// Playback information
struct MediaInfo
{
    // General
    std::string title;               // Title of the media
    std::string originalTitle;       // Original title (original language)
    MediaType type;                  // Type of media (movie, TV show)
    std::string artPath;             // Path to art on the server (cover image)
    int year;                        // Year of release
    std::string summary;             // Summary of the media
    std::vector<std::string> genres; // List of genres
    std::string imdbId;              // IMDB ID (if applicable)
    std::string tmdbId;              // TMDB ID (if applicable)
    std::string tvdbId;              // TVDB ID (if applicable)
    std::string malId;               // MyAnimeList ID (if applicable)

    // TV Show specific
    std::string grandparentTitle; // Parent title (tv show name)
    std::string grandparentArt;   // Parent art URL (tv show cover image)
    std::string grandparentKey;   // Parent ID (tv show ID)
    int season;                   // Season number
    int episode;                  // Episode number

    // Playback info
    std::string username; // Username of the person watching
    PlaybackState state;  // Current playback state
    double progress;      // Current progress in seconds
    double duration;      // Total duration in seconds
    time_t startTime;     // When the playback started

    // Misc
    std::string sessionKey; // Plex session key
    std::string serverId;   // ID of the server hosting this content

    MediaInfo() : state(PlaybackState::Stopped),
                  progress(0),
                  duration(0),
                  startTime(0),
                  type(MediaType::Unknown) {}
};