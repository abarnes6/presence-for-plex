use discord_rich_presence::{DiscordIpc, DiscordIpcClient, activity};
use log::{debug, error, info, warn};
use std::sync::mpsc;
use std::thread;
use std::time::{SystemTime, UNIX_EPOCH};

use crate::media::PlaybackState;
use crate::presence::{ActivityType, Presence};

// Timestamps this far out render as a frozen clock in Discord
const PAUSED_OFFSET: i64 = 9999 * 3600;

enum Command {
    Update(Presence),
    Clear,
    Shutdown(tokio::sync::oneshot::Sender<()>),
}

/// The IPC client is synchronous and timeout-free, so it lives on a dedicated
/// thread; the async side only posts commands, and a stalled Discord cannot
/// block the runtime or shutdown.
#[derive(Clone)]
pub struct DiscordHandle {
    tx: mpsc::Sender<Command>,
}

impl DiscordHandle {
    pub fn update(&self, presence: Presence) {
        let _ = self.tx.send(Command::Update(presence));
    }

    pub fn clear(&self) {
        let _ = self.tx.send(Command::Clear);
    }

    /// The returned receiver resolves once the actor has disconnected and
    /// exited (immediately if already gone). Bound the wait: a hung Discord
    /// pipe can block the actor indefinitely — the detached thread dies with
    /// the process.
    pub fn shutdown(&self) -> tokio::sync::oneshot::Receiver<()> {
        let (ack_tx, ack_rx) = tokio::sync::oneshot::channel();
        let _ = self.tx.send(Command::Shutdown(ack_tx));
        ack_rx
    }
}

pub fn spawn(client_id: &str) -> DiscordHandle {
    let (tx, rx) = mpsc::channel();
    let client_id = client_id.to_string();
    thread::spawn(move || actor(&client_id, &rx));
    DiscordHandle { tx }
}

struct Connection {
    client: DiscordIpcClient,
    connected: bool,
    retrying_quietly: bool,
}

impl Connection {
    fn new(client_id: &str) -> Self {
        Self {
            client: DiscordIpcClient::new(client_id),
            connected: false,
            retrying_quietly: false,
        }
    }

    fn ensure_connected(&mut self) -> bool {
        if self.connected {
            return true;
        }
        match self.client.connect() {
            Ok(_) => {
                info!("Connected to Discord");
                self.connected = true;
                self.retrying_quietly = false;
            }
            Err(e) => {
                if self.retrying_quietly {
                    debug!("Discord connect failed: {}", e);
                } else {
                    warn!("Discord connect failed (will keep retrying quietly): {}", e);
                    self.retrying_quietly = true;
                }
            }
        }
        self.connected
    }

    fn disconnect(&mut self) {
        if self.connected {
            let _ = self.client.close();
            self.connected = false;
        }
    }
}

fn actor(client_id: &str, rx: &mpsc::Receiver<Command>) {
    let mut conn = Connection::new(client_id);
    conn.ensure_connected();

    while let Ok(cmd) = rx.recv() {
        match cmd {
            Command::Update(p) => {
                if !conn.ensure_connected() {
                    continue;
                }
                let now = SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .map(|d| d.as_secs() as i64)
                    .unwrap_or(0);
                if let Err(e) = conn.client.set_activity(build_activity(&p, now)) {
                    error!("Presence update failed: {}", e);
                    conn.disconnect();
                }
            }
            Command::Clear => {
                if conn.connected && conn.client.clear_activity().is_err() {
                    conn.disconnect();
                }
            }
            Command::Shutdown(ack) => {
                conn.disconnect();
                let _ = ack.send(());
                return;
            }
        }
    }
    conn.disconnect();
}

fn secs(ms: u64) -> i64 {
    (ms / 1000) as i64
}

