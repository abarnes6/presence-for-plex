#include "application.h"
#include "version.h"

Application::Application()
{
    setupLogging();
}

void Application::setupLogging()
{
    // Set up logging
    Logger::getInstance().setLogLevel(static_cast<LogLevel>(Config::getInstance().getLogLevel()));
    Logger::getInstance().initFileLogging(Config::getConfigDirectory() / "log.txt", true);

#ifndef NDEBUG
    Logger::getInstance().setLogLevel(LogLevel::Debug);
#endif
    LOG_INFO("Application", "Presence For Plex starting up");
}

void Application::setupDiscordCallbacks()
{
    m_discord->setConnectedCallback([this]()
                                    {
#ifdef _WIN32
        // Check if this is first launch by looking for auth token
        bool isFirstLaunch = Config::getInstance().getPlexAuthToken().empty();
        if (isFirstLaunch) {
            m_trayIcon->setConnectionStatus("Status: Setup Required");
        } else {
            m_trayIcon->setConnectionStatus("Status: Connecting to Plex...");
        }
#endif
        m_plex->init();
        std::unique_lock<std::mutex> lock(m_discordConnectMutex);
        m_discordConnectCv.notify_all();
                                    });

        m_discord->setDisconnectedCallback([this]()
                                           {
            m_plex->stop();
#ifdef _WIN32
            m_trayIcon->setConnectionStatus("Status: Waiting for Discord...");
#endif
                                           });
}

bool Application::initialize()
{
    try
    {
        m_plex = std::make_unique<Plex>();
        m_discord = std::make_unique<Discord>();
#ifdef _WIN32
        m_trayIcon = std::make_unique<TrayIcon>("Presence For Plex");
#endif

#ifdef _WIN32
        m_trayIcon->setExitCallback([this]()
                                    {
            LOG_INFO("Application", "Exit triggered from tray icon");
            stop(); });
        m_trayIcon->setUpdateCheckCallback([this]()
                                           { checkForUpdates(); });
        m_trayIcon->show();
#endif

        setupDiscordCallbacks();

        m_discord->start();
        m_initialized = true;
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Application", "Initialization failed: " + std::string(e.what()));
        return false;
    }
}

void Application::checkForUpdates()
{
    LOG_INFO("Application", "Checking for updates...");

    // Get current version from version.h
    std::string currentVersion = VERSION_STRING;
    LOG_DEBUG("Application", "Current version: " + currentVersion);

    try
    {
        // Fetch the latest release information from GitHub
        std::string apiUrl = "https://api.github.com/repos/abarnes6/presence-for-plex/releases/latest";

        // Setup headers required by GitHub API
        std::map<std::string, std::string> headers = {
            {"User-Agent", "Presence-For-Plex-Update-Checker"},
            {"Accept", "application/json"}};

        // Response from GitHub API
        std::string response;

        // Create HTTP client instance
        HttpClient httpClient;

        if (httpClient.get(apiUrl, headers, response))
        {
            // Parse the JSON response
            try
            {
                auto releaseInfo = json::parse(response);

                // Extract the tag name (version) from the release
                std::string latestVersion = releaseInfo["tag_name"];
                // Remove 'v' prefix if present
                if (!latestVersion.empty() && latestVersion[0] == 'v')
                {
                    latestVersion = latestVersion.substr(1);
                }

                LOG_INFO("Application", "Latest version: " + latestVersion);

                // Compare versions
                bool updateAvailable = (latestVersion != currentVersion);

                std::string message;
                std::string downloadUrl = "";

                if (updateAvailable)
                {
                    message = "An update is available!\n";
                    message += "Latest version: " + latestVersion + " (current: " + currentVersion + ")\n\n";

                    // Add download URL or update instructions
                    downloadUrl = releaseInfo["html_url"];
                    message += "Click to open the download page.";

                    LOG_INFO("Application", "Update available: " + latestVersion);
                }
                else
                {
                    message = "You are running the latest version.\n\n";
                    message += "Current version: " + currentVersion;

                    LOG_INFO("Application", "No updates available");
                }

#ifdef _WIN32
                // Show the result as a notification
                if (updateAvailable)
                {
                    m_trayIcon->showUpdateNotification("Presence For Plex Update", message, downloadUrl);
                }
                else
                {
                    m_trayIcon->showNotification("Presence For Plex Update", message);
                }
#endif
            }
            catch (const json::exception &e)
            {
                std::string errorMsg = "Failed to parse GitHub response: " + std::string(e.what());
                LOG_ERROR("Application", errorMsg);

#ifdef _WIN32
                m_trayIcon->showNotification("Update Check Failed", errorMsg, true);
#endif
            }
        }
        else
        {
            std::string errorMsg = "Failed to check for updates: Could not connect to GitHub.";
            LOG_ERROR("Application", errorMsg);

#ifdef _WIN32
            m_trayIcon->showNotification("Update Check Failed", errorMsg, true);
#endif
        }
    }
    catch (const std::exception &e)
    {
        std::string errorMsg = "Failed to check for updates: " + std::string(e.what());
        LOG_ERROR("Application", errorMsg);

#ifdef _WIN32
        m_trayIcon->showNotification("Update Check Failed", errorMsg, true);
#endif
    }
}

