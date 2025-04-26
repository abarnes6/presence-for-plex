#pragma once

// Standard library headers
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

// Third-party headers
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Project headers
#include "config.h"
#include "discord.h"
#include "plex.h"
#include "trayicon.h"

class Application
{
public:
    Application();

    bool initialize();
    void run();
    void stop();

private:
    std::unique_ptr<Plex> m_plex;
    std::unique_ptr<Discord> m_discord;
#ifdef _WIN32
    std::unique_ptr<TrayIcon> m_trayIcon;
#endif
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_initialized{false};
    PlaybackState m_lastState = PlaybackState::Stopped;
    std::condition_variable m_discordConnectCv;
    std::mutex m_discordConnectMutex;
    time_t m_lastStartTime = 0;

    // Helper methods for improved readability
    void setupLogging();
    void setupDiscordCallbacks();
    void updateTrayStatus(const MediaInfo &info);
    void processPlaybackInfo(const MediaInfo &info);
    void performCleanup();
    void checkForUpdates();
};
