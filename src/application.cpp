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

bool Application::initialize()
{
    try
    {
        plex = std::make_unique<Plex>();
        discord = std::make_unique<Discord>();

#ifdef _WIN32
        trayIcon = std::make_unique<TrayIcon>("Plex Presence");
        trayIcon->show();
        trayIcon->setConnectionStatus("Status: Waiting for Discord...");
        trayIcon->setExitCallback([this]()
                                  {
            LOG_INFO("Application", "Exit triggered from tray icon");
            stop(); });

#endif

        discord->setConnectedCallback([this]()
                                      {
#ifdef _WIN32
                trayIcon->setConnectionStatus("Status: Connecting to Plex...");

#endif
            std::unique_lock<std::mutex> lock(discordConnectMutex);
            discordConnectCv.notify_all(); });

        discord->setDisconnectedCallback([this]()
                                         {
#ifdef _WIN32
                                             trayIcon->setConnectionStatus("Status: Waiting for Discord...");
#endif
                                         });

        plex->init();
        discord->start();
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

            MediaInfo info = plex->getCurrentPlayback();

#ifdef _WIN32
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
            else if (info.state == PlaybackState::BadToken)
            {
                trayIcon->setConnectionStatus("Status: Invalid Plex token");
            }
            else
            {
                trayIcon->setConnectionStatus("Status: Connecting to Plex...");
            }
#endif

            if (info.state != PlaybackState::BadToken)
            {
                if (info.state != lastState)
                {
                    LOG_DEBUG("Application", "Playback state changed, updating Discord presence");
                    discord->updatePresence(info);
                }
                lastState = info.state;
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Application", "Error in main loop: " + std::string(e.what()));
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    LOG_INFO("Application", "Stopping application");
    running = false;

    // Clean up Plex (this will stop all SSE connections)
    if (plex)
    {
        LOG_INFO("Application", "Cleaning up Plex connections");
        // Don't immediately reset - call cleanup method first
        plex->stopConnections();
    }

    // Stop Discord first
    if (discord)
    {
        LOG_INFO("Application", "Stopping Discord connection");
        discord->stop();
    }

#ifdef _WIN32
    if (trayIcon)
    {
        LOG_INFO("Application", "Destroying tray icon");
        trayIcon->hide();
    }
#endif

    LOG_INFO("Application", "Application stopped");
}

void Application::stop()
{
    LOG_INFO("Application", "Stop requested");
    running = false;
}