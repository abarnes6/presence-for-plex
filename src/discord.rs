use discord_rich_presence::{activity, DiscordIpc, DiscordIpcClient};
use log::{error, info};
use std::time::{SystemTime, UNIX_EPOCH};

use crate::plex_server::PlaybackState;

const PAUSED_OFFSET: i64 = 9999 * 3600;

pub struct DiscordClient {
    client: DiscordIpcClient,
    connected: bool,
}

impl DiscordClient {
    pub fn new(client_id: &str) -> Self {
        Self { client: DiscordIpcClient::new(client_id), connected: false }
    }

    pub fn connect(&mut self) -> bool {
        if self.connected { self.disconnect(); }
        match self.client.connect() {
            Ok(_) => { info!("Connected to Discord"); self.connected = true; true }
            Err(e) => { error!("Discord connect failed: {}", e); false }
        }
    }

    pub fn disconnect(&mut self) {
        if self.connected { let _ = self.client.close(); self.connected = false; }
    }

    pub fn is_connected(&self) -> bool { self.connected }

    pub fn update(&mut self, p: &Presence) {
        if !self.connected { return; }

        let now = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs() as i64).unwrap_or(0);
        let display = match p.activity_type { ActivityType::Listening => activity::StatusDisplayType::State, _ => activity::StatusDisplayType::Details };

        let mut b = activity::Activity::new()
            .activity_type(p.activity_type.into())
            .status_display_type(display)
            .details(&p.details)
            .state(&p.state);

        if p.show_timestamps {
            b = match p.playback_state {
                PlaybackState::Playing => {
                    let prog = (p.progress_ms / 1000) as i64;
                    let rem = (p.duration_ms.saturating_sub(p.progress_ms) / 1000) as i64;
                    b.timestamps(activity::Timestamps::new().start(now - prog).end(now + rem))
                }
                PlaybackState::Paused | PlaybackState::Buffering => {
                    let dur = (p.duration_ms / 1000) as i64;
                    b.timestamps(activity::Timestamps::new().start(now + PAUSED_OFFSET).end(now + PAUSED_OFFSET + dur))
                }
            };
        }

        let mut assets = activity::Assets::new();
        if let Some(ref url) = p.large_image { assets = assets.large_image(url).large_text(&p.large_image_text); }
        if p.playback_state == PlaybackState::Paused { assets = assets.small_image("paused").small_text("Paused"); }
        b = b.assets(assets);

        if !p.buttons.is_empty() {
            b = b.buttons(p.buttons.iter().take(2).map(|btn| activity::Button::new(&btn.label, &btn.url)).collect());
        }

        if let Err(e) = self.client.set_activity(b) { error!("Presence update failed: {}", e); self.disconnect(); }
    }

    pub fn clear(&mut self) { if self.connected { let _ = self.client.clear_activity(); } }
}

#[derive(Debug, Clone)]
pub struct Presence {
    pub details: String,
    pub state: String,
    pub large_image: Option<String>,
    pub large_image_text: String,
    pub progress_ms: u64,
    pub duration_ms: u64,
    pub show_timestamps: bool,
    pub activity_type: ActivityType,
    pub playback_state: PlaybackState,
    pub buttons: Vec<Button>,
}

#[derive(Debug, Clone, Copy)]
pub enum ActivityType { Watching, Listening }

impl From<ActivityType> for activity::ActivityType {
    fn from(t: ActivityType) -> Self {
        match t { ActivityType::Watching => activity::ActivityType::Watching, ActivityType::Listening => activity::ActivityType::Listening }
    }
}

#[derive(Debug, Clone)]
pub struct Button { pub label: String, pub url: String }