fn build_activity(p: &Presence, now: i64) -> activity::Activity<'_> {
    let display = match p.activity_type {
        ActivityType::Listening => activity::StatusDisplayType::State,
        ActivityType::Watching => activity::StatusDisplayType::Details,
    };
    let activity_type = match p.activity_type {
        ActivityType::Listening => activity::ActivityType::Listening,
        ActivityType::Watching => activity::ActivityType::Watching,
    };

    let mut b = activity::Activity::new()
        .activity_type(activity_type)
        .status_display_type(display);

    // Discord rejects strings under 2 chars
    if p.details.chars().count() >= 2 {
        b = b.details(&p.details);
    }
    if p.state.chars().count() >= 2 {
        b = b.state(&p.state);
    }

    if p.show_timestamps {
        b = match p.playback_state {
            PlaybackState::Playing => {
                let played = secs(p.progress_ms);
                let remaining = secs(p.duration_ms.saturating_sub(p.progress_ms));
                b.timestamps(
                    activity::Timestamps::new()
                        .start(now - played)
                        .end(now + remaining),
                )
            }
            PlaybackState::Paused | PlaybackState::Buffering => b.timestamps(
                activity::Timestamps::new()
                    .start(now + PAUSED_OFFSET)
                    .end(now + PAUSED_OFFSET + secs(p.duration_ms)),
            ),
        };
    }

    let mut assets = activity::Assets::new()
        .large_image(&p.large_image)
        .large_text(&p.large_image_text);
    if p.playback_state == PlaybackState::Paused {
        assets = assets.small_image("paused").small_text("Paused");
    }
    b = b.assets(assets);

    if !p.buttons.is_empty() {
        b = b.buttons(
            p.buttons
                .iter()
                .take(2)
                .map(|btn| activity::Button::new(&btn.label, &btn.url))
                .collect(),
        );
    }

    b
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::presence::Button;

    fn presence() -> Presence {
        Presence {
            details: "Some Movie".into(),
            state: "Drama".into(),
            large_image: "https://img.example/x.jpg".into(),
            large_image_text: "Some Movie".into(),
            progress_ms: 90_000,
            duration_ms: 600_000,
            show_timestamps: true,
            activity_type: ActivityType::Watching,
            playback_state: PlaybackState::Playing,
            buttons: Vec::new(),
        }
    }

    fn to_json(p: &Presence, now: i64) -> serde_json::Value {
        serde_json::to_value(build_activity(p, now)).unwrap()
    }

    #[test]
    fn playing_timestamps_span_the_item() {
        let v = to_json(&presence(), 1_000_000);
        assert_eq!(v["timestamps"]["start"], 1_000_000 - 90);
        assert_eq!(v["timestamps"]["end"], 1_000_000 + 510);
    }

    #[test]
    fn paused_timestamps_freeze_the_clock_and_show_the_pause_badge() {
        let mut p = presence();
        p.playback_state = PlaybackState::Paused;
        let v = to_json(&p, 1_000_000);
        assert_eq!(v["timestamps"]["start"], 1_000_000 + PAUSED_OFFSET);
        assert_eq!(v["timestamps"]["end"], 1_000_000 + PAUSED_OFFSET + 600);
        assert_eq!(v["assets"]["small_image"], "paused");
    }

    #[test]
    fn timestamps_omitted_when_progress_disabled() {
        let mut p = presence();
        p.show_timestamps = false;
        assert!(to_json(&p, 0).get("timestamps").is_none());
    }

    #[test]
    fn strings_under_two_chars_are_omitted() {
        let mut p = presence();
        p.details = "X".into();
        p.state = "".into();
        let v = to_json(&p, 0);
        assert!(v.get("details").is_none());
        assert!(v.get("state").is_none());
    }

    #[test]
    fn buttons_are_capped_at_two() {
        let mut p = presence();
        p.buttons = (1..=3)
            .map(|i| Button {
                label: format!("B{i}"),
                url: format!("https://example.com/{i}"),
            })
            .collect();
        let v = to_json(&p, 0);
        assert_eq!(v["buttons"].as_array().unwrap().len(), 2);
        assert_eq!(v["buttons"][0]["label"], "B1");
    }

    #[test]
    fn listening_uses_listening_type_and_state_display() {
        let mut p = presence();
        p.activity_type = ActivityType::Listening;
        let v = to_json(&p, 0);
        assert_eq!(v["type"], 2);
        assert_eq!(v["status_display_type"], 1);
        let v = to_json(&presence(), 0);
        assert_eq!(v["type"], 3);
        assert_eq!(v["status_display_type"], 2);
    }
}
