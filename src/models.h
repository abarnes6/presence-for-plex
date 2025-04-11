#pragma once
#include <string>
#include <shared_mutex>

// Configuration structure
struct PlexConfig {
    std::string serverUrl;
    std::string authToken;
    int pollInterval;  // seconds
    std::uint64_t clientId; // Discord application client ID
};

// Playback information
struct PlaybackInfo {
    bool isPlaying = false;
    std::string mediaType = "";  // "movie", "episode", etc.
    std::string title = "";
    std::string subtitle = "";   // show name for episodes, or empty for movies
    std::string thumbnailUrl = "";
    std::string userId = "";
    std::string username = "";
    int progress = 0;   // in seconds
    int duration = 0;   // in seconds
    time_t startTime = 0;
};

