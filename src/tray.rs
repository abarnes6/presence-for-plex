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

pub struct TrayHandle {
    _tray: tray_icon::TrayIcon,
    pub status_item: MenuItem,
    pub auth_item: MenuItem,
}

pub fn setup(tx: UnboundedSender<TrayCommand>, authenticated: bool) -> Option<TrayHandle> {
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
    let (status_clone, auth_clone) = (status_item.clone(), auth_item.clone());

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

    Some(TrayHandle { _tray: tray, status_item: status_clone, auth_item: auth_clone })
}
