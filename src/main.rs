#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod config;
mod discord;
mod plex;
mod tray;

use config::Config;
use discord::{ActivityType, Button, DiscordClient, Presence};
use log::{error, info, warn};
use plex::{MediaInfo, MediaType, PlaybackState, PlexClient, APP_NAME, SSE_RECONNECT_DELAY_SECS};
use simplelog::{CombinedLogger, Config as LogConfig, LevelFilter, SimpleLogger, WriteLogger};
use std::fs::File;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::{mpsc, Mutex};
use tokio_util::sync::CancellationToken;

const POLL_INTERVAL_MS: u64 = 50;
const AUTH_TIMEOUT_SECS: u64 = 300;
const AUTH_POLL_INTERVAL_SECS: u64 = 2;

#[derive(Debug, Clone)]
pub enum AppMessage {
    Quit,
    Authenticate,
}

fn main() {
    let log_path = Config::log_path();
    if let Some(parent) = log_path.parent() {
        std::fs::create_dir_all(parent).ok();
    }

    let mut loggers: Vec<Box<dyn simplelog::SharedLogger>> = vec![
        SimpleLogger::new(LevelFilter::Debug, LogConfig::default()),
    ];
    if let Ok(file) = File::create(&log_path) {
        loggers.push(WriteLogger::new(LevelFilter::Debug, LogConfig::default(), file));
    }
    if let Err(e) = CombinedLogger::init(loggers) {
        eprintln!("Failed to initialize logger: {}", e);
    }

    info!("Starting Presence for Plex");
    info!("Log file: {}", log_path.display());

    let config = Arc::new(std::sync::Mutex::new(Config::load()));
    let runtime = tokio::runtime::Runtime::new().expect("Failed to create tokio runtime");

    let (tx, mut rx) = mpsc::unbounded_channel::<AppMessage>();

    let is_authenticated = config.lock().expect("Config mutex poisoned").plex_token.is_some();
    let initial_status = if is_authenticated { "Status: Idle" } else { "Status: Not Authenticated" };
    let tray_handle = tray::setup(tx.clone(), initial_status, is_authenticated);

    let discord = {
        let cfg = config.lock().expect("Config mutex poisoned");
        let mut client = DiscordClient::new(&cfg.discord_client_id);
        if cfg.discord_enabled {
            client.connect();
        }
        Arc::new(Mutex::new(client))
    };

    let (media_tx, mut media_rx) = mpsc::unbounded_channel::<Option<MediaInfo>>();

    let app_cancel_token = CancellationToken::new();
    let sse_cancel_token = Arc::new(Mutex::new(CancellationToken::new()));

    {
        let cfg = config.lock().expect("Config mutex poisoned");
        if let Some(ref token) = cfg.plex_token {
            let token = token.clone();
            let media_tx = media_tx.clone();
            let tmdb_token = cfg.tmdb_token.clone();
            let app_cancel = app_cancel_token.clone();
            let sse_cancel = sse_cancel_token.clone();

            runtime.spawn(async move {
                run_sse_loop(token, tmdb_token, media_tx, app_cancel, sse_cancel).await;
            });
        }
    }

    let discord_task = Arc::clone(&discord);
    let config_task = Arc::clone(&config);
    let (status_tx, status_rx) = mpsc::unbounded_channel::<&'static str>();
    runtime.spawn(async move {
        handle_media_updates(&mut media_rx, discord_task, config_task, status_tx).await;
    });

    run_event_loop(
        &runtime,
        &mut rx,
        &config,
        &discord,
        &media_tx,
        &app_cancel_token,
        &sse_cancel_token,
        tray_handle.as_ref(),
        status_rx,
    );

    info!("Shutting down");
}

async fn run_sse_loop(
    token: String,
    tmdb_token: Option<String>,
    media_tx: mpsc::UnboundedSender<Option<MediaInfo>>,
    app_cancel: CancellationToken,
    sse_cancel: Arc<Mutex<CancellationToken>>,
) {
    let mut plex = PlexClient::new(tmdb_token);
    loop {
        let current_sse_cancel = sse_cancel.lock().await.clone();
        tokio::select! {
            _ = app_cancel.cancelled() => {
                info!("SSE monitoring cancelled (app shutdown)");
                break;
            }
            _ = current_sse_cancel.cancelled() => {
                info!("SSE monitoring cancelled (re-authentication)");
                break;
            }
            _ = plex.start_sse_monitoring(&token, media_tx.clone()) => {
                warn!("SSE connection lost, reconnecting in {}s...", SSE_RECONNECT_DELAY_SECS);
                tokio::time::sleep(Duration::from_secs(SSE_RECONNECT_DELAY_SECS)).await;
            }
        }
    }
}

