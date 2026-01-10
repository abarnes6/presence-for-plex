#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod config;
mod discord;
mod plex;
mod presence;
mod tray;

use config::Config;
use discord::DiscordClient;
use fs2::FileExt;
use log::{error, info, warn};
use plex::{MediaInfo, MediaType, PlaybackState, PlexClient, APP_NAME, SSE_RECONNECT_DELAY_SECS};
use presence::build_presence;
use simplelog::{CombinedLogger, Config as LogConfig, LevelFilter, SimpleLogger, WriteLogger};
use std::fs::File;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::{mpsc, oneshot, Mutex};
use tokio_util::sync::CancellationToken;

const AUTH_TIMEOUT_SECS: u64 = 300;
const AUTH_POLL_INTERVAL_SECS: u64 = 2;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TrayStatus {
    Idle,
    Playing,
    Paused,
    Buffering,
    NotAuthenticated,
}

impl TrayStatus {
    pub fn as_str(self) -> &'static str {
        match self {
            TrayStatus::Idle => "Status: Idle",
            TrayStatus::Playing => "Status: Playing",
            TrayStatus::Paused => "Status: Paused",
            TrayStatus::Buffering => "Status: Buffering",
            TrayStatus::NotAuthenticated => "Status: Not Authenticated",
        }
    }
}

impl From<PlaybackState> for TrayStatus {
    fn from(state: PlaybackState) -> Self {
        match state {
            PlaybackState::Playing => TrayStatus::Playing,
            PlaybackState::Paused => TrayStatus::Paused,
            PlaybackState::Buffering => TrayStatus::Buffering,
            PlaybackState::Stopped => TrayStatus::Idle,
        }
    }
}

#[derive(Debug, Clone)]
pub struct AuthResult {
    pub token: String,
    pub tmdb_token: Option<String>,
}

#[derive(Debug)]
pub enum TrayCommand {
    Quit,
    Authenticate,
}

fn acquire_instance_lock() -> Option<File> {
    let lock_path = Config::app_dir().join("presence-for-plex.lock");
    if let Some(parent) = lock_path.parent() {
        std::fs::create_dir_all(parent).ok();
    }

    let lock_file = File::create(&lock_path).ok()?;
    lock_file.try_lock_exclusive().ok()?;
    Some(lock_file)
}

fn init_logging() {
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
}

#[tokio::main]
async fn main() {
    let _lock_file = match acquire_instance_lock() {
        Some(f) => f,
        None => {
            eprintln!("Another instance is already running");
            return;
        }
    };

    init_logging();

    let config = Arc::new(Config::load());
    let app_cancel_token = CancellationToken::new();

    // Channels
    let (tray_tx, tray_rx) = mpsc::unbounded_channel::<TrayCommand>();
    let (media_tx, media_rx) = mpsc::unbounded_channel::<Option<MediaInfo>>();
    let (status_tx, status_rx) = mpsc::unbounded_channel::<TrayStatus>();

    let tray_handle = tray::setup(tray_tx, config.plex_token.is_some());

    let mut discord = DiscordClient::new(&config.discord_client_id);
    if config.discord_enabled {
        discord.connect();
    }
    let discord = Arc::new(Mutex::new(discord));

    let initial_sse_cancel = app_cancel_token.child_token();
    if let Some(ref token) = config.plex_token {
        let token = token.clone();
        let media_tx = media_tx.clone();
        let tmdb_token = config.tmdb_token.clone();
        let sse_cancel = initial_sse_cancel.clone();
        tokio::spawn(async move {
            run_sse_loop(token, tmdb_token, media_tx, sse_cancel).await;
        });
    }

    tokio::spawn({
        let discord = Arc::clone(&discord);
        let config = Arc::clone(&config);
        async move {
            handle_media_updates(media_rx, discord, config, status_tx).await;
        }
    });

    run_event_loop(
        tray_rx,
        &config,
        &discord,
        &media_tx,
        &app_cancel_token,
        tray_handle.as_ref(),
        status_rx,
        Some(initial_sse_cancel),
    ).await;

    info!("Shutting down");
}

