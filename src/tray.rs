use crossbeam_channel::RecvTimeoutError;
use image::GenericImageView;
use log::warn;
use std::time::Duration;
use tokio::sync::mpsc::UnboundedSender;
use tray_icon::{
    menu::{Menu, MenuEvent, MenuItem, PredefinedMenuItem},
    Icon, TrayIcon, TrayIconBuilder,
};

use crate::{TrayCommand, TrayStatus};

const ICON_BYTES: &[u8] = include_bytes!("../assets/icon.ico");
const MENU_POLL_TIMEOUT: Duration = Duration::from_millis(100);

pub struct TrayHandle {
    _tray: TrayIcon,
    pub status_item: MenuItem,
    pub auth_item: MenuItem,
}

pub fn setup(tx: UnboundedSender<TrayCommand>, is_authenticated: bool) -> Option<TrayHandle> {
    let menu = Menu::new();
    let initial_status = if is_authenticated { TrayStatus::Idle } else { TrayStatus::NotAuthenticated };
    let status_item = MenuItem::new(initial_status.as_str(), false, None);
    let auth_text = if is_authenticated { "Reauthenticate" } else { "Authenticate with Plex" };
    let auth_item = MenuItem::new(auth_text, true, None);
    let quit_item = MenuItem::new("Quit", true, None);
    menu.append(&status_item).ok()?;
    menu.append(&PredefinedMenuItem::separator()).ok()?;
    menu.append(&auth_item).ok()?;
    menu.append(&quit_item).ok()?;

    let img = image::load_from_memory(ICON_BYTES).ok()?;
    let rgba = img.to_rgba8();
    let (w, h) = img.dimensions();
    let icon = Icon::from_rgba(rgba.into_raw(), w, h).ok()?;

    let tray = TrayIconBuilder::new()
        .with_menu(Box::new(menu))
        .with_tooltip("Presence for Plex")
        .with_icon(icon)
        .build()
        .ok()?;

    let auth_id = auth_item.id().clone();
    let quit_id = quit_item.id().clone();

    let status_item_clone = status_item.clone();
    let auth_item_clone = auth_item.clone();

    std::thread::spawn(move || {
        let receiver = MenuEvent::receiver();

        loop {
            match receiver.recv_timeout(MENU_POLL_TIMEOUT) {
                Ok(event) => {
                    if event.id == quit_id {
                        let _ = tx.send(TrayCommand::Quit);
                        break;
                    } else if event.id == auth_id {
                        let _ = tx.send(TrayCommand::Authenticate);
                    }
                }
                Err(RecvTimeoutError::Timeout) => {
                    if tx.is_closed() {
                        break;
                    }
                }
                Err(RecvTimeoutError::Disconnected) => {
                    warn!("Menu event channel disconnected");
                    break;
                }
            }
        }
    });

    Some(TrayHandle {
        _tray: tray,
        status_item: status_item_clone,
        auth_item: auth_item_clone,
    })
}
