#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod config;
mod discord;
mod error;
mod media;
mod metadata;
mod plex_account;
mod plex_server;
mod presence;
#[cfg(feature = "tray")]
mod tray;

use config::Config;
use discord::DiscordHandle;
use error::NetError;
use log::{error, info, warn};
use media::{AppStatus, MediaUpdate, PlaybackState, SessionId};
use metadata::MetadataEnricher;
use percent_encoding::{NON_ALPHANUMERIC, utf8_percent_encode};
use plex_account::{PlexAccount, ServerInfo};
use plex_server::PlexServer;
use presence::{SessionArbiter, build_presence};
use simplelog::{CombinedLogger, Config as LogConfig, LevelFilter, SimpleLogger, WriteLogger};
use std::fs::File;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;
#[cfg(feature = "tray")]
use tray::{TrayCommand, TrayHandle};

const AUTH_TIMEOUT: Duration = Duration::from_secs(300);
const AUTH_POLL_INTERVAL: Duration = Duration::from_secs(2);
const DISCOVERY_RETRY_INITIAL: Duration = Duration::from_secs(5);
const DISCOVERY_RETRY_MAX: Duration = Duration::from_secs(300);
const REASSERT_INTERVAL: Duration = Duration::from_secs(30);
const SHUTDOWN_TIMEOUT: Duration = Duration::from_secs(2);

#[derive(Clone)]
struct MonitorContext {
    tmdb: Option<String>,
    client_id: String,
    cancel: CancellationToken,
    media_tx: mpsc::UnboundedSender<MediaUpdate>,
    status_tx: mpsc::UnboundedSender<AppStatus>,
}

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

    let mut config = Config::load();
    let client_id = config.ensure_client_identifier();
    let config = Arc::new(config);
    let cancel = CancellationToken::new();
    let (media_tx, media_rx) = mpsc::unbounded_channel::<MediaUpdate>();
    let (status_tx, status_rx) = mpsc::unbounded_channel::<AppStatus>();

    #[cfg(feature = "tray")]
    let (tray_tx, tray_rx) = mpsc::unbounded_channel::<TrayCommand>();
    #[cfg(feature = "tray")]
    let tray = tray::setup(tray_tx, config.plex_token.is_some());
    #[cfg(not(feature = "tray"))]
    drop(status_rx); // no consumer without a tray; dropped so sends stay no-ops

    let discord = discord::spawn(&config.discord_client_id);

    tokio::spawn(handle_media(
        media_rx,
        discord.clone(),
        Arc::clone(&config),
        status_tx.clone(),
    ));

    let ctx = MonitorContext {
        tmdb: config.tmdb_token.clone(),
        client_id,
        cancel: cancel.clone(),
        media_tx,
        status_tx,
    };
    let sse_cancel = config
        .plex_token
        .clone()
        .map(|token| spawn_monitoring(token, &ctx));
    if sse_cancel.is_none() {
        warn!(
            "No Plex token configured - run `presence-for-plex --auth` or use the tray menu to authenticate"
        );
    }

    #[cfg(feature = "tray")]
    run_tray(tray, tray_rx, status_rx, sse_cancel, &ctx).await;

    #[cfg(not(feature = "tray"))]
    {
        let _ = sse_cancel;
        tokio::signal::ctrl_c().await.ok();
    }

    cancel.cancel();
    let _ = tokio::time::timeout(SHUTDOWN_TIMEOUT, discord.shutdown()).await;
    info!("Shutting down");
}

fn acquire_instance_lock() -> Result<File, String> {
    let dir = Config::ensure_app_dir()
        .map_err(|e| format!("Cannot create {}: {}", Config::app_dir().display(), e))?;
    let path = dir.join("presence-for-plex.lock");
    let file = File::create(&path)
        .map_err(|e| format!("Cannot create lock file {}: {}", path.display(), e))?;
    file.try_lock()
        .map_err(|_| "Another instance is already running".to_string())?;
    Ok(file)
}

fn init_logging() {
    let _ = Config::ensure_app_dir();
    let path = Config::log_path();
    let level = std::env::var("RUST_LOG")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(LevelFilter::Info);
    let mut loggers: Vec<Box<dyn simplelog::SharedLogger>> =
        vec![SimpleLogger::new(level, LogConfig::default())];
    if let Ok(file) = File::create(&path) {
        // The log records usernames, viewing history, and during auth the
        // pin URL; scope it to the user like the config file
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let _ = file.set_permissions(std::fs::Permissions::from_mode(0o600));
        }
        loggers.push(WriteLogger::new(level, LogConfig::default(), file));
    }
    let _ = CombinedLogger::init(loggers);
    info!("Starting Presence for Plex - Log: {}", path.display());
}

