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
        if (!info.isPlaying)
        {
            std::cout << "No active playback" << std::endl;
            if (discord.isConnected())
                discord.clearPresence();
        }
        else
        {
            std::cout << "Now playing: " << info.title << std::endl;
            discord.updatePresence(info);
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