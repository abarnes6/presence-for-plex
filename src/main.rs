#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod config;
mod discord;
mod metadata;
mod plex_account;
mod plex_server;
mod presence;
mod tray;

use config::Config;
use discord::DiscordClient;
use fs2::FileExt;
use log::{debug, error, info, warn};
use metadata::MetadataEnricher;
use plex_account::{PlexAccount, APP_NAME};
use plex_server::{MediaType, MediaUpdate, PlexServer, PlaybackState};
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
    let (media_tx, media_rx) = mpsc::unbounded_channel::<MediaUpdate>();
    let (status_tx, status_rx) = mpsc::unbounded_channel::<TrayStatus>();

    let tray_handle = tray::setup(tray_tx, config.plex_token.is_some());

    let mut discord = DiscordClient::new(&config.discord_client_id);
    discord.connect();
    let discord = Arc::new(Mutex::new(discord));

    let initial_sse_cancel = app_cancel_token.child_token();
    if let Some(ref token) = config.plex_token {
        begin_plex_monitoring(
            token.clone(),
            config.tmdb_token.clone(),
            media_tx.clone(),
            initial_sse_cancel.clone(),
        ).await;
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

async fn begin_plex_monitoring(
    token: String,
    tmdb_token: Option<String>,
    media_tx: mpsc::UnboundedSender<MediaUpdate>,
    cancel_token: CancellationToken,
) {
    debug!("Starting SSE loop");
    let enricher = Arc::new(MetadataEnricher::new(tmdb_token));

    let mut account = PlexAccount::new();
    account.fetch_username(&token).await;

    let servers = match account.get_servers(&token).await {
        Some(s) if !s.is_empty() => s,
        Some(_) => {
            warn!("No Plex servers found");
            return;
        }
        None => {
            error!("Failed to get servers");
            return;
        }
    };

    debug!("Found {} servers", servers.len());
    let username = account.username().map(|s| s.to_string());
    let mut spawned = 0;

    for server_info in servers {
        let Some(access_token) = server_info.access_token else {
            warn!("Server {} has no access token, skipping", server_info.name);
            continue;
        };

        debug!(
            "Setting up monitoring for server: {} ({} connections)",
            server_info.name,
            server_info.connections.len()
        );

        let server = PlexServer::new(
            server_info.name,
            server_info.connections,
            access_token,
            username.clone(),
        );

        let tx = media_tx.clone();
        let enricher = Arc::clone(&enricher);
        let cancel = cancel_token.clone();

        tokio::spawn(async move {
            tokio::select! {
                _ = cancel.cancelled() => {
                    info!("SSE monitoring cancelled");
                }
                _ = server.start_monitoring(tx, enricher) => {}
            }
        });
        spawned += 1;
    }

    debug!("Spawned {} server monitoring tasks", spawned);
}

async fn handle_media_updates(
    mut media_rx: mpsc::UnboundedReceiver<MediaUpdate>,
    discord: Arc<Mutex<DiscordClient>>,
    config: Arc<Config>,
    status_tx: mpsc::UnboundedSender<TrayStatus>,
) {
    debug!("Media update handler started");
    while let Some(update) = media_rx.recv().await {
        match update {
            MediaUpdate::Playing(boxed_info) => {
                let info = *boxed_info;
                debug!(
                    "Media update: {:?} - {} ({:?})",
                    info.media_type, info.title, info.state
                );
                let _ = status_tx.send(TrayStatus::from(info.state));

                let enabled = match info.media_type {
                    MediaType::Movie => config.enable_movies,
                    MediaType::Episode => config.enable_tv_shows,
                    MediaType::Track => config.enable_music,
                };
                debug!("Media type {:?} enabled: {}", info.media_type, enabled);

                let presence = build_presence(&info, &config);

                if enabled {
                    let mut discord = discord.lock().await;
                    if !discord.is_connected() {
                        debug!("Discord not connected, reconnecting");
                        discord.connect();
                    }
                    discord.update(&presence);
                } else {
                    debug!("Skipping Discord update, media type disabled");
                }
            }
            MediaUpdate::Stopped => {
                let _ = status_tx.send(TrayStatus::Idle);
                debug!("Playback stopped, clearing Discord activity");
                discord.lock().await.clear();
            }
        }
    }
    debug!("Media update handler exiting");
}

struct EventLoopContext<'a> {
    config: &'a Arc<Config>,
    discord: &'a Arc<Mutex<DiscordClient>>,
    media_tx: &'a mpsc::UnboundedSender<MediaUpdate>,
    app_cancel_token: &'a CancellationToken,
    tray_handle: Option<&'a tray::TrayHandle>,
    sse_cancel_token: Option<CancellationToken>,
    auth_pending: Option<oneshot::Receiver<Option<AuthResult>>>,
}

async fn run_event_loop(
    mut rx: mpsc::UnboundedReceiver<TrayCommand>,
    config: &Arc<Config>,
    discord: &Arc<Mutex<DiscordClient>>,
    media_tx: &mpsc::UnboundedSender<MediaUpdate>,
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
    debug!("Handling quit command");
    ctx.app_cancel_token.cancel();
    ctx.discord.lock().await.disconnect();
}

fn handle_authenticate(ctx: &mut EventLoopContext<'_>) {
    if ctx.auth_pending.is_some() {
        debug!("Authentication already pending, ignoring");
        return;
    }

    debug!("Starting authentication flow");
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

    debug!("Authentication complete, starting SSE monitoring");

    if let Some(handle) = ctx.tray_handle {
        handle.auth_item.set_text("Reauthenticate");
        handle.status_item.set_text(TrayStatus::Idle.as_str());
    }

    if let Some(ref old_token) = ctx.sse_cancel_token {
        debug!("Cancelling previous SSE monitoring");
        old_token.cancel();
    }

    let sse_cancel = ctx.app_cancel_token.child_token();
    ctx.sse_cancel_token = Some(sse_cancel.clone());

    let token = auth.token;
    let tmdb_token = auth.tmdb_token;
    let media_tx = ctx.media_tx.clone();

    tokio::spawn(async move {
        begin_plex_monitoring(token, tmdb_token, media_tx, sse_cancel).await;
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

    let account = PlexAccount::new();
    let token = run_auth_flow(&account).await?;

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

async fn run_auth_flow(account: &PlexAccount) -> Option<String> {
    let (pin_id, code) = account.request_pin().await?;

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

    tokio::time::timeout(Duration::from_secs(AUTH_TIMEOUT_SECS), async {
        loop {
            tokio::time::sleep(Duration::from_secs(AUTH_POLL_INTERVAL_SECS)).await;
            if let Some(token) = account.check_pin(pin_id).await {
                info!("Authentication successful");
                return token;
            }
        }
    })
    .await
    .ok()
}
