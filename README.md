# Plex Presence

A lightweight application written in C++ that displays your current Plex media activity in Discord's Rich Presence.

## Features

-   Show what you're watching on Plex in your Discord status
-   Displays show titles, episode information, and progress
-   Runs in the system tray for easy access (Windows only)

## Installation

### Download

Download the latest release from the [Releases](https://github.com/abarnes6/plex-presence/releases) page.

### Setup

1. Run the executable
2. Connect your Plex account in a browser when prompted
3. The application will automatically connect to Plex/Discord

## Building from Source

### Requirements

-   C++17 compatible compiler
-   CMake 3.25+
-   Ninja
-   vcpkg

### Build Instructions

If on Windows, use a Visual Studio terminal with CMake tools and vcpkg components installed. This is also how you would need to open VS code to compile with it.

```bash
git clone https://github.com/abarnes6/plex-presence.git
cd plex-presence
mkdir build && cd build
cmake --preset=release ..
cmake --build release
```

## Troubleshooting

Check the log file located at:

-   Windows: `%APPDATA%\Plex Presence\log.txt`
-   macOS/Linux: `~/.config/plex-presence/log.txt`

## License

[MIT License](LICENSE)
