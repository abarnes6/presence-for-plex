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
use tray::{TrayCommand, TrayStatus};

const AUTH_TIMEOUT: Duration = Duration::from_secs(300);
const AUTH_POLL_INTERVAL: Duration = Duration::from_secs(2);

fn acquire_instance_lock() -> Option<File> {
    let path = Config::app_dir().join("presence-for-plex.lock");
    std::fs::create_dir_all(path.parent()?).ok();
    let file = File::create(&path).ok()?;
    file.try_lock_exclusive().ok()?;
    Some(file)
}

fn init_logging() {
    let path = Config::log_path();
    std::fs::create_dir_all(path.parent().unwrap()).ok();
    let mut loggers: Vec<Box<dyn simplelog::SharedLogger>> = vec![SimpleLogger::new(LevelFilter::Debug, LogConfig::default())];
    if let Ok(file) = File::create(&path) {
        loggers.push(WriteLogger::new(LevelFilter::Debug, LogConfig::default(), file));
    }
    let _ = CombinedLogger::init(loggers);
    info!("Starting Presence for Plex - Log: {}", path.display());
}

#[tokio::main]
async fn main() {
    let _lock = match acquire_instance_lock() {
        Some(f) => f,
        None => { eprintln!("Another instance running"); return; }
    };

    init_logging();

    #[cfg(target_os = "linux")]
    gtk::init().expect("Failed to initialize GTK");

    let config = Arc::new(Config::load());
    let cancel = CancellationToken::new();
    let (tray_tx, mut tray_rx) = mpsc::unbounded_channel::<TrayCommand>();
    let (media_tx, media_rx) = mpsc::unbounded_channel::<MediaUpdate>();
    let (status_tx, mut status_rx) = mpsc::unbounded_channel::<TrayStatus>();
    let tray = tray::setup(tray_tx, config.plex_token.is_some());

    let mut discord = DiscordClient::new(&config.discord_client_id);
    discord.connect();
    let discord = Arc::new(Mutex::new(discord));

    let mut sse_cancel = config.plex_token.as_ref().map(|token| {
        let c = cancel.child_token();
        let tx = media_tx.clone();
        let tmdb = config.tmdb_token.clone();
        let t = token.clone();
        tokio::spawn(async move { begin_monitoring(t, tmdb, tx, c).await });
        cancel.child_token()
    });

    tokio::spawn({
        let discord = Arc::clone(&discord);
        let config = Arc::clone(&config);
        async move { handle_media(media_rx, discord, config, status_tx).await }
    });

    let mut pump = tokio::time::interval(Duration::from_millis(16));
    let (auth_result_tx, mut auth_result_rx) = mpsc::channel::<(String, Option<String>)>(1);
    let mut auth_in_progress = false;

    loop {
        tokio::select! {
            biased;
            _ = pump.tick() => {
                #[cfg(windows)]
                pump_messages();
                #[cfg(target_os = "linux")]
                while gtk::events_pending() { gtk::main_iteration(); }
            }
            Some((token, tmdb)) = auth_result_rx.recv() => {
                auth_in_progress = false;
                if let Some(h) = tray.as_ref() { h.auth_item.set_text("Reauthenticate"); h.status_item.set_text(TrayStatus::Idle.as_str()); }
                if let Some(old) = sse_cancel.as_ref() { old.cancel(); }
                let c = cancel.child_token();
                sse_cancel = Some(c.clone());
                let tx = media_tx.clone();
                tokio::spawn(async move { begin_monitoring(token, tmdb, tx, c).await });
            }
            Some(status) = status_rx.recv() => { if let Some(h) = tray.as_ref() { h.status_item.set_text(status.as_str()); } }
            msg = tray_rx.recv() => match msg {
                Some(TrayCommand::Quit) => { cancel.cancel(); discord.lock().await.disconnect(); break; }
                Some(TrayCommand::Authenticate) if !auth_in_progress => {
                    auth_in_progress = true;
                    let auth_tx = auth_result_tx.clone();
                    let tmdb = config.tmdb_token.clone();
                    tokio::spawn(async move { if let Some(r) = run_auth(tmdb).await { let _ = auth_tx.send(r).await; } });
                }
                _ => {}
            }
        }
    }
    info!("Shutting down");
}

async fn begin_monitoring(token: String, tmdb: Option<String>, tx: mpsc::UnboundedSender<MediaUpdate>, cancel: CancellationToken) {
    let enricher = Arc::new(MetadataEnricher::new(tmdb));
    let mut account = PlexAccount::new();
    account.fetch_username(&token).await;

    let servers = match account.get_servers(&token).await {
        Some(s) if !s.is_empty() => s,
        _ => { warn!("No Plex servers found"); return; }
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

async fn run_auth(tmdb: Option<String>) -> Option<(String, Option<String>)> {
    info!("Starting Plex auth");
    let account = PlexAccount::new();
    let (pin_id, code) = account.request_pin().await?;
    let url = format!("https://app.plex.tv/auth#?clientID={}&code={}&context%5Bdevice%5D%5Bproduct%5D=Presence%20for%20Plex", utf8_percent_encode(APP_NAME, NON_ALPHANUMERIC), utf8_percent_encode(&code, NON_ALPHANUMERIC));
    if let Err(e) = open::that(&url) { warn!("Browser failed: {}", e); }

    let token = tokio::time::timeout(AUTH_TIMEOUT, async {
        loop { tokio::time::sleep(AUTH_POLL_INTERVAL).await; if let Some(t) = account.check_pin(pin_id).await { return t; } }
    }).await.ok()?;

    let mut cfg = Config::load();
    cfg.plex_token = Some(token.clone());
    if let Err(e) = cfg.save() { error!("Config save failed: {}", e); }
    info!("Auth complete");
    Some((token, tmdb))
}
