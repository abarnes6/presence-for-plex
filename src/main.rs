#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod config;
mod discord;
mod media;
mod metadata;
mod plex_account;
mod plex_server;
mod presence;
#[cfg(feature = "tray")]
mod tray;

use config::Config;
use discord::DiscordClient;
use log::{error, info, warn};
use media::{MediaType, MediaUpdate};
use metadata::MetadataEnricher;
use percent_encoding::{NON_ALPHANUMERIC, utf8_percent_encode};
use plex_account::{APP_NAME, PlexAccount};
use plex_server::PlexServer;
use presence::build_presence;
use simplelog::{CombinedLogger, Config as LogConfig, LevelFilter, SimpleLogger, WriteLogger};
use std::fs::File;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::{Mutex, mpsc};
use tokio_util::sync::CancellationToken;
#[cfg(feature = "tray")]
use tray::{TrayCommand, TrayHandle, TrayStatus};

const AUTH_TIMEOUT: Duration = Duration::from_secs(300);
const AUTH_POLL_INTERVAL: Duration = Duration::from_secs(2);
const DISCOVERY_RETRY_INITIAL: Duration = Duration::from_secs(5);
const DISCOVERY_RETRY_MAX: Duration = Duration::from_secs(300);

#[tokio::main]
async fn main() {
    let _lock = match acquire_instance_lock() {
        Ok(f) => f,
        Err(e) => {
            eprintln!("{}", e);
            return;
        }
    };

    init_logging();

    if std::env::args().any(|a| a == "--auth") {
        match run_auth().await {
            Some(_) => info!("Token saved"),
            None => error!("Auth failed or timed out"),
        }
        return;
    }

    let config = Arc::new(Config::load());
    let cancel = CancellationToken::new();
    let (media_tx, media_rx) = mpsc::unbounded_channel::<MediaUpdate>();

    #[cfg(feature = "tray")]
    let (tray_tx, tray_rx) = mpsc::unbounded_channel::<TrayCommand>();
    #[cfg(feature = "tray")]
    let (status_tx, status_rx) = mpsc::unbounded_channel::<TrayStatus>();
    #[cfg(feature = "tray")]
    let tray = tray::setup(tray_tx, config.plex_token.is_some());

    let mut discord = DiscordClient::new(&config.discord_client_id);
    discord.connect();
    let discord = Arc::new(Mutex::new(discord));

    #[cfg(feature = "tray")]
    let media_task = handle_media(
        media_rx,
        Arc::clone(&discord),
        Arc::clone(&config),
        status_tx,
    );
    #[cfg(not(feature = "tray"))]
    let media_task = handle_media(media_rx, Arc::clone(&discord), Arc::clone(&config));
    tokio::spawn(media_task);

    let sse_cancel = config
        .plex_token
        .clone()
        .map(|token| spawn_monitoring(token, config.tmdb_token.clone(), &cancel, &media_tx));

    #[cfg(feature = "tray")]
    run_tray(
        tray, tray_rx, status_rx, sse_cancel, &config, &cancel, &media_tx,
    )
    .await;

    #[cfg(not(feature = "tray"))]
    {
        let _ = sse_cancel;
        tokio::signal::ctrl_c().await.ok();
    }

    cancel.cancel();
    discord.lock().await.disconnect();
    info!("Shutting down");
}

fn acquire_instance_lock() -> Result<File, String> {
    let dir = Config::app_dir();
    std::fs::create_dir_all(&dir).map_err(|e| format!("Cannot create {}: {}", dir.display(), e))?;
    let path = dir.join("presence-for-plex.lock");
    let file = File::create(&path)
        .map_err(|e| format!("Cannot create lock file {}: {}", path.display(), e))?;
    file.try_lock()
        .map_err(|_| "Another instance is already running".to_string())?;
    Ok(file)
}

