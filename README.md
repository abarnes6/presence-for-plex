# Presence for Plex

A lightweight Discord Rich Presence client for Plex. Shows what you're watching with artwork, progress, and interactive links.

## Features

- Movies, TV shows, and music support
- Artwork from TMDB and MusicBrainz
- IMDb and MyAnimeList links as clickable buttons (thank you Jikan for MAL search)
- Progress timestamps
- Customizable display templates
- Multi-server support

## Configuration

Config file locations:

- Linux: `~/.config/presence-for-plex/config.yaml`
- macOS: `~/Library/Application Support/presence-for-plex/config.yaml`
- Windows: `%APPDATA%\presence-for-plex\config.yaml`

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `plex_token` | – | Plex auth token (set automatically by the auth flow) |
| `tmdb_token` | – | Optional personal TMDB API read token for artwork |
| `show_buttons` | `true` | Show IMDb/MyAnimeList link buttons |
| `show_progress` | `true` | Show playback progress timestamps |
| `show_artwork` | `true` | Show media artwork instead of the Plex logo |
| `enable_movies` / `enable_tv_shows` / `enable_music` | `true` | Toggle presence per media type |
| `tv_details`, `tv_state`, `tv_image_text`, `movie_*`, `music_*` | see defaults | Display templates (see variables below) |

### Template Variables

| Variable | Description |
|----------|-------------|
| `{title}` | Media title |
| `{show}` | TV show name |
| `{season}` | Season number |
| `{episode}` | Episode number |
| `{se}` | Formatted as S01E02 |
| `{year}` | Release year |
| `{genres}` | Genre list |
| `{artist}` | Music artist |
| `{album}` | Album name |

## Headless mode

For servers or Docker-style setups you can build without the system tray (drops the GTK dependency on Linux):

```sh
cargo build --release --no-default-features
```

Since there is no tray menu in headless mode, authenticate once from the terminal:

```sh
presence-for-plex --auth
```

This prints a Plex link URL, waits for you to approve it, and saves the token to the config file. Then start the app normally. (`--auth` also works in the tray build.)

## Troubleshooting

Check the log file (`presence-for-plex.log`):
- Linux: `~/.config/presence-for-plex/presence-for-plex.log`
- macOS: `~/Library/Application Support/presence-for-plex/presence-for-plex.log`
- Windows: `%APPDATA%\presence-for-plex\presence-for-plex.log`

## FAQ

### Why don't the buttons work for me?

Discord only shows Rich Presence buttons to others, not yourself.

## Attribution

![TMDB Logo](https://github.com/user-attachments/assets/38abfb34-72cf-46d9-9d17-724761aa570a)

## License

[MIT License](LICENSE)
