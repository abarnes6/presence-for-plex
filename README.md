# Presence for Plex

A lightweight and efficient application written in Rust that displays what you are watching on Plex in your Discord client.

## Installation

Windows
- Download the .msi on the releases page and run it

## Building from source

After downloading Rust, just `cargo build` and `cargo run`!

## Troubleshooting

Check the log file located at:

-   Windows: `%APPDATA%\presence-for-plex\log.txt`
-   macOS/Linux: `~/.config/presence-for-plex/log.txt`

## FAQ

### Why do the buttons not work?

They do, but only for others! For some reason Discord doesn't like to show you your own rich presence buttons.

### Failed to connect to Discord: failed to connect to IPC socket

You need to have a Discord app running in the same machine as the application.

## Attribution

![blue_square_2-d537fb228cf3ded904ef09b136fe3fec72548ebc1fea3fbbd1ad9e36364db38b](https://github.com/user-attachments/assets/38abfb34-72cf-46d9-9d17-724761aa570a)

(image API)

## License

[MIT License](LICENSE)