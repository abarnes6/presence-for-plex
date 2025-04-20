# Plex Rich Presence

![Plex Rich Presence Logo](https://github.com/user-attachments/assets/882c06f1-f1d6-4444-9249-7021eef77d78)

A lightweight application that displays your current Plex media activity in Discord's Rich Presence.

## Features

-   Show what you're watching on Plex in your Discord status
-   Displays movie/show titles, episode information, and progress
-   Automatically updates activity when media playback state changes (playing/paused/stopped)
-   Runs in the system tray for easy access (Windows only)
-   Configurable logging levels
-   Cross-platform support (Windows, macOS, Linux)

## Installation

### Download

Download the latest release from the [Releases](https://github.com/abarnes6/plex-rich-presence/releases) page.

### Setup

1. Extract the downloaded archive
2. Run the executable
3. Configure your Plex server connection settings
4. The application will automatically connect to Discord

## Configuration

Configuration is stored in a TOML file located in:

-   Windows: `%APPDATA%\Plex Rich Presence\config.toml`
-   macOS/Linux: `~/.config/plex-rich-presence/config.toml`

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
-   curl
-   nlohmann/json
-   toml++

### Build Instructions

```bash
git clone https://github.com/abarnes6/plex-rich-presence.git
cd plex-rich-presence
mkdir build && cd build
cmake --preset=release ..
cmake --build release
```

## Troubleshooting

Check the log file located at:

-   Windows: `%APPDATA%\Plex Rich Presence\log.txt`
-   macOS/Linux: `~/.config/plex-rich-presence/log.txt`

## License

[MIT License](LICENSE)
