# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

**CMake 3.25+ with C++23 (MSVC/Clang/GCC)**

Configure:
```bash
cmake --preset=release  # or debug
```

Build:
```bash
cmake --build build/release
```

Package (Windows - use Visual Studio terminal with CMake tools):
```bash
scripts\package.bat
# Or manually: cd build/release && cpack -C Release -G "NSIS;ZIP"
```

Package (Linux/macOS):
```bash
./scripts/package.sh
# Or manually: cd build/release && cpack -C Release -G "DEB;RPM;TGZ" (Linux) or "DragNDrop;ZIP" (macOS)
```

**Dependencies:** Managed via CPM.cmake (auto-downloaded)
- libcurl 8.4.0 (HTTP/SSE, platform-native TLS)
- nlohmann/json 3.11.3
- yaml-cpp (config files)
- Qt6 (Core, Widgets, Network - for UI)

## Architecture

**Event-Driven Layered Architecture:**

Application orchestrates services that communicate through EventBus:
- **PlexService** - Polls Plex servers, publishes MediaSessionUpdated events
- **DiscordPresenceService** - Subscribes to media events, updates Discord Rich Presence via IPC
- **ConfigurationService** - Manages YAML config with hot-reload support
- **EventBus** - Provides type-safe pub/sub using `std::any` and `std::type_index`

**Plex Service Composition:**
PlexService is composed of specialized managers:
- `PlexAuthenticator` - OAuth2 flow
- `PlexCacheManager` - Server/media metadata caching
- `PlexConnectionManager` - SSE streaming for real-time updates
- `PlexMediaFetcher` - API calls + TMDB/Jikan enrichment
- `PlexSessionManager` - Session state tracking

**Service Creation:**
- Services are instantiated directly in Application::initialize()
- PlexService and DiscordPresenceService are concrete implementations (not interfaces)
- `IConnectionStrategy` - Platform-specific Discord IPC (named pipes/Unix sockets)

**Key Design Patterns:**
- Direct dependency injection via constructor parameters
- RAII with smart pointers throughout
- Error handling: `std::expected<T, Error>` at API boundaries
- Type aliases for clarity: `PlexToken`, `ClientId`, `ServerId`, `SessionKey`
- Thread safety: `std::atomic`, mutexes, async event publishing

## Important Implementation Details

**Event System (`core/event_bus.hpp`):**
- Thread-safe type-safe pub/sub
- 30+ event types in `core/events.hpp` for all major state transitions
- Supports sync (`publish()`) and async (`publish_async()`) dispatch

**Configuration (`src/core/configuration/`):**
- YAML-based at platform-specific paths (AppData/XDG_CONFIG_HOME)
- Hot-reload support with `ConfigurationUpdated` events
- Services can enable/disable dynamically

**Discord Integration (`src/services/discord/`):**
- Native IPC protocol implementation (handshake, SET_ACTIVITY)
- Platform-specific connection strategies (Windows named pipes, Unix sockets)
- Rate limiting: 5 updates per 20 seconds
- Connection resilience with retry logic
- PresenceData formatting with placeholder replacement for customizable templates

**Plex Integration (`src/services/plex/`):**
- Server discovery via Plex.tv API
- Real-time session updates via Server-Sent Events (SSE)
- Metadata enrichment from TMDB/Jikan APIs
- Session filtering by media type (movies/TV/music)

**Platform Differences:**
- Windows: Schannel TLS, named pipes (`\\.\pipe\discord-ipc-0`), system tray
- macOS: SecureTransport TLS, Unix sockets (`/tmp/discord-ipc-0`), bundle packaging
- Linux: OpenSSL TLS, Unix sockets (`$XDG_RUNTIME_DIR/discord-ipc-0`), DEB/RPM

## Logs and Troubleshooting

Log files:
- Windows: `%APPDATA%\Presence For Plex\log.txt`
- macOS/Linux: `~/.config/presence-for-plex/log.txt`

Configurable log levels via config file (same directory as logs).
