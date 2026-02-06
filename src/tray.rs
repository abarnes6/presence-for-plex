use crossbeam_channel::RecvTimeoutError;
use image::GenericImageView;
use std::time::Duration;
use tokio::sync::mpsc::UnboundedSender;
use tray_icon::{menu::{Menu, MenuEvent, MenuItem, PredefinedMenuItem}, Icon, TrayIconBuilder};

use crate::plex_server::PlaybackState;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TrayStatus { Idle, Playing, Paused, Buffering, NotAuthenticated }

impl TrayStatus {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Idle => "Status: Idle", Self::Playing => "Status: Playing",
            Self::Paused => "Status: Paused", Self::Buffering => "Status: Buffering",
            Self::NotAuthenticated => "Status: Not Authenticated",
        }
    }
}

impl From<PlaybackState> for TrayStatus {
    fn from(s: PlaybackState) -> Self {
        match s { PlaybackState::Playing => Self::Playing, PlaybackState::Paused => Self::Paused, PlaybackState::Buffering => Self::Buffering }
    }
}

#[derive(Debug)]
pub enum TrayCommand { Quit, Authenticate }

enum MenuTextUpdate {
    Status(String),
    Auth(String),
}

pub struct TrayHandle {
    #[cfg(not(target_os = "linux"))]
    _tray: tray_icon::TrayIcon,
    #[cfg(not(target_os = "linux"))]
    status_item: MenuItem,
    #[cfg(not(target_os = "linux"))]
    auth_item: MenuItem,
    #[cfg(target_os = "linux")]
    update_tx: std::sync::mpsc::Sender<MenuTextUpdate>,
}

impl TrayHandle {
    pub fn set_status_text(&self, text: &str) {
        #[cfg(not(target_os = "linux"))]
        self.status_item.set_text(text);
        #[cfg(target_os = "linux")]
        let _ = self.update_tx.send(MenuTextUpdate::Status(text.to_string()));
    }

    pub fn set_auth_text(&self, text: &str) {
        #[cfg(not(target_os = "linux"))]
        self.auth_item.set_text(text);
        #[cfg(target_os = "linux")]
        let _ = self.update_tx.send(MenuTextUpdate::Auth(text.to_string()));
    }
}

fn build_tray(tx: UnboundedSender<TrayCommand>, authenticated: bool) -> Option<(MenuItem, MenuItem, tray_icon::TrayIcon)> {
    let menu = Menu::new();
    let status_item = MenuItem::new(if authenticated { TrayStatus::Idle } else { TrayStatus::NotAuthenticated }.as_str(), false, None);
    let auth_item = MenuItem::new(if authenticated { "Reauthenticate" } else { "Authenticate with Plex" }, true, None);
    let quit_item = MenuItem::new("Quit", true, None);

    menu.append(&status_item).ok()?;
    menu.append(&PredefinedMenuItem::separator()).ok()?;
    menu.append(&auth_item).ok()?;
    menu.append(&quit_item).ok()?;

    let img = image::load_from_memory(include_bytes!("../assets/icon.ico")).ok()?;
    let (w, h) = img.dimensions();
    let icon = Icon::from_rgba(img.to_rgba8().into_raw(), w, h).ok()?;

    let tray = TrayIconBuilder::new().with_menu(Box::new(menu)).with_tooltip("Presence for Plex").with_icon(icon).build().ok()?;

    let (auth_id, quit_id) = (auth_item.id().clone(), quit_item.id().clone());

    std::thread::spawn(move || {
        let recv = MenuEvent::receiver();
        loop {
            match recv.recv_timeout(Duration::from_millis(100)) {
                Ok(e) if e.id == quit_id => { let _ = tx.send(TrayCommand::Quit); break; }
                Ok(e) if e.id == auth_id => { let _ = tx.send(TrayCommand::Authenticate); }
                Ok(_) => {}
                Err(RecvTimeoutError::Timeout) if tx.is_closed() => break,
                Err(RecvTimeoutError::Disconnected) => break,
                _ => {}
            }
        }
    });

    Some((status_item, auth_item, tray))
}

#[cfg(target_os = "linux")]
pub fn setup(tx: UnboundedSender<TrayCommand>, authenticated: bool) -> Option<TrayHandle> {
    let (ready_tx, ready_rx) = std::sync::mpsc::sync_channel(1);
    let (update_tx, update_rx) = std::sync::mpsc::channel::<MenuTextUpdate>();
    std::thread::spawn(move || {
        gtk::init().expect("Failed to initialize GTK");
        let result = build_tray(tx, authenticated);
        if result.is_none() {
            ready_tx.send(false).ok();
            return;
        }
        let (status_item, auth_item, _tray) = result.unwrap();
        ready_tx.send(true).ok();

        // Process text updates from main thread via glib idle callbacks
        let status = status_item.clone();
        let auth = auth_item.clone();
        gtk::glib::timeout_add_local(Duration::from_millis(50), move || {
            while let Ok(update) = update_rx.try_recv() {
                match update {
                    MenuTextUpdate::Status(text) => status.set_text(&text),
                    MenuTextUpdate::Auth(text) => auth.set_text(&text),
                }
            }
            gtk::glib::ControlFlow::Continue
        });

        gtk::main();
    });
    if !ready_rx.recv().ok()? { return None; }
    Some(TrayHandle { update_tx })
}

#[cfg(not(target_os = "linux"))]
pub fn setup(tx: UnboundedSender<TrayCommand>, authenticated: bool) -> Option<TrayHandle> {
    let (status_item, auth_item, tray) = build_tray(tx, authenticated)?;
    Some(TrayHandle { _tray: tray, status_item, auth_item })
}
