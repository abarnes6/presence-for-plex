#pragma once

#include "plex.h"
#include "discord.h"
#include "config.h"
#include "trayicon.h"
#include <memory>
#include <atomic>
#include <condition_variable>
#include <mutex>

class Application
{
private:
    std::unique_ptr<Plex> plex;
    std::unique_ptr<Discord> discord;
#ifdef _WIN32
    std::unique_ptr<TrayIcon> trayIcon;
#endif
    std::atomic<bool> running{false};
    time_t lastStartTime = 0;
    PlaybackState lastState = PlaybackState::Stopped;
    std::condition_variable discordConnectCv;
    std::mutex discordConnectMutex;

public:
    Application();

    bool initialize();
    void run();
    void stop();
};
