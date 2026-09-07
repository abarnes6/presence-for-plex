use crossbeam_channel::RecvTimeoutError;
use image::GenericImageView;
use std::time::Duration;
use tokio::sync::mpsc::UnboundedSender;
use tray_icon::{
    Icon, TrayIconBuilder,
    menu::{Menu, MenuEvent, MenuItem, PredefinedMenuItem},
};

use crate::media::AppStatus;

pub enum TrayCommand {
    Quit,
    Authenticate,
    HideTray,
}

#[cfg(target_os = "linux")]
enum TrayUpdate {
    Status(&'static str),
    Auth(&'static str),
    Hide,
}

pub struct TrayHandle {
    #[cfg(not(target_os = "linux"))]
    tray: tray_icon::TrayIcon,
    #[cfg(not(target_os = "linux"))]
    status_item: MenuItem,
    #[cfg(not(target_os = "linux"))]
    auth_item: MenuItem,
    #[cfg(target_os = "linux")]
    update_tx: std::sync::mpsc::Sender<TrayUpdate>,
}

fn status_label(status: AppStatus) -> &'static str {
    match status {
        AppStatus::Idle => "Status: Idle",
        AppStatus::Playing => "Status: Playing",
        AppStatus::Paused => "Status: Paused",
        AppStatus::Buffering => "Status: Buffering",
        AppStatus::NotAuthenticated => "Status: Not Authenticated",
    }
}

fn auth_label(authenticated: bool) -> &'static str {
    if authenticated {
        "Reauthenticate"
    } else {
        "Authenticate with Plex"
    }
}

impl TrayHandle {
    pub fn set_status(&self, status: AppStatus) {
        let text = status_label(status);
        #[cfg(not(target_os = "linux"))]
        self.status_item.set_text(text);
        #[cfg(target_os = "linux")]
        let _ = self.update_tx.send(TrayUpdate::Status(text));
    }

    pub fn set_authenticated(&self, authenticated: bool) {
        let text = auth_label(authenticated);
        #[cfg(not(target_os = "linux"))]
        self.auth_item.set_text(text);
        #[cfg(target_os = "linux")]
        let _ = self.update_tx.send(TrayUpdate::Auth(text));
    }

    /// Removes the icon for the rest of this run; `show_tray` in the config
    /// keeps it away on the next start.
    pub fn hide(&self) {
        #[cfg(not(target_os = "linux"))]
        if let Err(e) = self.tray.set_visible(false) {
            log::warn!("Could not hide tray icon: {}", e);
        }
        #[cfg(target_os = "linux")]
        let _ = self.update_tx.send(TrayUpdate::Hide);
    }
}

fn build_tray(
    tx: UnboundedSender<TrayCommand>,
    authenticated: bool,
) -> Option<(MenuItem, MenuItem, tray_icon::TrayIcon)> {
    let menu = Menu::new();
    let status_item = MenuItem::new(
        status_label(if authenticated {
            AppStatus::Idle
        } else {
            AppStatus::NotAuthenticated
        }),
        false,
        None,
    );
    let auth_item = MenuItem::new(auth_label(authenticated), true, None);
    let hide_item = MenuItem::new("Hide Tray Icon", true, None);
    let quit_item = MenuItem::new("Quit", true, None);

    menu.append(&status_item).ok()?;
    menu.append(&PredefinedMenuItem::separator()).ok()?;
    menu.append(&auth_item).ok()?;
    menu.append(&hide_item).ok()?;
    menu.append(&quit_item).ok()?;

    let img = image::load_from_memory(include_bytes!("../assets/icon.ico")).ok()?;
    let (w, h) = img.dimensions();
    let icon = Icon::from_rgba(img.to_rgba8().into_raw(), w, h).ok()?;

    let tray = TrayIconBuilder::new()
        .with_menu(Box::new(menu))
        .with_tooltip("Presence for Plex")
        .with_icon(icon)
        .build()
        .ok()?;

    let (auth_id, hide_id, quit_id) = (
        auth_item.id().clone(),
        hide_item.id().clone(),
        quit_item.id().clone(),
    );

    std::thread::spawn(move || {
        let recv = MenuEvent::receiver();
        loop {
            match recv.recv_timeout(Duration::from_millis(100)) {
                Ok(e) if e.id == quit_id => {
                    let _ = tx.send(TrayCommand::Quit);
                    break;
                }
                Ok(e) if e.id == auth_id => {
                    let _ = tx.send(TrayCommand::Authenticate);
                }
                Ok(e) if e.id == hide_id => {
                    let _ = tx.send(TrayCommand::HideTray);
                }
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
    let (update_tx, update_rx) = std::sync::mpsc::channel::<TrayUpdate>();
    std::thread::spawn(move || {
        if gtk::init().is_err() {
            log::error!("GTK init failed");
            ready_tx.send(false).ok();
            return;
        }
        let Some((status_item, auth_item, tray)) = build_tray(tx, authenticated) else {
            ready_tx.send(false).ok();
            return;
        };
        ready_tx.send(true).ok();

        // Menu items may only be touched from the GTK thread
        let status = status_item.clone();
        let auth = auth_item.clone();
        gtk::glib::timeout_add_local(Duration::from_millis(50), move || {
            while let Ok(update) = update_rx.try_recv() {
                match update {
                    TrayUpdate::Status(text) => status.set_text(text),
                    TrayUpdate::Auth(text) => auth.set_text(text),
                    TrayUpdate::Hide => {
                        if let Err(e) = tray.set_visible(false) {
                            log::warn!("Could not hide tray icon: {}", e);
                        }
                        return gtk::glib::ControlFlow::Break;
                    }
                }
            }
            gtk::glib::ControlFlow::Continue
        });

        gtk::main();
    });
    if !ready_rx.recv().ok()? {
        return None;
    }
    Some(TrayHandle { update_tx })
}

#[cfg(not(target_os = "linux"))]
pub fn setup(tx: UnboundedSender<TrayCommand>, authenticated: bool) -> Option<TrayHandle> {
    let (status_item, auth_item, tray) = build_tray(tx, authenticated)?;
    Some(TrayHandle {
        tray,
        status_item,
        auth_item,
    })
}
