#include "main.h"

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