#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod config;
mod discord;
mod metadata;
mod plex_account;
mod plex_server;
mod presence;
#[cfg(feature = "tray")]
mod tray;

use config::Config;
use discord::DiscordClient;
use fs2::FileExt;
use log::{error, info, warn};
use metadata::MetadataEnricher;
use percent_encoding::{utf8_percent_encode, NON_ALPHANUMERIC};
use plex_account::{PlexAccount, APP_NAME};
use plex_server::{MediaType, MediaUpdate, PlexServer};
use presence::build_presence;
use simplelog::{CombinedLogger, Config as LogConfig, LevelFilter, SimpleLogger, WriteLogger};
use std::fs::File;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::{mpsc, Mutex};
use tokio_util::sync::CancellationToken;
#[cfg(feature = "tray")]
use tray::{TrayCommand, TrayStatus};

const AUTH_TIMEOUT: Duration = Duration::from_secs(300);
const AUTH_POLL_INTERVAL: Duration = Duration::from_secs(2);
const DISCOVERY_RETRY_INITIAL: Duration = Duration::from_secs(5);
const DISCOVERY_RETRY_MAX: Duration = Duration::from_secs(300);

fn acquire_instance_lock() -> Result<File, String> {
    let dir = Config::app_dir();
    std::fs::create_dir_all(&dir).map_err(|e| format!("Cannot create {}: {}", dir.display(), e))?;
    let path = dir.join("presence-for-plex.lock");
    let file = File::create(&path).map_err(|e| format!("Cannot create lock file {}: {}", path.display(), e))?;
    file.try_lock_exclusive().map_err(|_| "Another instance is already running".to_string())?;
    Ok(file)
}

fn init_logging() {
    let path = Config::log_path();
    std::fs::create_dir_all(path.parent().unwrap()).ok();
    let level = std::env::var("RUST_LOG").ok().and_then(|s| s.parse().ok()).unwrap_or(LevelFilter::Info);
    let mut loggers: Vec<Box<dyn simplelog::SharedLogger>> = vec![SimpleLogger::new(level, LogConfig::default())];
    if let Ok(file) = File::create(&path) {
        loggers.push(WriteLogger::new(level, LogConfig::default(), file));
    }
    let _ = CombinedLogger::init(loggers);
    info!("Starting Presence for Plex - Log: {}", path.display());
}

fn spawn_monitoring(token: String, tmdb: Option<String>, cancel: &CancellationToken, media_tx: &mpsc::UnboundedSender<MediaUpdate>) -> CancellationToken {
    let c = cancel.child_token();
    let monitor_cancel = c.clone();
    let tx = media_tx.clone();
    tokio::spawn(async move { begin_monitoring(token, tmdb, tx, monitor_cancel).await });
    c
}

#[tokio::main]
async fn main() {
    let _lock = match acquire_instance_lock() {
        Ok(f) => f,
        Err(e) => { eprintln!("{}", e); return; }
    };

    init_logging();

    if std::env::args().any(|a| a == "--auth") {
        match run_auth().await {
            Some(_) => info!("Authentication successful - token saved. Start the app normally to begin monitoring."),
            None => error!("Authentication failed or timed out"),
        }
        return;
    }

    let config = Arc::new(Config::load());
    let cancel = CancellationToken::new();

    let (media_tx, media_rx) = mpsc::unbounded_channel::<MediaUpdate>();
    #[cfg(feature = "tray")]
    let (tray_tx, mut tray_rx) = mpsc::unbounded_channel::<TrayCommand>();
    #[cfg(feature = "tray")]
    let (status_tx, mut status_rx) = mpsc::unbounded_channel::<TrayStatus>();
    #[cfg(feature = "tray")]
    let tray = tray::setup(tray_tx, config.plex_token.is_some());

    let mut discord = DiscordClient::new(&config.discord_client_id);
    discord.connect();
    let discord = Arc::new(Mutex::new(discord));

    #[cfg(feature = "tray")]
    let mut sse_cancel = config.plex_token.clone().map(|token| spawn_monitoring(token, config.tmdb_token.clone(), &cancel, &media_tx));
    #[cfg(not(feature = "tray"))]
    let _sse_cancel = config.plex_token.clone().map(|token| spawn_monitoring(token, config.tmdb_token.clone(), &cancel, &media_tx));

    #[cfg(feature = "tray")]
    tokio::spawn({
        let discord = Arc::clone(&discord);
        let config = Arc::clone(&config);
        async move { handle_media(media_rx, discord, config, status_tx).await }
    });

    #[cfg(not(feature = "tray"))]
    tokio::spawn({
        let discord = Arc::clone(&discord);
        let config = Arc::clone(&config);
        async move { handle_media(media_rx, discord, config).await }
    });

    #[cfg(feature = "tray")]
    {
        if tray.is_none() {
            warn!("System tray unavailable - running without tray (Ctrl+C to quit)");
            tokio::signal::ctrl_c().await.ok();
        } else {
            // Windows/macOS need the UI event loop pumped from this thread; on Linux
            // the tray runs its own GTK thread, so the tick is just a keepalive.
            let pump_period = if cfg!(any(windows, target_os = "macos")) { Duration::from_millis(16) } else { Duration::from_secs(3600) };
            let mut pump = tokio::time::interval(pump_period);
            let (auth_result_tx, mut auth_result_rx) = mpsc::channel::<Option<String>>(1);
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
                    Some(result) = auth_result_rx.recv() => {
                        auth_in_progress = false;
                        match result {
                            Some(token) => {
                                if let Some(h) = tray.as_ref() { h.set_auth_text("Reauthenticate"); h.set_status_text(TrayStatus::Idle.as_str()); }
                                if let Some(old) = sse_cancel.as_ref() { old.cancel(); }
                                sse_cancel = Some(spawn_monitoring(token, config.tmdb_token.clone(), &cancel, &media_tx));
                            }
                            None => {
                                warn!("Plex authentication failed or timed out");
                                if sse_cancel.is_none() && let Some(h) = tray.as_ref() { h.set_status_text(TrayStatus::NotAuthenticated.as_str()); }
                            }
                        }
                    }
                    Some(status) = status_rx.recv() => { if let Some(h) = tray.as_ref() { h.set_status_text(status.as_str()); } }
                    Some(msg) = tray_rx.recv() => match msg {
                        TrayCommand::Quit => break,
                        TrayCommand::Authenticate if !auth_in_progress => {
                            auth_in_progress = true;
                            let auth_tx = auth_result_tx.clone();
                            tokio::spawn(async move { let _ = auth_tx.send(run_auth().await).await; });
                        }
                        _ => {}
                    }
                }
            }
        }
    }

    #[cfg(not(feature = "tray"))]
    tokio::signal::ctrl_c().await.ok();

    cancel.cancel();
    discord.lock().await.disconnect();
    info!("Shutting down");
}

