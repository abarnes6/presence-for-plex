# Presence For Plex

A lightweight application written in C++ that displays your current Plex media activity in Discord's Rich Presence.

## Features

-   Show what you're watching on Plex in your Discord status
-   Displays show titles, episode information, and progress
-   Runs in the system tray for easy access (Windows only)

## Installation

### Download

1. (Windows only) Ensure you have the latest [C++ Redistributables](https://aka.ms/vs/17/release/vc_redist.x64.exe) 
2. Download the latest release from the [Releases](https://github.com/abarnes6/presence-for-plex/releases) page.

### Setup

1. Run the executable
2. Connect your Plex account in a browser when prompted
3. The application will automatically connect to Plex/Discord

## Building from Source

### Requirements

-   C++17 compatible compiler
-   CMake 3.25+
-   Ninja
-   (Windows only) Windows 11 SDK
-   (Windows only) [NSIS3](https://prdownloads.sourceforge.net/nsis/nsis-3.11-setup.exe?download)

### Build Instructions

If on Windows, use a Visual Studio terminal with CMake tools and vcpkg components installed. This is also how you would need to open VS code to compile with it.

```bash
git clone https://github.com/abarnes6/presence-for-plex.git
cd presence-for-plex
mkdir build && cd build
cmake --preset=release ..
cmake --build release
```

## Troubleshooting

Check the log file located at:

-   Windows: `%APPDATA%\Presence For Plex\log.txt`
-   macOS/Linux: `~/.config/presence-for-plex/log.txt`

## FAQ

### Why does Presence For Plex show "No active sessions"?

If the application is connecting to your Plex server but not detecting your media playback:

1. Check your Plex server's network settings
2. Go to Plex server Settings → Network
3. Verify that "Preferred network interface" is set correctly
    - If set to "Auto", try selecting your specific network interface instead
    - This is particularly important for servers with multiple network interfaces

### Why do the buttons not work?

They do, but only for others! For some reason Discord doesn't like to show you your own rich presence buttons.

### Could not connect to any Discord socket. Is Discord running?

You need to have a Discord app running in the same machine as the application.

It will try to connect to Discord using the following files:

-   Windows: `\\.pipe\discord-ipc-0` to `discord-ipc-9`
-   macOS: `$TMPDIR/discord-ipc-0` to `discord-ipc-9`
-   Linux:
    - `$XDG_RUNTIME_DIR/discord-ipc-0` to `discord-ipc-9`
    - `$HOME/.discord-ipc-0` to `discord-ipc-9`
    - `/var/run/$UID/snap.discord/discord-ipc-0`
    - `/var/run/$UID/app/com.discordapp.Discord/discord-ipc-0`

If none of those succeed, it means it can not talk to your Discord local client.

## Attribution

![blue_square_2-d537fb228cf3ded904ef09b136fe3fec72548ebc1fea3fbbd1ad9e36364db38b](https://github.com/user-attachments/assets/38abfb34-72cf-46d9-9d17-724761aa570a)

(image API)

## License

[MIT License](LICENSE)
