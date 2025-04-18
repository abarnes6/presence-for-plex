#include "main.h"
#include "logger.h"

std::atomic<bool> running = true;

static void signalHandler(int signum)
{
    LOG_INFO("Main", "Interrupt signal (" + std::to_string(signum) + ") received.");
    running = false;
}

int main()
{
    // Set log level based on configuration or default to INFO
    Logger::getInstance().setLogLevel(LogLevel::Info);

    LOG_INFO("Main", "Plex Rich Presence starting up");

    std::signal(SIGINT, signalHandler);
    Plex plex;
    Discord discord;
    discord.start();
    plex.startPolling();

    // Keep track of last state to avoid unnecessary logging
    PlaybackState lastState = PlaybackState::Unknown;

    while (running)
    {
        PlaybackInfo info = plex.getCurrentPlayback();

        // Only log when state changes
        if (info.state != lastState)
        {
            const char *stateStr = "Unknown";
            switch (info.state)
            {
            case PlaybackState::Playing:
                stateStr = "Playing";
                break;
            case PlaybackState::Paused:
                stateStr = "Paused";
                break;
            case PlaybackState::Stopped:
                stateStr = "Stopped";
                break;
            case PlaybackState::Buffering:
                stateStr = "Buffering";
                break;
            default:
                stateStr = "Unknown";
                break;
            }

            if (!info.title.empty())
            {
                LOG_INFO("Playback", std::string(stateStr) + ": " + info.title);
            }
            else
            {
                LOG_INFO("Playback", std::string(stateStr));
            }

            lastState = info.state;
        }

        // Always update presence regardless of state
        // The Discord class handles unnecessary updates internally
        discord.updatePresence(info);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup
    LOG_INFO("Main", "Shutting down");
    plex.stopPolling();
    if (discord.isConnected())
        discord.clearPresence();
    discord.stop();
    return 0;
}