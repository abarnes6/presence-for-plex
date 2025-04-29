#include "main.h"
#include "config.h"
#include "single_instance.h"

/**
 * Global application instance used by signal handlers
 */
static Application *g_app = nullptr;

/**
 * Signal handler for clean application shutdown
 * @param sig Signal number that triggered the handler
 */
void signalHandler(int sig)
{
    LOG_INFO("Main", "Received signal " + std::to_string(sig) + ", shutting down...");
    if (g_app)
    {
        g_app->stop();
    }
}

/**
 * Main program entry point
 * @return Exit code (0 for success, non-zero for errors)
 */
int main()
{
    // Check for an existing instance
    SingleInstance singleInstance("PresenceForPlex");
    if (!singleInstance.isFirstInstance()) {
        // Another instance is already running
        #ifdef _WIN32
        MessageBoxA(NULL, 
            "Another instance of Presence For Plex is already running.",
            "Presence For Plex", 
            MB_ICONINFORMATION | MB_OK);
        #else
        // For non-Windows platforms, just print a message to stderr
        fprintf(stderr, "Another instance of Presence For Plex is already running.\n");
        #endif
        return 1;
    }

    // Register signal handlers for graceful shutdown
#ifndef _WIN32
    if (signal(SIGINT, signalHandler) == SIG_ERR ||
        signal(SIGTERM, signalHandler) == SIG_ERR)
    {
        LOG_ERROR("Main", "Failed to register signal handlers");
        return 1;
    }
#else
    if (signal(SIGINT, signalHandler) == SIG_ERR ||
        signal(SIGBREAK, signalHandler) == SIG_ERR)
    {
        LOG_ERROR("Main", "Failed to register signal handlers");
        return 1;
    }
#endif

    // Initialize application
    Application app;
    g_app = &app;

    auto &config = Config::getInstance();
    LOG_INFO("Application", "Starting Presence For Plex v" + config.getVersionString());

    if (!app.initialize())
    {
        LOG_ERROR("Main", "Application failed to initialize");
        return 1;
    }

    // Run main application loop
    app.run();
    return 0;
}

#ifdef _WIN32
/**
 * Windows-specific entry point
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Delegate to the platform-independent main function
    return main();
}
#endif