async fn run_sse_loop(
    token: String,
    tmdb_token: Option<String>,
    media_tx: mpsc::UnboundedSender<Option<MediaInfo>>,
    cancel_token: CancellationToken,
) {
    let mut plex = PlexClient::new(tmdb_token);
    loop {
        tokio::select! {
            _ = cancel_token.cancelled() => {
                info!("SSE monitoring cancelled");
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
    mut media_rx: mpsc::UnboundedReceiver<Option<MediaInfo>>,
    discord: Arc<Mutex<DiscordClient>>,
    config: Arc<Config>,
    status_tx: mpsc::UnboundedSender<TrayStatus>,
) {
    while let Some(update) = media_rx.recv().await {
        match update {
            Some(info) => {
                let _ = status_tx.send(TrayStatus::from(info.state.clone()));

                let enabled = match info.media_type {
                    MediaType::Movie => config.enable_movies,
                    MediaType::Episode => config.enable_tv_shows,
                    MediaType::Track => config.enable_music,
                };
                let presence = build_presence(&info, &config);

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
                let _ = status_tx.send(TrayStatus::Idle);
                info!("Playback stopped");
                discord.lock().await.clear();
            }
        }
    }
}

struct EventLoopContext<'a> {
    config: &'a Arc<Config>,
    discord: &'a Arc<Mutex<DiscordClient>>,
    media_tx: &'a mpsc::UnboundedSender<Option<MediaInfo>>,
    app_cancel_token: &'a CancellationToken,
    tray_handle: Option<&'a tray::TrayHandle>,
    sse_cancel_token: Option<CancellationToken>,
    auth_pending: Option<oneshot::Receiver<Option<AuthResult>>>,
}

async fn run_event_loop(
    mut rx: mpsc::UnboundedReceiver<TrayCommand>,
    config: &Arc<Config>,
    discord: &Arc<Mutex<DiscordClient>>,
    media_tx: &mpsc::UnboundedSender<Option<MediaInfo>>,
    app_cancel_token: &CancellationToken,
    tray_handle: Option<&tray::TrayHandle>,
    mut status_rx: mpsc::UnboundedReceiver<TrayStatus>,
    initial_sse_cancel: Option<CancellationToken>,
) {
    let mut ctx = EventLoopContext {
        config,
        discord,
        media_tx,
        app_cancel_token,
        tray_handle,
        sse_cancel_token: initial_sse_cancel,
        auth_pending: None,
    };

    let mut pump_interval = tokio::time::interval(Duration::from_millis(16));

    loop {
        tokio::select! {
            biased;

            _ = pump_interval.tick() => {
                #[cfg(windows)]
                pump_windows_messages();
            }
            Some(result) = recv_auth_result(&mut ctx.auth_pending) => {
                handle_auth_complete(&mut ctx, result);
            }
            Some(status) = status_rx.recv() => {
                if let Some(handle) = ctx.tray_handle {
                    handle.status_item.set_text(status.as_str());
                }
            }
            msg = rx.recv() => {
                match msg {
                    Some(TrayCommand::Quit) => {
                        handle_quit(&ctx).await;
                        break;
                    }
                    Some(TrayCommand::Authenticate) => {
                        handle_authenticate(&mut ctx);
                    }
                    None => break,
                }
            }
        }
    }
}

async fn recv_auth_result(pending: &mut Option<oneshot::Receiver<Option<AuthResult>>>) -> Option<Option<AuthResult>> {
    match pending.take() {
        Some(rx) => Some(rx.await.ok().flatten()),
        None => std::future::pending().await,
    }
}

async fn handle_quit(ctx: &EventLoopContext<'_>) {
    ctx.app_cancel_token.cancel();
    ctx.discord.lock().await.disconnect();
}

fn handle_authenticate(ctx: &mut EventLoopContext<'_>) {
    if ctx.auth_pending.is_some() {
        return;
    }

    let config = Arc::clone(ctx.config);
    let (tx, rx) = oneshot::channel();
    ctx.auth_pending = Some(rx);

    tokio::spawn(async move {
        let result = start_auth_flow(&config).await;
        let _ = tx.send(result);
    });
}

fn handle_auth_complete(ctx: &mut EventLoopContext<'_>, result: Option<AuthResult>) {
    let Some(auth) = result else {
        warn!("Authentication failed or timed out");
        return;
    };

    if let Some(handle) = ctx.tray_handle {
        handle.auth_item.set_text("Reauthenticate");
        handle.status_item.set_text(TrayStatus::Idle.as_str());
    }

    if let Some(ref old_token) = ctx.sse_cancel_token {
        old_token.cancel();
    }

    let sse_cancel = ctx.app_cancel_token.child_token();
    ctx.sse_cancel_token = Some(sse_cancel.clone());

    let token = auth.token;
    let tmdb_token = auth.tmdb_token;
    let media_tx = ctx.media_tx.clone();

    tokio::spawn(async move {
        run_sse_loop(token, tmdb_token, media_tx, sse_cancel).await;
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

async fn start_auth_flow(config: &Arc<Config>) -> Option<AuthResult> {
    info!("Starting Plex authentication");

    let plex = PlexClient::new(None);
    let token = run_auth_flow(&plex).await?;

    // Save to disk
    let mut disk_config = Config::load();
    disk_config.plex_token = Some(token.clone());
    if let Err(e) = disk_config.save() {
        error!("Failed to save config: {}", e);
    }
    info!("Token saved");

    Some(AuthResult {
        token,
        tmdb_token: config.tmdb_token.clone(),
    })
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

