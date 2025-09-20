# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Building the Project
```bash
# Configure with CMake preset (recommended)
cmake --preset=debug    # For debug build
cmake --preset=release  # For release build

# Build
cmake --build build/debug    # Debug build
cmake --build build/release  # Release build

# Or use ninja directly after configuration
cd build/release && ninja
```

### Running the Application
```bash
# Debug build
./build/debug/PresenceForPlex

# Release build
./build/release/PresenceForPlex
```

### Creating Packages
```bash
# Windows NSIS installer
cmake --build build/release --target package

# Or use cpack preset
cpack --preset release-windows  # Windows
cpack --preset release-linux    # Linux
```

## High-Level Architecture

### Core Application Structure
The application follows a service-oriented architecture with dependency injection:

- **Application Layer** (`core/application.hpp`): Main application interface managing lifecycle, state, and service coordination
- **Service Layer**: Loosely coupled services for different responsibilities:
  - `MediaService`: Interface for media providers (implemented by PlexService)
  - `PresenceService`: Interface for presence providers (implemented by DiscordPresenceService)
  - `ConfigurationService`: Configuration management with change notifications
  - `UiService`: Platform-specific UI abstraction (Windows tray icon, etc.)
- **Dependency Injection** (`core/dependency_injection.hpp`): Simple DI container for service registration and resolution

### Key Service Implementations

**Plex Integration** (`services/plex/`):
- `PlexAuthenticator`: OAuth authentication flow
- `PlexConnectionManager`: Server discovery and connection management
- `PlexSessionManager`: Active media session tracking via SSE
- `PlexCacheManager`: Caching layer for metadata
- `PlexMediaFetcher`: Media metadata retrieval

**Discord Integration** (`services/discord/`):
- `discord_ipc.cpp`: Low-level IPC communication with Discord
- `discord_presence_service.cpp`: High-level presence management
- `rate_limiter.cpp`: Rate limiting for Discord API calls
- `connection_manager.cpp`: Connection resilience and retry logic

### Platform Abstraction
Platform-specific code is isolated in `platform/` with factory patterns:
- Windows: System tray integration, Windows-specific UI
- Cross-platform: Single instance enforcement, system service abstractions

### Threading Model
- `ThreadPool` and `TaskScheduler` in `utils/threading.hpp` for async operations
- Services use thread pools for non-blocking operations
- Main application runs an event loop with `run()` method

### Configuration
- YAML-based configuration stored in platform-specific locations
- Configuration service provides validation and change notifications
- Plex authentication tokens and server lists persisted between sessions

## Dependencies

The project uses FetchContent for dependency management with static linking by default:
- **curl**: HTTP client for Plex API
- **yaml-cpp**: Configuration file parsing
- **nlohmann/json**: JSON parsing for API responses
- **googletest**: Unit testing framework (when BUILD_TESTING=ON)

Platform-specific SSL/TLS backends are automatically selected (Schannel on Windows, SecureTransport on macOS, OpenSSL on Linux).