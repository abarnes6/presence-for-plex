#include "Application.h"

Application::Application()
    : discordConnectCv(), discordConnectMutex()
{
    // Set up logging
    Logger::getInstance().setLogLevel(static_cast<LogLevel>(Config::getInstance().getLogLevel()));
    Logger::getInstance().initFileLogging(Config::getConfigDirectory() / "log.txt", true);

#ifdef DEBUG
    Logger::getInstance().setLogLevel(LogLevel::Debug);
#endif
    LOG_INFO("Application", "Plex Rich Presence starting up");
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
        trayIcon = std::make_unique<TrayIcon>("Plex Rich Presence");
        trayIcon->setTooltip("Plex Rich Presence - Disconnected");
        trayIcon->show();
        trayIcon->setExitCallback([this]()
                                  {
            LOG_INFO("Application", "Exit triggered from tray icon");
            stop(); });
#endif

        plex->init();

        discord->setConnectedCallback([this]()
                                      {
#ifdef _WIN32
            trayIcon->setTooltip("Plex Rich Presence - Connected");
#endif
            plex->startPolling();
            // Signal the main loop that Discord has connected
            std::unique_lock<std::mutex> lock(discordConnectMutex);
            discordConnectCv.notify_all(); });

        discord->setDisconnectedCallback([this]()
                                         {
#ifdef _WIN32
            trayIcon->setTooltip("Plex Rich Presence - Disconnected");
#endif
            plex->stopPolling(); });

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
                // Wait for Discord to connect instead of polling
                std::unique_lock<std::mutex> lock(discordConnectMutex);
                discordConnectCv.wait(lock,
                                      [this]()
                                      { return discord->isConnected() || !running; });

                if (!running || !discord->isConnected())
                {
                    continue;
                }
            }

            PlaybackInfo info = plex->getCurrentPlayback();

            if ((abs(info.startTime - lastStartTime) > 3 && info.state != PlaybackState::Paused) ||
                info.state != lastState)
            {
                LOG_DEBUG_STREAM("Application", "Updating Discord presence: "
                                                    << std::to_string(info.startTime) << " - "
                                                    << std::to_string(lastStartTime) << " - "
                                                    << static_cast<int>(info.state) << " - "
                                                    << static_cast<int>(lastState));

                lastStartTime = info.startTime;
                lastState = info.state;
                discord->updatePresence(info);
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
