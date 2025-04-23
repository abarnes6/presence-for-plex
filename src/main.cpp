#include "main.h"

static Application *g_app = nullptr;

static void signalHandler(int sig)
{
    LOG_INFO("Main", "Received signal " + std::to_string(sig) + ", shutting down...");
    if (g_app)
    {
        g_app->stop();
    }
}

int main()
{

#ifndef _WIN32
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
#else
    signal(SIGINT, signalHandler);
    signal(SIGBREAK, signalHandler);
#endif

    Application app;
    g_app = &app;

    if (!app.initialize())
    {
        LOG_ERROR("Main", "Application failed to initialize");
        return 1;
    }

    app.run();
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    return main();
}
#endif