async fn handle_media_updates(
    media_rx: &mut mpsc::UnboundedReceiver<Option<MediaInfo>>,
    discord: Arc<Mutex<DiscordClient>>,
    config: Arc<std::sync::Mutex<Config>>,
    status_tx: mpsc::UnboundedSender<&'static str>,
) {
    while let Some(media_info) = media_rx.recv().await {
        match media_info {
            Some(info) => {
                let status_text = match info.state {
                    PlaybackState::Playing => "Status: Playing",
                    PlaybackState::Paused => "Status: Paused",
                    PlaybackState::Buffering => "Status: Buffering",
                    PlaybackState::Stopped => "Status: Idle",
                };
                let _ = status_tx.send(status_text);

                let (enabled, presence) = {
                    let cfg = config.lock().expect("Config mutex poisoned");
                    let enabled = match info.media_type {
                        MediaType::Movie => cfg.enable_movies,
                        MediaType::Episode => cfg.enable_tv_shows,
                        MediaType::Track => cfg.enable_music,
                    };
                    (enabled, build_presence(&info, &cfg))
                };

                if enabled {
                    info!("Now playing: {}", info.title);
                    let mut discord = discord.lock().await;
                    if !discord.is_connected() {
                        discord.connect();
                    }
                    discord.update(&presence);
                }
            }
            None => {
                let _ = status_tx.send("Status: Idle");
                info!("Playback stopped");
                discord.lock().await.clear();
            }
        }
    }
}

fn run_event_loop(
    runtime: &tokio::runtime::Runtime,
    rx: &mut mpsc::UnboundedReceiver<AppMessage>,
    config: &Arc<std::sync::Mutex<Config>>,
    discord: &Arc<Mutex<DiscordClient>>,
    media_tx: &mpsc::UnboundedSender<Option<MediaInfo>>,
    app_cancel_token: &CancellationToken,
    sse_cancel_token: &Arc<Mutex<CancellationToken>>,
    tray_handle: Option<&tray::TrayHandle>,
    mut status_rx: mpsc::UnboundedReceiver<&'static str>,
) {
    runtime.block_on(async {
        loop {
            #[cfg(windows)]
            pump_windows_messages();

            while let Ok(status) = status_rx.try_recv() {
                if let Some(handle) = tray_handle {
                    handle.status_item.set_text(status);
                }
            }

            match rx.try_recv() {
                Ok(AppMessage::Quit) => {
                    app_cancel_token.cancel();
                    discord.lock().await.disconnect();
                    break;
                }
                Ok(AppMessage::Authenticate) => {
                    handle_authentication(
                        runtime,
                        config,
                        media_tx,
                        app_cancel_token,
                        sse_cancel_token,
                        tray_handle,
                    )
                    .await;
                }
                Err(mpsc::error::TryRecvError::Empty) => {
                    tokio::time::sleep(Duration::from_millis(POLL_INTERVAL_MS)).await;
                }
                Err(mpsc::error::TryRecvError::Disconnected) => break,
            }
        }
    });
}

