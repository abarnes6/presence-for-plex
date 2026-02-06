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

Config file: `~/.config/presence-for-plex/config.yaml` (Windows: `%APPDATA%\presence-for-plex\config.yaml`)

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

## Troubleshooting

Check the log file:
- Windows: `%APPDATA%\presence-for-plex\log.txt`
- macOS/Linux: `~/.config/presence-for-plex/log.txt`

## FAQ

### Why don't the buttons work for me?

Discord only shows Rich Presence buttons to others, not yourself.

## Attribution

![TMDB Logo](https://github.com/user-attachments/assets/38abfb34-72cf-46d9-9d17-724761aa570a)

## License

[MIT License](LICENSE)