#[cfg(feature = "tray")]
async fn run_tray(
    tray: Option<TrayHandle>,
    mut tray_rx: mpsc::UnboundedReceiver<TrayCommand>,
    mut status_rx: mpsc::UnboundedReceiver<AppStatus>,
    mut sse_cancel: Option<CancellationToken>,
    ctx: &MonitorContext,
) {
    let Some(tray) = tray else {
        warn!("Tray unavailable, Ctrl+C to quit");
        tokio::signal::ctrl_c().await.ok();
        return;
    };

    // Linux GTK pumps its own thread; Windows/macOS need it done here
    let needs_pump = cfg!(any(windows, target_os = "macos"));
    let mut pump = tokio::time::interval(Duration::from_millis(16));
    let (auth_tx, mut auth_rx) = mpsc::channel::<Option<String>>(1);
    let mut auth_in_progress = false;

    loop {
        tokio::select! {
            biased;
            _ = pump.tick(), if needs_pump => {
                #[cfg(any(windows, target_os = "macos"))]
                pump_ui();
            }
            Some(token) = auth_rx.recv() => {
                auth_in_progress = false;
                match token {
                    Some(token) => {
                        tray.set_authenticated(true);
                        tray.set_status(AppStatus::Idle);
                        if let Some(old) = sse_cancel.take() {
                            old.cancel();
                        }
                        sse_cancel = Some(spawn_monitoring(token, ctx));
                    }
                    None => {
                        warn!("Auth failed or timed out");
                        if sse_cancel.is_none() {
                            tray.set_status(AppStatus::NotAuthenticated);
                        }
                    }
                }
            }
            Some(status) = status_rx.recv() => tray.set_status(status),
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

fn spawn_monitoring(token: String, ctx: &MonitorContext) -> CancellationToken {
    let child = ctx.cancel.child_token();
    let ctx = MonitorContext {
        cancel: child.clone(),
        ..ctx.clone()
    };
    tokio::spawn(begin_monitoring(token, ctx));
    child
}

async fn discover_servers(
    account: &mut PlexAccount,
    token: &str,
) -> Result<Vec<ServerInfo>, (&'static str, NetError)> {
    if account.username().is_none() {
        account
            .fetch_username(token)
            .await
            .map_err(|e| ("Account fetch failed", e))?;
    }
    account
        .get_servers(token)
        .await
        .map_err(|e| ("Server discovery failed", e))
}

async fn begin_monitoring(token: String, ctx: MonitorContext) {
    let MonitorContext {
        tmdb,
        client_id,
        cancel,
        media_tx: tx,
        status_tx,
    } = ctx;
    let enricher = Arc::new(MetadataEnricher::new(tmdb));
    let mut account = PlexAccount::new(&client_id);

    // Retry discovery, the network may not be up yet at login
    let mut delay = DISCOVERY_RETRY_INITIAL;
    let servers = loop {
        match discover_servers(&mut account, &token).await {
            Ok(servers) if !servers.is_empty() => break servers,
            Ok(_) => warn!("No servers found, retrying in {}s", delay.as_secs()),
            Err((_, e)) if e.is_auth() => return auth_rejected(&e, &status_tx, &cancel),
            Err((step, e)) => warn!("{}: {}, retrying in {}s", step, e, delay.as_secs()),
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
            info!("Skipping server {} (no access token)", srv.name);
            continue;
        };
        let name = srv.name.clone();
        let server = PlexServer::new(
            srv.name,
            srv.connections,
            access,
            username.clone(),
            client_id.clone(),
        );
        let tx = tx.clone();
        let enricher = Arc::clone(&enricher);
        let c = cancel.clone();
        tokio::spawn(async move {
            tokio::select! { _ = c.cancelled() => {} _ = server.start_monitoring(&tx, enricher) => {} }
            let _ = tx.send(MediaUpdate::ServerGone(name));
        });
    }
}

fn auth_rejected(
    e: &NetError,
    status_tx: &mpsc::UnboundedSender<AppStatus>,
    cancel: &CancellationToken,
) {
    // A cancelled generation lost the right to report: a newer one (fresh
    // token) owns the status now
    if cancel.is_cancelled() {
        return;
    }
    error!(
        "Plex rejected the token ({}). Re-authenticate with --auth or the tray menu",
        e
    );
    let _ = status_tx.send(AppStatus::NotAuthenticated);
}

#[derive(PartialEq)]
struct ShownPresence {
    session: SessionId,
    rating_key: Option<String>,
    state: PlaybackState,
    offset_ms: u64,
}

async fn handle_media(
    mut rx: mpsc::UnboundedReceiver<MediaUpdate>,
    discord: DiscordHandle,
    config: Arc<Config>,
    status_tx: mpsc::UnboundedSender<AppStatus>,
) {
    let mut arbiter = SessionArbiter::new();
    let mut shown: Option<ShownPresence> = None;
    let mut reassert = tokio::time::interval(REASSERT_INTERVAL);
    reassert.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);

    loop {
        tokio::select! {
            update = rx.recv() => {
                let Some(update) = update else { break };
                match update {
                    MediaUpdate::Playing(id, info) => {
                        if config.enables(info.media_type) {
                            arbiter.playing(id, info);
                        } else {
                            // A disabled type may not hold the presence slot
                            arbiter.stopped(&id);
                        }
                    }
                    MediaUpdate::Stopped(id) => arbiter.stopped(&id),
                    MediaUpdate::ServerGone(server) => arbiter.server_gone(&server),
                }
                sync_presence(&arbiter, &discord, &config, &status_tx, &mut shown);
            }
            _ = reassert.tick() => {
                // Re-send so presence survives a Discord restart
                if let Some(cur) = arbiter.current() {
                    discord.update(build_presence(&cur.extrapolated(), &config));
                }
            }
        }
    }
}

fn sync_presence(
    arbiter: &SessionArbiter,
    discord: &DiscordHandle,
    config: &Config,
    status_tx: &mpsc::UnboundedSender<AppStatus>,
    shown: &mut Option<ShownPresence>,
) {
    match arbiter.current() {
        Some(cur) => {
            let info = cur.info();
            let _ = status_tx.send(AppStatus::from(info.state));
            let key = ShownPresence {
                session: cur.id.clone(),
                rating_key: info.rating_key.clone(),
                state: info.state,
                offset_ms: info.view_offset_ms,
            };
            if shown.as_ref() != Some(&key) {
                discord.update(build_presence(&cur.extrapolated(), config));
                *shown = Some(key);
            }
        }
        None => {
            if shown.take().is_some() {
                let _ = status_tx.send(AppStatus::Idle);
                discord.clear();
            }
        }
    }
}

#[cfg(windows)]
fn pump_ui() {
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
fn pump_ui() {
    use objc2_core_foundation::{CFRunLoop, kCFRunLoopDefaultMode};
    CFRunLoop::run_in_mode(unsafe { kCFRunLoopDefaultMode }, 0.0, false);
}

async fn run_auth() -> Option<String> {
    info!("Starting Plex auth");
    let mut cfg = Config::load();
    let client_id = cfg.ensure_client_identifier();
    let account = PlexAccount::new(&client_id);
    let (pin_id, code) = account
        .request_pin()
        .await
        .inspect_err(|e| error!("Pin request failed: {}", e))
        .ok()?;
    let url = format!(
        "https://app.plex.tv/auth#?clientID={}&code={}&context%5Bdevice%5D%5Bproduct%5D=Presence%20for%20Plex",
        utf8_percent_encode(&client_id, NON_ALPHANUMERIC),
        utf8_percent_encode(&code, NON_ALPHANUMERIC)
    );
    println!("Open to authenticate:\n{}", url);
    // Also into the log file: a windows_subsystem build has no console
    info!("Auth URL: {}", url);
    if let Err(e) = open::that(&url) {
        warn!("Browser failed: {}", e);
    }

    let token = tokio::time::timeout(AUTH_TIMEOUT, async {
        loop {
            tokio::time::sleep(AUTH_POLL_INTERVAL).await;
            match account.check_pin(pin_id, &code).await {
                Ok(Some(token)) => return token,
                Ok(None) => {}
                Err(e) => warn!("Pin check failed, still waiting: {}", e),
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