fn init_logging() {
    let path = Config::log_path();
    std::fs::create_dir_all(path.parent().unwrap()).ok();
    let level = std::env::var("RUST_LOG")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(LevelFilter::Info);
    let mut loggers: Vec<Box<dyn simplelog::SharedLogger>> =
        vec![SimpleLogger::new(level, LogConfig::default())];
    if let Ok(file) = File::create(&path) {
        loggers.push(WriteLogger::new(level, LogConfig::default(), file));
    }
    let _ = CombinedLogger::init(loggers);
    info!("Starting Presence for Plex - Log: {}", path.display());
}

#[cfg(feature = "tray")]
async fn run_tray(
    tray: Option<TrayHandle>,
    mut tray_rx: mpsc::UnboundedReceiver<TrayCommand>,
    mut status_rx: mpsc::UnboundedReceiver<TrayStatus>,
    mut sse_cancel: Option<CancellationToken>,
    config: &Config,
    cancel: &CancellationToken,
    media_tx: &mpsc::UnboundedSender<MediaUpdate>,
) {
    let Some(tray) = tray else {
        warn!("Tray unavailable, Ctrl+C to quit");
        tokio::signal::ctrl_c().await.ok();
        return;
    };

    // Only Windows/macOS need the UI loop pumped from this thread
    let pump_period = if cfg!(any(windows, target_os = "macos")) {
        Duration::from_millis(16)
    } else {
        Duration::from_secs(3600)
    };
    let mut pump = tokio::time::interval(pump_period);
    let (auth_tx, mut auth_rx) = mpsc::channel::<Option<String>>(1);
    let mut auth_in_progress = false;

    loop {
        tokio::select! {
            biased;
            _ = pump.tick() => {
                #[cfg(windows)]
                pump_messages();
                #[cfg(target_os = "macos")]
                pump_macos();
            }
            Some(token) = auth_rx.recv() => {
                auth_in_progress = false;
                match token {
                    Some(token) => {
                        tray.set_auth_text("Reauthenticate");
                        tray.set_status_text(TrayStatus::Idle.as_str());
                        if let Some(old) = sse_cancel.take() {
                            old.cancel();
                        }
                        sse_cancel =
                            Some(spawn_monitoring(token, config.tmdb_token.clone(), cancel, media_tx));
                    }
                    None => {
                        warn!("Auth failed or timed out");
                        if sse_cancel.is_none() {
                            tray.set_status_text(TrayStatus::NotAuthenticated.as_str());
                        }
                    }
                }
            }
            Some(status) = status_rx.recv() => tray.set_status_text(status.as_str()),
            Some(cmd) = tray_rx.recv() => match cmd {
                TrayCommand::Quit => break,
                TrayCommand::Authenticate if !auth_in_progress => {
                    auth_in_progress = true;
                    let auth_tx = auth_tx.clone();
                    tokio::spawn(async move {
                        let _ = auth_tx.send(run_auth().await).await;
                    });
                }
                _ => {}
            }
        }
    }
}

fn spawn_monitoring(
    token: String,
    tmdb: Option<String>,
    cancel: &CancellationToken,
    media_tx: &mpsc::UnboundedSender<MediaUpdate>,
) -> CancellationToken {
    let c = cancel.child_token();
    let monitor_cancel = c.clone();
    let tx = media_tx.clone();
    tokio::spawn(async move { begin_monitoring(token, tmdb, tx, monitor_cancel).await });
    c
}