#[cfg(windows)]
fn pump_windows_messages() {
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        DispatchMessageW, PeekMessageW, TranslateMessage, MSG, PM_REMOVE,
    };
    unsafe {
        let mut msg: MSG = std::mem::zeroed();
        while PeekMessageW(&mut msg, std::ptr::null_mut(), 0, 0, PM_REMOVE) != 0 {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

async fn handle_authentication(
    runtime: &tokio::runtime::Runtime,
    config: &Arc<std::sync::Mutex<Config>>,
    media_tx: &mpsc::UnboundedSender<Option<MediaInfo>>,
    app_cancel_token: &CancellationToken,
    sse_cancel_token: &Arc<Mutex<CancellationToken>>,
    tray_handle: Option<&tray::TrayHandle>,
) {
    info!("Starting Plex authentication");

    let plex = PlexClient::new(None);
    let Some(token) = run_auth_flow(&plex).await else {
        warn!("Authentication failed or timed out");
        return;
    };

    let tmdb_token = {
        let mut cfg = config.lock().expect("Config mutex poisoned");
        cfg.plex_token = Some(token.clone());
        if let Err(e) = cfg.save() {
            error!("Failed to save config: {}", e);
        }
        info!("Token saved");
        cfg.tmdb_token.clone()
    };

    if let Some(handle) = tray_handle {
        handle.auth_item.set_text("Reauthenticate");
        handle.status_item.set_text("Status: Idle");
    }

    {
        let mut sse_cancel = sse_cancel_token.lock().await;
        sse_cancel.cancel();
        *sse_cancel = CancellationToken::new();
    }

    let media_tx = media_tx.clone();
    let app_cancel = app_cancel_token.clone();
    let sse_cancel = sse_cancel_token.clone();

    runtime.spawn(async move {
        run_sse_loop(token, tmdb_token, media_tx, app_cancel, sse_cancel).await;
    });
}

async fn run_auth_flow(plex: &PlexClient) -> Option<String> {
    let (pin_id, code) = plex.request_pin().await?;

    let auth_url = format!(
        "https://app.plex.tv/auth#?clientID={}&code={}&context%5Bdevice%5D%5Bproduct%5D=Presence%20for%20Plex",
        urlencoding::encode(APP_NAME),
        urlencoding::encode(&code)
    );

    info!("Opening browser for authentication");
    if let Err(e) = open::that(&auth_url) {
        warn!("Failed to open browser: {}. Visit: {}", e, auth_url);
    }

    info!("Waiting for authentication ({}s timeout)...", AUTH_TIMEOUT_SECS);
    let deadline = std::time::Instant::now() + Duration::from_secs(AUTH_TIMEOUT_SECS);

    loop {
        if std::time::Instant::now() >= deadline {
            return None;
        }

        tokio::time::sleep(Duration::from_secs(AUTH_POLL_INTERVAL_SECS)).await;

        if let Some(token) = plex.check_pin(pin_id).await {
            info!("Authentication successful");
            return Some(token);
        }
    }
}

const MAX_BUTTONS: usize = 2;
const DEFAULT_IMAGE: &str = "plex_logo";

fn build_presence(info: &MediaInfo, config: &Config) -> Presence {
    let template_set = match info.media_type {
        MediaType::Episode => (&config.tv_details, &config.tv_state, &config.tv_image_text),
        MediaType::Movie => (&config.movie_details, &config.movie_state, &config.movie_image_text),
        MediaType::Track => (&config.music_details, &config.music_state, &config.music_image_text),
    };

    let activity_type = match info.media_type {
        MediaType::Track => ActivityType::Listening,
        _ => ActivityType::Watching,
    };

    let large_image = match config.show_artwork {
        true => info.art_url.clone().unwrap_or_else(|| DEFAULT_IMAGE.to_string()),
        false => DEFAULT_IMAGE.to_string(),
    };

    Presence {
        details: format_template(template_set.0, info),
        state: format_template(template_set.1, info),
        large_image: Some(large_image),
        large_image_text: format_template(template_set.2, info),
        progress_ms: info.view_offset_ms,
        duration_ms: info.duration_ms,
        show_timestamps: config.show_progress,
        activity_type,
        playback_state: info.state.clone(),
        buttons: build_buttons(info, config.show_buttons),
    }
}

fn build_buttons(info: &MediaInfo, show_buttons: bool) -> Vec<Button> {
    if !show_buttons {
        return Vec::new();
    }

    let mut buttons = Vec::with_capacity(MAX_BUTTONS);

    if let Some(ref mal_id) = info.mal_id {
        buttons.push(Button {
            label: "View on MyAnimeList".to_string(),
            url: format!("https://myanimelist.net/anime/{}", mal_id),
        });
    }

    if let Some(ref imdb_id) = info.imdb_id {
        if buttons.len() < MAX_BUTTONS {
            buttons.push(Button {
                label: "View on IMDb".to_string(),
                url: format!("https://www.imdb.com/title/{}", imdb_id),
            });
        }
    }

    buttons
}

fn format_template(template: &str, info: &MediaInfo) -> String {
    let mut result = String::with_capacity(template.len() + 32);
    let mut chars = template.chars().peekable();

    while let Some(c) = chars.next() {
        if c == '{' {
            // Handle escape sequence: {{ becomes literal {
            if chars.peek() == Some(&'{') {
                chars.next();
                result.push('{');
                continue;
            }

            // Collect placeholder until closing brace
            let mut placeholder = String::new();
            let mut found_closing = false;
            for ch in chars.by_ref() {
                if ch == '}' {
                    found_closing = true;
                    break;
                }
                placeholder.push(ch);
            }

            // Handle unclosed brace: output literally
            if !found_closing {
                result.push('{');
                result.push_str(&placeholder);
                continue;
            }

            let value = match placeholder.as_str() {
                "show" => info.show_name.as_deref().unwrap_or(""),
                "title" => &info.title,
                "se" => {
                    if let (Some(s), Some(e)) = (info.season, info.episode) {
                        use std::fmt::Write;
                        let _ = write!(result, "S{s:02}E{e:02}");
                    }
                    continue;
                }
                "season" => {
                    if let Some(s) = info.season {
                        use std::fmt::Write;
                        let _ = write!(result, "{s}");
                    }
                    continue;
                }
                "episode" => {
                    if let Some(e) = info.episode {
                        use std::fmt::Write;
                        let _ = write!(result, "{e}");
                    }
                    continue;
                }
                "year" => {
                    if let Some(y) = info.year {
                        use std::fmt::Write;
                        let _ = write!(result, "{y}");
                    }
                    continue;
                }
                "genres" => {
                    result.push_str(&info.genres.join(", "));
                    continue;
                }
                "artist" => info.artist.as_deref().unwrap_or(""),
                "album" => info.album.as_deref().unwrap_or(""),
                _ => {
                    result.push('{');
                    result.push_str(&placeholder);
                    result.push('}');
                    continue;
                }
            };
            result.push_str(value);
        } else if c == '}' {
            // Handle escape sequence: }} becomes literal }
            if chars.peek() == Some(&'}') {
                chars.next();
            }
            result.push('}');
        } else {
            result.push(c);
        }
    }

    result
}