async fn begin_monitoring(token: String, tmdb: Option<String>, tx: mpsc::UnboundedSender<MediaUpdate>, cancel: CancellationToken) {
    let enricher = Arc::new(MetadataEnricher::new(tmdb));
    let mut account = PlexAccount::new();

    // Server discovery can fail transiently (e.g. app starts before the network
    // is up at login), so retry with backoff instead of giving up.
    let mut delay = DISCOVERY_RETRY_INITIAL;
    let servers = loop {
        if account.username().is_none() && account.fetch_username(&token).await.is_none() {
            warn!("Could not fetch Plex account info; retrying in {}s", delay.as_secs());
        } else {
            match account.get_servers(&token).await {
                Some(s) if !s.is_empty() => break s,
                _ => warn!("No Plex servers found; retrying in {}s", delay.as_secs()),
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
        let Some(access) = srv.access_token else { continue };
        let server = PlexServer::new(srv.name, srv.connections, access, username.clone());
        let tx = tx.clone();
        let enricher = Arc::clone(&enricher);
        let c = cancel.clone();
        tokio::spawn(async move {
            tokio::select! { _ = c.cancelled() => {} _ = server.start_monitoring(tx, enricher) => {} }
        });
    }
}

#[cfg(feature = "tray")]
async fn handle_media(mut rx: mpsc::UnboundedReceiver<MediaUpdate>, discord: Arc<Mutex<DiscordClient>>, config: Arc<Config>, status_tx: mpsc::UnboundedSender<TrayStatus>) {
    while let Some(update) = rx.recv().await {
        match update {
            MediaUpdate::Playing(info) => {
                let _ = status_tx.send(TrayStatus::from(info.state));
                let enabled = match info.media_type { MediaType::Movie => config.enable_movies, MediaType::Episode => config.enable_tv_shows, MediaType::Track => config.enable_music };
                if enabled {
                    let mut d = discord.lock().await;
                    if !d.is_connected() { d.connect(); }
                    d.update(&build_presence(&info, &config));
                }
            }
            MediaUpdate::Stopped => { let _ = status_tx.send(TrayStatus::Idle); discord.lock().await.clear(); }
        }
    }
}

#[cfg(not(feature = "tray"))]
async fn handle_media(mut rx: mpsc::UnboundedReceiver<MediaUpdate>, discord: Arc<Mutex<DiscordClient>>, config: Arc<Config>) {
    while let Some(update) = rx.recv().await {
        match update {
            MediaUpdate::Playing(info) => {
                let enabled = match info.media_type { MediaType::Movie => config.enable_movies, MediaType::Episode => config.enable_tv_shows, MediaType::Track => config.enable_music };
                if enabled {
                    let mut d = discord.lock().await;
                    if !d.is_connected() { d.connect(); }
                    d.update(&build_presence(&info, &config));
                }
            }
            MediaUpdate::Stopped => { discord.lock().await.clear(); }
        }
    }
}

#[cfg(windows)]
fn pump_messages() {
    use windows_sys::Win32::UI::WindowsAndMessaging::{DispatchMessageW, PeekMessageW, TranslateMessage, MSG, PM_REMOVE};
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
    let url = format!("https://app.plex.tv/auth#?clientID={}&code={}&context%5Bdevice%5D%5Bproduct%5D=Presence%20for%20Plex", utf8_percent_encode(APP_NAME, NON_ALPHANUMERIC), utf8_percent_encode(&code, NON_ALPHANUMERIC));
    println!("Open this URL to link your Plex account:\n{}", url);
    if let Err(e) = open::that(&url) { warn!("Browser failed: {}", e); }

    let token = tokio::time::timeout(AUTH_TIMEOUT, async {
        loop { tokio::time::sleep(AUTH_POLL_INTERVAL).await; if let Some(t) = account.check_pin(pin_id).await { return t; } }
    }).await.ok()?;

    let mut cfg = Config::load();
    cfg.plex_token = Some(token.clone());
    if let Err(e) = cfg.save() { error!("Config save failed: {}", e); }
    info!("Auth complete");
    Some(token)
}
