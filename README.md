# Plex Presence

A lightweight application written in C++ that displays your current Plex media activity in Discord's Rich Presence.

## Features

-   Show what you're watching on Plex in your Discord status
-   Displays show titles, episode information, and progress
-   Cross-platform (Windows, macOS, Linux)
-   Runs in the system tray for easy access (Windows only)

## Installation

### Download

Download the latest release from the [Releases](https://github.com/abarnes6/plex-presence/releases) page.

### Setup

1. Run the executable
2. Configure your Plex server connection settings
3. Connect your Plex account in a browser when prompted
4. The application will automatically connect to Plex/Discord

## Configuration

Configuration is stored in a TOML file located in:

-   Windows: `%APPDATA%\Plex Presence\config.toml`
-   macOS/Linux: `~/.config/plex-presence/config.toml`

### Configuration Options

| Option            | Description                                             | Default  |
| ----------------- | ------------------------------------------------------- | -------- |
| `log_level`       | Logging verbosity (0=Debug, 1=Info, 2=Warning, 3=Error) | 1 (Info) |
| `plex_server_url` | URL of your Plex Media Server                           |          |
| `poll_interval`   | How often to check for playback changes (seconds)       | 5        |

## Building from Source

### Requirements

-   C++17 compatible compiler
-   CMake 3.25+
-   vcpkg

### Build Instructions

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
