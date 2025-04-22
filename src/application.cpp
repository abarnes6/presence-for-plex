#include "application.h"

Application::Application()
    : discordConnectCv(), discordConnectMutex()
{
    // Set up logging
    Logger::getInstance().setLogLevel(static_cast<LogLevel>(Config::getInstance().getLogLevel()));
    Logger::getInstance().initFileLogging(Config::getConfigDirectory() / "log.txt", true);

#ifndef NDEBUG
    Logger::getInstance().setLogLevel(LogLevel::Debug);
#endif
    LOG_INFO("Application", "Plex Presence starting up");
}

Application::~Application()
{
    shutdown();
    LOG_INFO("Application", "Application shutdown complete");
}

bool Application::initialize()
{
    try
    {
        plex = std::make_unique<Plex>();
        discord = std::make_unique<Discord>();

#ifdef _WIN32
        trayIcon = std::make_unique<TrayIcon>("Plex Presence");
        trayIcon->show();
        trayIcon->setExitCallback([this]()
                                  {
            LOG_INFO("Application", "Exit triggered from tray icon");
            stop(); });
        trayIcon->setReloadConfigCallback([this]()
                                          {
            LOG_INFO("Application", "Reload configuration triggered from tray icon");
            Config::getInstance().loadConfig(); });
        trayIcon->setOpenConfigLocationCallback([]()
                                                {
            LOG_INFO("Application", "Open config location triggered from tray icon");
            std::wstring wPath = std::filesystem::path(Config::getInstance().getConfigDirectory()).wstring();
            ShellExecuteW(NULL, L"open", wPath.c_str(), NULL, NULL, SW_SHOWNORMAL); });

#endif

        plex->init();

        discord->setConnectedCallback([this]()
                                      {
            trayIcon->setConnectionStatus("Status: Waiting for Plex...");
            plex->startPolling();
            std::unique_lock<std::mutex> lock(discordConnectMutex);
            discordConnectCv.notify_all(); });

        discord->setDisconnectedCallback([this]()
                                         { 
                                            trayIcon->setConnectionStatus("Status: Waiting for Discord...");
                                            plex->stopPolling(); });

        discord->start();
        trayIcon->setConnectionStatus("Status: Waiting for Discord...");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Application", "Initialization failed: " + std::string(e.what()));
        return false;
    }
}

void Application::run()
{
    running = true;
    LOG_DEBUG("Application", "Entering main loop");

    while (running)
    {
        try
        {
            if (!discord->isConnected())
            {
                // Wait for Discord to connect instead of polling, with timeout to check running status
                std::unique_lock<std::mutex> lock(discordConnectMutex);
                discordConnectCv.wait_for(lock,
                                          std::chrono::seconds(1),
                                          [this]()
                                          { return discord->isConnected() || !running; });

                if (!running || !discord->isConnected())
                {
                    continue;
                }
            }

            PlaybackInfo info = plex->getCurrentPlayback();
            if (info.state == PlaybackState::Stopped)
            {
                trayIcon->setConnectionStatus("Status: No active sessions");
            }
            else if (info.state == PlaybackState::Playing)
            {
                trayIcon->setConnectionStatus("Status: Playing");
            }
            else if (info.state == PlaybackState::Paused)
            {
                trayIcon->setConnectionStatus("Status: Paused");
            }
            else if (info.state == PlaybackState::Buffering)
            {
                trayIcon->setConnectionStatus("Status: Buffering...");
            }
            else if (info.state == PlaybackState::BadUrl)
            {
                trayIcon->setConnectionStatus("Status: Invalid Plex URL");
            }
            else
            {
                trayIcon->setConnectionStatus("Status: Connecting to Plex...");
            }

            if (info.state != PlaybackState::BadUrl)
            {
                if (abs(info.startTime - lastStartTime) > Config::getInstance().getPollInterval() + 1)
                {
                    LOG_WARNING("Application", "Playback position changed significantly, updating Discord presence");
                    discord->updatePresence(info);
                }
                else if (info.state != lastState)
                {
                    LOG_DEBUG("Application", "Playback state changed, updating Discord presence");
                    discord->updatePresence(info);
                }
                lastState = info.state;
                lastStartTime = info.startTime;
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Application", "Error in main loop: " + std::string(e.what()));
        }

        std::this_thread::sleep_for(std::chrono::seconds(Config::getInstance().getPollInterval()));
    }
}

void Application::stop()
{
    running = false;
}

void Application::shutdown()
{
    if (discord)
    {
        discord->stop();
    }

    if (plex)
    {
        plex->stopPolling();
    }

#ifdef _WIN32
    if (trayIcon)
    {
        trayIcon->hide();
    }
#endif
}
