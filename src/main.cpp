#include "main.h"

#ifdef _WIN32
// For hiding console window
#include <windows.h>
#endif

std::atomic<bool> running = true;
TrayIcon *g_trayIcon = nullptr;
Discord *g_discord = nullptr;
Plex *g_plex = nullptr;

static void signalHandler(int signum)
{
    LOG_INFO("Main", "Received signal: " + std::to_string(signum));
    running = false;

    // Force immediate cleanup of key resources
    if (g_discord)
    {
        LOG_INFO("Main", "Stopping Discord connection");
        g_discord->stop();
    }

    if (g_plex)
    {
        LOG_INFO("Main", "Stopping Plex polling");
        g_plex->stopPolling();
    }

    if (g_trayIcon)
    {
        LOG_INFO("Main", "Hiding tray icon");
        g_trayIcon->hide();
    }

    // Exit immediately if it's SIGTERM
    if (signum == SIGTERM)
    {
        LOG_INFO("Main", "SIGTERM received, exiting immediately");
        exit(0);
    }
}

int main()
{
#ifdef _WIN32
    // Hide console window on Windows
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow)
    {
        ShowWindow(consoleWindow, SW_HIDE);
    }
#endif

    // Register signal handlers for clean shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifdef _WIN32
    std::signal(SIGBREAK, signalHandler);
#endif
    // Set up logging to a file in the config directory
    Logger::getInstance().initFileLogging(Config::getConfigDirectory() / "log.txt", true); // true = clear existing file

    LOG_INFO("Main", "Plex Rich Presence starting up");

    // Apply log level from config
    Logger::getInstance().setLogLevel(static_cast<LogLevel>(Config::getInstance().getLogLevel()));

    // Plex plex = Plex();
    // g_plex = &plex;

    // Discord discord = Discord();
    // g_discord = &discord;

    TrayIcon trayIcon("Plex Rich Presence");
    g_trayIcon = &trayIcon;

    // Create the tray icon

    // Set exit callback
    trayIcon.setExitCallback([&]()
                             { running = false; });

    // Show the tray icon - this must be done after initialization
    trayIcon.setTooltip("Plex Rich Presence - Running");
    trayIcon.show();

    // A short delay to make sure the tray icon appears
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Start the Discord and Plex components
    // discord.start();
    // plex.startPolling();

    // Keep track of last state to avoid unnecessary logging
    PlaybackState lastState = PlaybackState::Stopped;

    // Main loop
    LOG_INFO("Main", "Entering main loop");
    while (running)
    {
        // Check if Discord is waiting or needs reconnection
        // if (!discord.isConnected())
        // {
        //     trayIcon.setTooltip("Plex Rich Presence - Disconnected");
        //     std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        //     continue;
        // }
        // else
        // {
        //     trayIcon.setTooltip("Plex Rich Presence - Connected");
        // }

        // // Get current playback information
        // PlaybackInfo info = plex.getCurrentPlayback();

        // // Only log state changes
        // if (info.state != lastState)
        // {
        //     LOG_INFO_STREAM("Main", "Playback state changed to: "
        //                                 << std::string(info.title)
        //                                 << (info.state == PlaybackState::Paused ? " (Paused)" : "")
        //                                 << (info.state == PlaybackState::Buffering ? " (Buffering)" : "")
        //                                 << (info.state == PlaybackState::Playing ? " (Playing)" : "")
        //                                 << (info.state == PlaybackState::Stopped ? " (Stopped)" : ""));
        //     lastState = info.state;
        // }

        // discord.updatePresence(info);

        // Avoid hammering the CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Clean up resources
    LOG_INFO("Main", "Shutting down");
    // discord.stop();
    // plex.stopPolling();
    trayIcon.hide();

    // Reset global pointers
    g_trayIcon = nullptr;
    g_discord = nullptr;
    g_plex = nullptr;

    LOG_INFO("Main", "Shutdown complete");

    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    return main();
}
#endif