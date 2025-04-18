#pragma once
#include <string>
#include <shared_mutex>
#include <memory>

enum class PlaybackState
{
    Playing,
    Paused,
    Buffering,
    Stopped,
    Unknown
};

// Playback information
struct PlaybackInfo
{
    PlaybackState state = PlaybackState::Stopped; // "playing", "paused", etc.
    std::string userId = "";
    std::string username = "";
    std::string title = "";
    std::string subtitle = ""; // show name for episodes, or empty for movies
    std::string mediaType = ""; // "movie", "episode", etc.
    std::string thumbnailUrl = "";
    std::string seasonEpisode = "";
    std::string episodeName = "";
    int64_t progress = 0;                         // in seconds
    int64_t duration = 0;                         // in seconds
    time_t startTime = 0;
};

// Thread-safe wrapper for PlaybackInfo
class SharedPlaybackInfo {
public:
    // Get a copy of the current playback information
    PlaybackInfo get() const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return info;
    }
    
    // Update the playback information
    void update(const PlaybackInfo& newInfo) {
        std::unique_lock<std::shared_mutex> lock(mutex);
        info = newInfo;
    }
    
    // Check if equals to Stopped state
    bool isStopped() const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return info.state == PlaybackState::Stopped;
    }

private:
    mutable std::shared_mutex mutex;
    PlaybackInfo info;
};
