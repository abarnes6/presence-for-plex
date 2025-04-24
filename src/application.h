#pragma once

// Standard library headers
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

// Project headers
#include "config.h"
#include "discord.h"
#include "plex.h"
#include "trayicon.h"

class Application
{
private:
    std::unique_ptr<Plex> plex;
    std::unique_ptr<Discord> discord;
#ifdef _WIN32
    std::unique_ptr<TrayIcon> trayIcon;
#endif
    std::atomic<bool> running{false};
    PlaybackState lastState = PlaybackState::Stopped;
    std::condition_variable discordConnectCv;
    std::mutex discordConnectMutex;

    // Helper methods for improved readability
    void setupLogging();
    void setupDiscordCallbacks();
    void updateTrayStatus(const MediaInfo &info);
    void processPlaybackInfo(const MediaInfo &info);
    void performCleanup();

public:
    Application();

    bool initialize();
    void run();
    void stop();
};