async fn begin_monitoring(
    token: String,
    tmdb: Option<String>,
    tx: mpsc::UnboundedSender<MediaUpdate>,
    cancel: CancellationToken,
) {
    let enricher = Arc::new(MetadataEnricher::new(tmdb));
    let mut account = PlexAccount::new();

    // Retry discovery, the network may not be up yet at login
    let mut delay = DISCOVERY_RETRY_INITIAL;
    let servers = loop {
        if account.username().is_none() && account.fetch_username(&token).await.is_none() {
            warn!("Account fetch failed, retrying in {}s", delay.as_secs());
        } else {
            match account.get_servers(&token).await {
                Some(s) if !s.is_empty() => break s,
                _ => warn!("No servers found, retrying in {}s", delay.as_secs()),
            }
        }
        tokio::select! {
            _ = cancel.cancelled() => return,
            _ = tokio::time::sleep(delay) => {}
        }
        delay = (delay * 2).min(DISCOVERY_RETRY_MAX);
    };

    let username = account.username().map(String::from);
    for srv in servers {
        let Some(access) = srv.access_token else {
            continue;
        };
        let server = PlexServer::new(srv.name, srv.connections, access, username.clone());
        let tx = tx.clone();
        let enricher = Arc::clone(&enricher);
        let c = cancel.clone();
        tokio::spawn(async move {
            tokio::select! { _ = c.cancelled() => {} _ = server.start_monitoring(tx, enricher) => {} }
        });
    }
}

async fn handle_media(
    mut rx: mpsc::UnboundedReceiver<MediaUpdate>,
    discord: Arc<Mutex<DiscordClient>>,
    config: Arc<Config>,
    #[cfg(feature = "tray")] status_tx: mpsc::UnboundedSender<TrayStatus>,
) {
    while let Some(update) = rx.recv().await {
        match update {
            MediaUpdate::Playing(info) => {
                #[cfg(feature = "tray")]
                let _ = status_tx.send(TrayStatus::from(info.state));

                let enabled = match info.media_type {
                    MediaType::Movie => config.enable_movies,
                    MediaType::Episode => config.enable_tv_shows,
                    MediaType::Track => config.enable_music,
                };
                if enabled {
                    let mut d = discord.lock().await;
                    if !d.is_connected() {
                        d.connect();
                    }
                    d.update(&build_presence(&info, &config));
                }
            }
            MediaUpdate::Stopped => {
                #[cfg(feature = "tray")]
                let _ = status_tx.send(TrayStatus::Idle);

                discord.lock().await.clear();
            }
        }
    }
}

#[cfg(windows)]
fn pump_messages() {
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        DispatchMessageW, MSG, PM_REMOVE, PeekMessageW, TranslateMessage,
    };
    unsafe {
        let mut msg: MSG = std::mem::zeroed();
        while PeekMessageW(&mut msg, std::ptr::null_mut(), 0, 0, PM_REMOVE) != 0 {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

#[cfg(target_os = "macos")]
fn pump_macos() {
    use objc2_core_foundation::{CFRunLoop, kCFRunLoopDefaultMode};
    CFRunLoop::run_in_mode(unsafe { kCFRunLoopDefaultMode }, 0.0, false);
}

async fn run_auth() -> Option<String> {
    info!("Starting Plex auth");
    let account = PlexAccount::new();
    let (pin_id, code) = account.request_pin().await?;
    let url = format!(
        "https://app.plex.tv/auth#?clientID={}&code={}&context%5Bdevice%5D%5Bproduct%5D=Presence%20for%20Plex",
        utf8_percent_encode(APP_NAME, NON_ALPHANUMERIC),
        utf8_percent_encode(&code, NON_ALPHANUMERIC)
    );
    println!("Open to authenticate:\n{}", url);
    if let Err(e) = open::that(&url) {
        warn!("Browser failed: {}", e);
    }

    let token = tokio::time::timeout(AUTH_TIMEOUT, async {
        loop {
            tokio::time::sleep(AUTH_POLL_INTERVAL).await;
            if let Some(t) = account.check_pin(pin_id).await {
                return t;
            }
        }
    })
    .await
    .ok()?;

    let mut cfg = Config::load();
    cfg.plex_token = Some(token.clone());
    if let Err(e) = cfg.save() {
        error!("Config save failed: {}", e);
    }
    info!("Auth complete");
    Some(token)
}
