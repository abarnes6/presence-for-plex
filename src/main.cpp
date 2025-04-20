#include "main.h"

#ifdef _WIN32
TrayIcon *g_trayIcon = nullptr;
#endif
Discord *g_discord = nullptr;
Plex *g_plex = nullptr;

static void signalHandler(int signum)
{
    LOG_INFO("Main", "Received signal: " + std::to_string(signum));

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

#ifdef _WIN32
    if (g_trayIcon)
    {
        LOG_INFO("Main", "Hiding tray icon");
        g_trayIcon->hide();
    }
#endif

    LOG_INFO("Main", "Signal received, exiting immediately");
    exit(0);
}

int main()
{
#ifdef DEBUG
    LOG_INFO("Main", "Debug mode enabled");
    Logger::getInstance().setLogLevel(LogLevel::Debug);
#else
    Logger::getInstance().setLogLevel(static_cast<LogLevel>(Config::getInstance().getLogLevel()));
#endif

    Logger::getInstance().initFileLogging(Config::getConfigDirectory() / "log.txt", true); // true = clear existing file

    LOG_INFO("Main", "Plex Rich Presence starting up");

#ifdef _WIN32
    // Hide console window on Windows
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow)
    {
        ShowWindow(consoleWindow, SW_HIDE);
    }
#endif

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

#ifdef _WIN32
    std::signal(SIGBREAK, signalHandler);
#endif

    Plex plex = Plex();
    g_plex = &plex;

    Discord discord = Discord();
    g_discord = &discord;

#ifdef _WIN32
    TrayIcon trayIcon("Plex Rich Presence");
    g_trayIcon = &trayIcon;
    trayIcon.setTooltip("Plex Rich Presence");
    trayIcon.show();
    trayIcon.setExitCallback([&]()
                             {
        LOG_INFO("Main", "Exit triggered from tray icon");

        discord.stop();
        plex.stopPolling();
        
        std::thread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            exit(0);
        }).detach(); });
#endif

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    discord.start();
    plex.startPolling();

    PlaybackState lastState = PlaybackState::Stopped;

    LOG_INFO("Main", "Entering main loop");
    while (true)
    {
        if (!discord.isConnected())
        {
#ifdef _WIN32
            trayIcon.setTooltip("Plex Rich Presence - Disconnected");
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            continue;
        }
        else
        {
#ifdef _WIN32
            trayIcon.setTooltip("Plex Rich Presence - Connected");
#endif
        }

        PlaybackInfo info = plex.getCurrentPlayback();

        if (info.state != lastState)
        {
            LOG_INFO_STREAM("Main", "Playback state changed to: "
                                        << std::string(info.title)
                                        << (info.state == PlaybackState::Paused ? " (Paused)" : "")
                                        << (info.state == PlaybackState::Buffering ? " (Buffering)" : "")
                                        << (info.state == PlaybackState::Playing ? " (Playing)" : "")
                                        << (info.state == PlaybackState::Stopped ? " (Stopped)" : ""));
            lastState = info.state;
        }

        discord.updatePresence(info);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    return main();
}
#endif