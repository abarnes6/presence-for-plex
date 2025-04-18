#include "main.h"

std::atomic<bool> running = true;

static void signalHandler(int signum)
{
    LOG_INFO_STREAM("SignalHandler", "Interrupt signal (" << signum << ") received.");
    running = false;
}

int main()
{
    // Set log level based on configuration or default to INFO
    Logger::getInstance().setLogLevel(LogLevel::Info);

    LOG_INFO("Main", "Plex Rich Presence starting up");

    std::signal(SIGINT, signalHandler);
    Plex plex = Plex();
    Discord discord = Discord();
    
    LOG_INFO("Main", "Creating tray icon");
    
    // Create the tray icon
    TrayIcon trayIcon("Plex Rich Presence");
    
    // Set exit callback
    trayIcon.setExitCallback([&]() {
        LOG_INFO("Main", "Exit requested from tray");
        running = false;
    });
    
    // Show the tray icon - this must be done after initialization
    trayIcon.setTooltip("Plex Rich Presence - Running");
    trayIcon.show();
    
    // A short delay to make sure the tray icon appears
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Start the Discord and Plex components
    discord.start();
    plex.startPolling();

    // Keep track of last state to avoid unnecessary logging
    PlaybackState lastState = PlaybackState::Stopped;

    // Main application loop
    while (running)
    {
        PlaybackInfo info = plex.getCurrentPlayback();

        // Only log when state changes
        if (info.state != lastState)
        {
            lastState = info.state;
            switch (info.state)
            {
            case PlaybackState::Playing:
                trayIcon.setTooltip("Plex Rich Presence - Playing: " + info.title);
                break;
            case PlaybackState::Paused:
                trayIcon.setTooltip("Plex Rich Presence - Paused: " + info.title);
                break;
            case PlaybackState::Stopped:
                trayIcon.setTooltip("Plex Rich Presence - Stopped");
                break;
            case PlaybackState::Buffering:
                trayIcon.setTooltip("Plex Rich Presence - Buffering: " + info.title);
                break;
            }

            LOG_INFO_STREAM("Main", "Playback state changed to: " 
                << (info.state == PlaybackState::Paused ? " (Paused)" : "")
                << (info.state == PlaybackState::Buffering ? " (Buffering)" : "")
                << (info.state == PlaybackState::Playing ? " (Playing)" : "")
                << (info.state == PlaybackState::Stopped ? " (Stopped)" : ""));
            lastState = info.state;
        }

        discord.updatePresence(info);

        // Avoid hammering the CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Clean up resources
    LOG_INFO("Main", "Shutting down");
    discord.stop();
    plex.stopPolling();
    trayIcon.hide();

    return 0;
}