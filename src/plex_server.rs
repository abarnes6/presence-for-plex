use futures::StreamExt;
use log::{debug, info};
use std::sync::Arc;
use reqwest::Client;
use reqwest_eventsource::{Event, EventSource};
use serde::Deserialize;
use std::time::{Duration, Instant};
use tokio::sync::{mpsc, RwLock};

use crate::metadata::MetadataEnricher;
use crate::plex_account::{ServerConnection, APP_NAME};

pub const SSE_RECONNECT_DELAY_SECS: u64 = 5;
const SESSION_TIMEOUT_SECS: u64 = 10;
const SEEK_THRESHOLD_MS: u64 = 30_000;

#[derive(Debug, Clone)]
pub enum MediaUpdate {
    Playing(Box<MediaInfo>),
    Stopped,
}

#[derive(Debug, Clone)]
pub struct MediaInfo {
    pub title: String,
    pub media_type: MediaType,
    pub show_name: Option<String>,
    pub season: Option<u32>,
    pub episode: Option<u32>,
    pub artist: Option<String>,
    pub album: Option<String>,
    pub year: Option<u32>,
    pub genres: Vec<String>,
    pub duration_ms: u64,
    pub view_offset_ms: u64,
    pub state: PlaybackState,
    pub imdb_id: Option<String>,
    pub tmdb_id: Option<String>,
    pub mal_id: Option<String>,
    pub art_url: Option<String>,
    pub rating_key: Option<String>,
    grandparent_key: Option<String>,
    key: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum MediaType {
    Movie,
    Episode,
    Track,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PlaybackState {
    Playing,
    Paused,
    Buffering,
    Stopped,
}

pub struct PlexServer {
    name: String,
    connections: Vec<ServerConnection>,
    access_token: String,
    username: Option<String>,
    client: Client,
}

struct PlaybackTracker {
    cached_info: Option<MediaInfo>,
    source_server: Option<String>,
    last_update: Option<Instant>,
}

impl PlaybackTracker {
    fn new() -> Self {
        Self {
            cached_info: None,
            source_server: None,
            last_update: None,
        }
    }

    fn has_media(&self) -> bool {
        self.cached_info.is_some()
    }

    fn is_same_media(&self, rating_key: &str) -> bool {
        self.cached_info
            .as_ref()
            .and_then(|info| info.rating_key.as_deref())
            == Some(rating_key)
    }

    fn is_duplicate(&self, state: PlaybackState, view_offset: u64) -> bool {
        let (Some(ref info), Some(last_update)) = (&self.cached_info, self.last_update) else {
            return false;
        };

        if info.state != state {
            return false;
        }

        // Calculate expected offset based on elapsed time
        let elapsed_ms = last_update.elapsed().as_millis() as u64;
        let expected_offset = info.view_offset_ms.saturating_add(elapsed_ms);
        let diff = expected_offset.abs_diff(view_offset);
        diff <= SEEK_THRESHOLD_MS
    }

    fn update_playback(&mut self, state: PlaybackState, view_offset: u64) {
        if let Some(ref mut info) = self.cached_info {
            info.state = state;
            info.view_offset_ms = view_offset;
            self.last_update = Some(Instant::now());
        }
    }

    fn set_media(&mut self, info: MediaInfo, server_uri: &str) {
        self.cached_info = Some(info);
        self.source_server = Some(server_uri.to_string());
        self.last_update = Some(Instant::now());
    }

    fn get_info(&self) -> Option<&MediaInfo> {
        self.cached_info.as_ref()
    }

    fn clear_if_from_server(&mut self, server_uri: &str) -> bool {
        if self.source_server.as_deref() == Some(server_uri) {
            self.cached_info = None;
            self.source_server = None;
            self.last_update = None;
            true
        } else {
            false
        }
    }

    fn clear(&mut self) {
        self.cached_info = None;
        self.source_server = None;
        self.last_update = None;
    }
}

impl PlexServer {
    pub fn new(
        name: String,
        connections: Vec<ServerConnection>,
        access_token: String,
        username: Option<String>,
    ) -> Self {
        debug!(
            "Creating PlexServer: name={}, connections={}, username={:?}",
            name,
            connections.len(),
            username
        );
        let client = Client::builder()
            .user_agent("PresenceForPlex/1.0")
            .build()
            .expect("Failed to create HTTP client");

        Self {
            name,
            connections,
            access_token,
            username,
            client,
        }
    }

    pub async fn start_monitoring(
        self,
        tx: mpsc::UnboundedSender<MediaUpdate>,
        enricher: Arc<MetadataEnricher>,
    ) {
        info!("Monitoring server: {}", self.name);

        loop {
            for conn in &self.connections {
                debug!("Trying connection: {}", conn.uri);
                match self.try_connection(&conn.uri, &tx, &enricher).await {
                    Ok(()) => {
                        info!("Connection to {} closed cleanly", conn.uri);
                    }
                    Err(e) => {
                        debug!("Connection to {} failed: {}", conn.uri, e);
                    }
                }
            }

            debug!(
                "All connections to {} failed, retrying in {}s",
                self.name, SSE_RECONNECT_DELAY_SECS
            );
            tokio::time::sleep(Duration::from_secs(SSE_RECONNECT_DELAY_SECS)).await;
        }
    }

    async fn try_connection(
        &self,
        server_uri: &str,
        tx: &mpsc::UnboundedSender<MediaUpdate>,
        enricher: &Arc<MetadataEnricher>,
    ) -> Result<(), String> {
        let sse_url = format!("{}/:/eventsource/notifications?filters=playing", server_uri);

        let sse_client = Client::builder()
            .user_agent("PresenceForPlex/1.0")
            .connect_timeout(Duration::from_secs(5))
            .pool_max_idle_per_host(0)
            .build()
            .map_err(|e| format!("Failed to create client: {}", e))?;

        let request = sse_client
            .get(&sse_url)
            .header("Accept", "text/event-stream")
            .header("X-Plex-Token", &self.access_token)
            .header("X-Plex-Client-Identifier", APP_NAME);

        let mut es = EventSource::new(request).map_err(|e| format!("Failed to create EventSource: {}", e))?;
        let mut connection_opened = false;
        let tracker = RwLock::new(PlaybackTracker::new());

        while let Some(event) = es.next().await {
            match event {
                Ok(Event::Open) => {
                    connection_opened = true;
                    info!("SSE connection opened: {}", server_uri);
                }
                Ok(Event::Message(msg)) => {
                    self.handle_sse_message(&msg.data, server_uri, tx, enricher, &tracker)
                        .await;
                }
                Err(e) => {
                    if connection_opened {
                        let mut tracker_guard = tracker.write().await;
                        if tracker_guard.clear_if_from_server(server_uri) {
                            let _ = tx.send(MediaUpdate::Stopped);
                        }
                    }
                    return Err(format!("SSE error: {:?}", e));
                }
            }
        }

        Ok(())
    }

    async fn handle_sse_message(
        &self,
        data: &str,
        server_uri: &str,
        tx: &mpsc::UnboundedSender<MediaUpdate>,
        enricher: &Arc<MetadataEnricher>,
        tracker: &RwLock<PlaybackTracker>,
    ) {
        debug!("SSE event: {}", data);

        let notification = match serde_json::from_str::<SseNotification>(data) {
            Ok(n) => n,
            Err(e) => {
                debug!("Failed to parse SSE notification: {}", e);
                return;
            }
        };

        let Some(playing) = notification.play_session_state else {
            return;
        };

        if playing.state == "stopped" {
            let mut tracker_guard = tracker.write().await;
            if tracker_guard.has_media() {
                tracker_guard.clear();
                let _ = tx.send(MediaUpdate::Stopped);
            }
            return;
        }

        let state = match playing.state.as_str() {
            "playing" => PlaybackState::Playing,
            "paused" => PlaybackState::Paused,
            "buffering" => PlaybackState::Buffering,
            _ => return,
        };
        let view_offset = playing.view_offset.unwrap_or(0);

        {
            let mut tracker_guard = tracker.write().await;

            if tracker_guard.is_same_media(&playing.rating_key) {
                if tracker_guard.is_duplicate(state, view_offset) {
                    debug!("Skipping duplicate update");
                    return;
                }

                tracker_guard.update_playback(state, view_offset);
                if let Some(info) = tracker_guard.get_info() {
                    debug!("Playback update: {} ({:?})", info.title, state);
                    let _ = tx.send(MediaUpdate::Playing(Box::new(info.clone())));
                }
                return;
            }
        }

        let Some(mut info) = self.fetch_session(server_uri).await else {
            return;
        };

        info!("Now playing: {} ({:?})", info.title, info.state);

        enricher.enrich(&mut info).await;

        let mut tracker_guard = tracker.write().await;
        tracker_guard.set_media(info.clone(), server_uri);
        let _ = tx.send(MediaUpdate::Playing(Box::new(info)));
    }

    async fn fetch_session(&self, server_uri: &str) -> Option<MediaInfo> {
        debug!("Fetching sessions from {}", server_uri);
        let start = std::time::Instant::now();

        let resp = match self.client
            .get(format!("{}/status/sessions", server_uri))
            .header("Accept", "application/json")
            .header("X-Plex-Token", &self.access_token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .timeout(Duration::from_secs(SESSION_TIMEOUT_SECS))
            .send()
            .await
        {
            Ok(r) => r,
            Err(e) => {
                debug!("Session fetch failed after {:?}: {}", start.elapsed(), e);
                return None;
            }
        };

        debug!("Session response received in {:?}", start.elapsed());

        if !resp.status().is_success() {
            debug!("Session fetch failed with status: {}", resp.status());
            return None;
        }

        let sessions: SessionsResponse = match resp.json().await {
            Ok(s) => s,
            Err(e) => {
                debug!("Failed to parse sessions: {}", e);
                return None;
            }
        };

        debug!(
            "Got {} sessions in {:?}, filtering for user: {:?}",
            sessions.media_container.metadata.len(),
            start.elapsed(),
            self.username
        );

        let meta = sessions
            .media_container
            .metadata
            .into_iter()
            .find(|m| match (self.username.as_deref(), &m.user) {
                (Some(target), Some(user)) => user.title == target,
                (Some(_), None) => false,
                (None, _) => true,
            })?;

        debug!("Found matching session: {}", meta.title);
        let mut info = Self::parse_session(meta)?;

        self.enrich_external_ids(server_uri, &mut info).await;

        debug!("Session fetch complete in {:?}", start.elapsed());
        Some(info)
    }

    fn get_metadata_key(info: &MediaInfo) -> Option<&str> {
        match info.media_type {
            MediaType::Episode => info.grandparent_key.as_deref(),
            MediaType::Movie => info.key.as_deref(),
            _ => None,
        }
    }

    async fn enrich_external_ids(&self, server_uri: &str, info: &mut MediaInfo) {
        let Some(key) = Self::get_metadata_key(info) else {
            debug!("No metadata key for {:?}, skipping external ID enrichment", info.media_type);
            return;
        };

        debug!("Fetching external IDs from metadata key: {}", key);
        let Some(meta) = self.fetch_item_metadata(server_uri, key).await else {
            debug!("Failed to fetch item metadata");
            return;
        };

        for guid in &meta.guids {
            if let Some(id) = guid.id.strip_prefix("imdb://") {
                debug!("Found IMDB ID: {}", id);
                info.imdb_id = Some(id.to_string());
            } else if let Some(id) = guid.id.strip_prefix("tmdb://") {
                debug!("Found TMDB ID: {}", id);
                info.tmdb_id = Some(id.to_string());
            }
        }

        if info.media_type == MediaType::Episode {
            info.genres = meta.genres.into_iter().map(|g| g.tag).collect();
            debug!("Enriched genres: {:?}", info.genres);
        }
    }

    fn parse_session(meta: SessionMetadata) -> Option<MediaInfo> {
        let state = match meta.player.as_ref().map(|p| p.state.as_str()) {
            Some("playing") => PlaybackState::Playing,
            Some("paused") => PlaybackState::Paused,
            Some("buffering") => PlaybackState::Buffering,
            _ => PlaybackState::Stopped,
        };

        let media_type = match meta.media_type.as_str() {
            "movie" => MediaType::Movie,
            "episode" => MediaType::Episode,
            "track" => MediaType::Track,
            _ => return None,
        };

        let (imdb_id, tmdb_id) = Self::extract_external_ids(&meta.guids);

        Some(MediaInfo {
            title: meta.title,
            media_type,
            show_name: meta.grandparent_title.clone(),
            season: meta.parent_index,
            episode: meta.index,
            artist: meta.grandparent_title,
            album: meta.parent_title,
            year: meta.year,
            genres: meta.genre.into_iter().map(|g| g.tag).collect(),
            duration_ms: meta.duration.unwrap_or(0),
            view_offset_ms: meta.view_offset.unwrap_or(0),
            state,
            imdb_id,
            tmdb_id,
            mal_id: None,
            art_url: None,
            rating_key: meta.rating_key,
            grandparent_key: meta.grandparent_key,
            key: meta.key,
        })
    }

    fn extract_external_ids(guids: &[GuidTag]) -> (Option<String>, Option<String>) {
        let mut imdb_id = None;
        let mut tmdb_id = None;

        for guid in guids {
            if let Some(id) = guid.id.strip_prefix("imdb://") {
                imdb_id = Some(id.to_string());
            } else if let Some(id) = guid.id.strip_prefix("tmdb://") {
                tmdb_id = Some(id.to_string());
            }
        }

        (imdb_id, tmdb_id)
    }

    async fn fetch_item_metadata(&self, server_uri: &str, key: &str) -> Option<ItemMetadata> {
        let resp = self.client
            .get(format!("{}{}", server_uri, key))
            .header("Accept", "application/json")
            .header("X-Plex-Token", &self.access_token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .timeout(Duration::from_secs(SESSION_TIMEOUT_SECS))
            .send()
            .await
            .ok()?;

        let meta: MetadataResponse = resp.json().await.ok()?;
        meta.media_container.metadata.into_iter().next()
    }
}

// SSE notification types
#[derive(Debug, Deserialize)]
struct SseNotification {
    #[serde(rename = "PlaySessionStateNotification")]
    play_session_state: Option<PlaySessionState>,
}

#[derive(Debug, Deserialize)]
struct PlaySessionState {
    state: String,
    #[serde(rename = "ratingKey")]
    rating_key: String,
    #[serde(rename = "viewOffset")]
    view_offset: Option<u64>,
}

// Plex API response types
#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct SessionsResponse {
    media_container: MediaContainer,
}

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct MediaContainer {
    #[serde(default)]
    metadata: Vec<SessionMetadata>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct SessionMetadata {
    title: String,
    #[serde(rename = "type")]
    media_type: String,
    duration: Option<u64>,
    view_offset: Option<u64>,
    year: Option<u32>,
    grandparent_title: Option<String>,
    parent_index: Option<u32>,
    index: Option<u32>,
    parent_title: Option<String>,
    #[serde(rename = "Genre", default)]
    genre: Vec<GenreTag>,
    #[serde(rename = "Player")]
    player: Option<PlayerInfo>,
    #[serde(rename = "User")]
    user: Option<UserInfo>,
    #[serde(rename = "Guid", default)]
    guids: Vec<GuidTag>,
    #[serde(rename = "ratingKey")]
    rating_key: Option<String>,
    #[serde(rename = "grandparentKey")]
    grandparent_key: Option<String>,
    key: Option<String>,
}

#[derive(Deserialize)]
struct UserInfo {
    title: String,
}

#[derive(Deserialize)]
struct GuidTag {
    id: String,
}

#[derive(Deserialize)]
struct GenreTag {
    tag: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct PlayerInfo {
    state: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct MetadataResponse {
    media_container: MetadataContainer,
}

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct MetadataContainer {
    #[serde(default)]
    metadata: Vec<ItemMetadata>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct ItemMetadata {
    #[serde(rename = "Guid", default)]
    guids: Vec<GuidTag>,
    #[serde(rename = "Genre", default)]
    genres: Vec<GenreTag>,
}
