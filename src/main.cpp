#include "main.h"

std::atomic<bool> running = true;

static void signalHandler(int signum)
{
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    running = false;
}

int main()
{
    std::signal(SIGINT, signalHandler);
    Plex plex;
    Discord discord;
    discord.start();
    plex.startPolling();

    while (running)
    {
        PlaybackInfo info = plex.getCurrentPlayback();
        if (info.state == PlaybackState::Playing)
        {
            std::cout << "Playing: " << info.title << std::endl;
            discord.updatePresence(info);
        }
        else if (info.state == PlaybackState::Paused)
        {
            std::cout << "Paused: " << info.title << std::endl;
            discord.updatePresence(info);
        }
        else if (info.state == PlaybackState::Stopped)
        {
            std::cout << "Stopped: " << info.title << std::endl;
            discord.updatePresence(info);
        }
        else if (info.state == PlaybackState::Buffering)
        {
            std::cout << "Buffering: " << info.title << std::endl;
            discord.updatePresence(info);
        }
        else
        {
            std::cout << "Unknown state: " << info.title << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup
    plex.stopPolling();
    if (discord.isConnected())
        discord.clearPresence();
    discord.stop();
    return 0;
}