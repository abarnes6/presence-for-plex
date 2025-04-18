#include "main.h"

std::atomic<bool> running = true;

static void signalHandler(int signum) {
	std::cout << "Interrupt signal (" << signum << ") received.\n";
	running = false;
}

int main()
{
    std::signal(SIGINT, signalHandler);
    //TrayIcon trayIcon = TrayIcon();
    Plex plex = Plex();
    //DiscordClient discord = DiscordClient();

	plex.startPolling();

    while (running) {

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return 0;
}