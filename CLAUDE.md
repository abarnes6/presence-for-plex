# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
cargo build              # Debug build
cargo build --release    # Release build (optimized, stripped)
cargo run                # Run debug build
cargo clippy             # Lint
cargo fmt                # Format
```

### Platform-specific Dependencies

**Linux**: `sudo apt-get install libgtk-3-dev libxdo-dev`

### Packaging

- **Windows MSI**: `cargo install cargo-wix && cargo wix`
- **Linux .deb**: `cargo install cargo-deb && cargo deb`
- **macOS .app**: `cargo install cargo-bundle && cargo bundle`

## Purpose

Discord Rich Presence client that monitors Plex Media Server and displays current media (movies, TV shows, music) on Discord status with artwork, progress timestamps, and interactive links (IMDb, MyAnimeList).

## Architecture

Event-driven async design using channels to coordinate:
- Plex monitoring via Server-Sent Events (SSE) streams
- Discord status updates via IPC
- System tray UI for authentication and control

### Data Flow

1. **main.rs** spawns SSE connections per Plex server and routes `MediaUpdate` events through an unbounded channel
2. **plex_server.rs** parses SSE notifications, fetches session metadata, and uses `PlaybackTracker` to deduplicate updates
3. **metadata.rs** enriches media info with artwork URLs from TMDB/Jikan/MusicBrainz (cached with 1-hour TTL)
4. **presence.rs** builds Discord activity using configurable templates with variable substitution
5. **discord.rs** sends Rich Presence updates via IPC

### Modules

| Module | Role |
|--------|------|
| `main.rs` | Orchestrates lifecycle, auth flow, event loop |
| `config.rs` | YAML configuration at `~/.config/presence-for-plex/config.yaml` |
| `plex_account.rs` | Plex authentication (PIN flow) and server discovery |
| `plex_server.rs` | SSE-based playback monitoring and session tracking |
| `discord.rs` | Discord Rich Presence IPC client |
| `presence.rs` | Builds Discord activity from configurable templates |
| `metadata.rs` | Fetches artwork from TMDB, Jikan (anime), MusicBrainz with caching |
| `tray.rs` | System tray menu |

## Configuration

Config file location: `~/.config/presence-for-plex/config.yaml` (or platform equivalent via `dirs::config_dir()`)

Template variables: `{show}`, `{title}`, `{season}`, `{episode}`, `{se}` (S01E02 format), `{year}`, `{genres}`, `{artist}`, `{album}`
