use discord_rich_presence::{activity, DiscordIpc, DiscordIpcClient};
use log::{debug, error, info};
use std::time::{SystemTime, UNIX_EPOCH};

use crate::plex_server::PlaybackState;

const MAX_BUTTONS: usize = 2;
const PAUSED_TIMESTAMP_OFFSET_SECS: i64 = 9999 * 3600;

pub struct DiscordClient {
    client: DiscordIpcClient,
    connected: bool,
}

impl DiscordClient {
    pub fn new(client_id: &str) -> Self {
        debug!("Creating Discord client with ID: {}", client_id);
        Self {
            client: DiscordIpcClient::new(client_id),
            connected: false,
        }
    }

    pub fn connect(&mut self) -> bool {
        debug!("Attempting Discord connection (currently connected: {})", self.connected);
        if self.is_connected() {
            debug!("Already connected, disconnecting first");
            self.disconnect();
        }
        match self.client.connect() {
            Ok(_) => {
                info!("Connected to Discord");
                self.connected = true;
                true
            }
            Err(e) => {
                error!("Failed to connect to Discord: {}", e);
                false
            }
        }
    }

    pub fn disconnect(&mut self) {
        if self.connected {
            debug!("Closing Discord connection");
            let _ = self.client.close();
            self.connected = false;
            info!("Disconnected from Discord");
        }
    }

    pub fn is_connected(&self) -> bool {
        self.connected
    }

    pub fn update(&mut self, presence: &Presence) {
        debug!(
            "Updating presence: details={:?}, state={:?}, playback={:?}, activity={:?}",
            presence.details, presence.state, presence.playback_state, presence.activity_type
        );

        if !self.connected {
            debug!("Not connected, skipping update");
            return;
        }

        let now = current_unix_timestamp();
        debug!("Current timestamp: {}", now);

        let status_display_type = match presence.activity_type {
            ActivityType::Listening => activity::StatusDisplayType::State,
            ActivityType::Watching => activity::StatusDisplayType::Details,
        };

        let mut builder = activity::Activity::new()
            .activity_type(presence.activity_type.into())
            .status_display_type(status_display_type)
            .details(&presence.details)
            .state(&presence.state);

        if presence.show_timestamps {
            debug!("Applying timestamps (progress: {}ms, duration: {}ms)", presence.progress_ms, presence.duration_ms);
            builder = apply_timestamps(builder, presence, now);
        }

        builder = builder.assets(build_assets(presence));

        if !presence.buttons.is_empty() {
            debug!("Adding {} button(s)", presence.buttons.len());
            builder = builder.buttons(build_buttons(&presence.buttons));
        }

        if let Err(e) = self.client.set_activity(builder) {
            error!("Failed to update presence: {}", e);
            self.disconnect();
        } else {
            debug!("Presence updated successfully");
        }
    }

    pub fn clear(&mut self) {
        if self.connected {
            debug!("Clearing activity");
            let _ = self.client.clear_activity();
        }
    }
}

fn current_unix_timestamp() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

fn apply_timestamps<'a>(
    builder: activity::Activity<'a>,
    presence: &Presence,
    now: i64,
) -> activity::Activity<'a> {
    match presence.playback_state {
        PlaybackState::Playing => {
            let progress_secs = (presence.progress_ms / 1000) as i64;
            let remaining_secs = (presence.duration_ms.saturating_sub(presence.progress_ms) / 1000) as i64;
            debug!(
                "Playing timestamps: start={}, end={} (progress: {}s, remaining: {}s)",
                now - progress_secs, now + remaining_secs, progress_secs, remaining_secs
            );
            builder.timestamps(
                activity::Timestamps::new()
                    .start(now - progress_secs)
                    .end(now + remaining_secs),
            )
        }
        PlaybackState::Paused | PlaybackState::Buffering => {
            let far_future = now + PAUSED_TIMESTAMP_OFFSET_SECS;
            let duration_secs = (presence.duration_ms / 1000) as i64;
            debug!(
                "Paused/buffering timestamps: start={}, end={} (far future offset)",
                far_future, far_future + duration_secs
            );
            builder.timestamps(
                activity::Timestamps::new()
                    .start(far_future)
                    .end(far_future + duration_secs),
            )
        }
        PlaybackState::Stopped => {
            debug!("Stopped state, no timestamps applied");
            builder
        }
    }
}

fn build_assets(presence: &Presence) -> activity::Assets<'_> {
    let mut assets = activity::Assets::new();

    if let Some(ref url) = presence.large_image {
        debug!("Setting large image: {}", url);
        assets = assets.large_image(url).large_text(&presence.large_image_text);
    }

    if presence.playback_state == PlaybackState::Paused {
        debug!("Adding paused indicator");
        assets = assets.small_image("paused").small_text("Paused");
    }

    assets
}

fn build_buttons(buttons: &[Button]) -> Vec<activity::Button<'_>> {
    buttons
        .iter()
        .take(MAX_BUTTONS)
        .map(|b| activity::Button::new(&b.label, &b.url))
        .collect()
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
pub enum ActivityType {
    Watching,
    Listening,
}

impl From<ActivityType> for activity::ActivityType {
    fn from(activity_type: ActivityType) -> Self {
        match activity_type {
            ActivityType::Watching => activity::ActivityType::Watching,
            ActivityType::Listening => activity::ActivityType::Listening,
        }
    }
}

#[derive(Debug, Clone)]
pub struct Button {
    pub label: String,
    pub url: String,
}
