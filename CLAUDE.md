# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Presence For Plex is a C++23 application that displays Plex media activity as Discord Rich Presence. The codebase follows a modular architecture with clear separation between core logic, services, platform-specific code, and utilities.

## Build Commands

### Configure and Build
```bash
# Debug build
cmake --preset=debug ..
cmake --build debug

# Release build
cmake --preset=release ..
cmake --build release
```

### Running Tests
Currently no tests directory exists. When implementing tests, they should be added to a `tests/` directory and the existing CMake testing infrastructure will handle them.

### Package Creation
```bash
cd build
cmake --build . --target package
```

## Architecture Overview

### Directory Structure
- `include/presence_for_plex/` - All header files organized by module
- `src/` - Implementation files mirroring the include structure
  - `core/` - Application lifecycle, configuration, models
  - `services/` - Plex and Discord integration services
  - `platform/` - Platform-specific implementations (Windows tray icon, etc.)
  - `utils/` - Logging, threading, UUID generation, configuration validation

### Key Components

**Core Application Flow:**
1. `Application` class manages the lifecycle through `LifecycleManager`
2. `EventDispatcher` handles inter-component communication
3. `ConfigManager` manages YAML configuration files

**Service Layer:**
- `PlexService` - Authenticates with Plex, manages sessions, fetches media metadata
- `DiscordPresenceService` - Manages Discord IPC connection and presence updates with robust connection management, rate limiting, and reconnection features
- `HttpClient` - CURL-based HTTP client with SSL support
- `SSEClient` - Server-sent events client for real-time Plex updates

**Platform Layer:**
- Windows: System tray integration via `TrayIconWin` and `WindowsUIService`
- Cross-platform single instance enforcement via `SingleInstance`

## Dependencies

The project uses FetchContent to manage dependencies statically by default:
- `nlohmann/json` - JSON parsing
- `yaml-cpp` - Configuration files
- `curl` - HTTP requests with platform-specific SSL backends
- `googletest` - Testing framework (when BUILD_TESTING=ON)

Use `USE_DYNAMIC_LINKS=ON` to use system libraries instead.

## Configuration

User configuration is stored in:
- Windows: `%APPDATA%\Presence For Plex\`
- macOS/Linux: `~/.config/presence-for-plex/`

The application uses YAML for configuration and stores authentication tokens, server preferences, and user settings.

## Important Patterns

1. **Dependency Injection**: The codebase uses a custom DI container in `dependency_injection.hpp`
2. **Error Handling**: Uses expected type for error handling instead of exceptions
3. **Async Operations**: Thread pool and async task management via `threading.hpp`
4. **Event System**: Pub-sub pattern through `EventDispatcher` for decoupled communication
5. **Service Interfaces**: All services implement interfaces for testability and modularity

## Platform-Specific Notes

### Windows
- Requires Windows 11 SDK for tray icon features
- Uses Schannel for SSL
- NSIS for installer creation
- Runs as a Windows subsystem application (no console window)

### macOS
- Uses SecureTransport for SSL
- Requires Cocoa, Foundation, IOKit, and Security frameworks

### Linux
- Uses OpenSSL for SSL
- Optional X11 support for window management
- GTK3 and libnotify support disabled by default to avoid pkg-config dependency