void Application::updateTrayStatus(const MediaInfo &info)
{
#ifdef _WIN32
    if (info.state == PlaybackState::Stopped)
    {
        m_trayIcon->setConnectionStatus("Status: No active sessions");
    }
    else if (info.state == PlaybackState::Playing)
    {
        m_trayIcon->setConnectionStatus("Status: Playing");
    }
    else if (info.state == PlaybackState::Paused)
    {
        m_trayIcon->setConnectionStatus("Status: Paused");
    }
    else if (info.state == PlaybackState::Buffering)
    {
        m_trayIcon->setConnectionStatus("Status: Buffering...");
    }
    else if (info.state == PlaybackState::BadToken)
    {
        m_trayIcon->setConnectionStatus("Status: Invalid Plex token");
    }
    else
    {
        m_trayIcon->setConnectionStatus("Status: Connecting to Plex...");
    }
#endif
}

void Application::processPlaybackInfo(const MediaInfo &info)
{
    if (info.state != PlaybackState::BadToken && info.state != PlaybackState::NotInitialized)
    {
        if (info.state != m_lastState || (info.state == PlaybackState::Playing && abs(info.startTime - m_lastStartTime) > 5))
        {
            LOG_DEBUG("Application", "Playback state changed, updating Discord presence to " + std::to_string(static_cast<int>(info.state)));
            m_discord->updatePresence(info);
        }
        m_lastStartTime = info.startTime;
        m_lastState = info.state;
    }
    else if (info.state == PlaybackState::NotInitialized)
    {
        LOG_INFO("Application", "Plex class not initialized, skipping update");
        m_lastState = PlaybackState::NotInitialized;
    }
    else
    {
        LOG_ERROR("Application", "Invalid Plex token, stopping Discord presence updates");
        m_discord->clearPresence();
        m_lastState = PlaybackState::BadToken;
    }
}

void Application::run()
{
    m_running = true;
    LOG_DEBUG("Application", "Entering main loop");

    while (m_running)
    {
        try
        {
            if (!m_discord->isConnected())
            {
                std::unique_lock<std::mutex> lock(m_discordConnectMutex);
                // Reduce wait time to be more responsive to shutdown
                m_discordConnectCv.wait_for(lock,
                                            std::chrono::milliseconds(500),
                                            [this]()
                                            { return m_discord->isConnected() || !m_running; });

                if (!m_running || !m_discord->isConnected())
                {
                    continue;
                }
            }

            MediaInfo info = m_plex->getCurrentPlayback();

            updateTrayStatus(info);
            processPlaybackInfo(info);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Application", "Error in main loop: " + std::string(e.what()));
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    performCleanup();
}

void Application::performCleanup()
{
    LOG_INFO("Application", "Stopping application");
    m_running = false;

    // Launch cleanup operations in parallel
    std::vector<std::future<void>> cleanupTasks;

#ifdef _WIN32
    if (m_trayIcon)
    {
        LOG_INFO("Application", "Destroying tray icon");
        try
        {
            m_trayIcon->hide();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Application", "Error hiding tray icon: " + std::string(e.what()));
        }
    }
#endif

    if (m_plex)
    {
        LOG_INFO("Application", "Cleaning up Plex connections");
        cleanupTasks.push_back(std::async(std::launch::async, [this]()
                                          {
            try {
                m_plex->stop();
            } catch (const std::exception& e) {
                LOG_ERROR("Application", "Exception during Plex cleanup: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("Application", "Unknown exception during Plex cleanup");
            } }));
    }

    if (m_discord)
    {
        LOG_INFO("Application", "Stopping Discord connection");
        cleanupTasks.push_back(std::async(std::launch::async, [this]()
                                          {
            try {
                m_discord->stop();
            } catch (const std::exception& e) {
                LOG_ERROR("Application", "Exception during Discord cleanup: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("Application", "Unknown exception during Discord cleanup");
            } }));
    }

    // Wait for all cleanup tasks with timeout
    for (auto &task : cleanupTasks)
    {
        if (task.wait_for(std::chrono::seconds(5)) == std::future_status::timeout)
        {
            LOG_WARNING("Application", "A cleanup task did not complete within the timeout");
        }
    }

    LOG_INFO("Application", "Application stopped");
}

void Application::stop()
{
    LOG_INFO("Application", "Stop requested");

    if (!m_initialized)
    {
        performCleanup();
        return;
    }

    // Set running to false and wake up any waiting threads
    m_running = false;

    // Wake up thread waiting for Discord connection if we're in that state
    std::unique_lock<std::mutex> lock(m_discordConnectMutex);
    m_discordConnectCv.notify_all();
}
