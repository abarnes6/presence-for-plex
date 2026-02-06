use eventsource_client::{self as es, Client as EsClient, SSE};
use futures_util::TryStreamExt;
use log::info;
use reqwest::Client;
use serde::Deserialize;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::sync::{mpsc, RwLock};

use crate::metadata::MetadataEnricher;
use crate::plex_account::{ServerConnection, APP_NAME};

const SSE_RECONNECT_DELAY: Duration = Duration::from_secs(5);
const REQUEST_TIMEOUT: Duration = Duration::from_secs(10);
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
pub enum MediaType { Movie, Episode, Track }

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PlaybackState { Playing, Paused, Buffering }

pub struct PlexServer {
    name: String,
    connections: Vec<ServerConnection>,
    access_token: String,
    username: Option<String>,
    client: Client,
}

#[derive(Default)]
struct PlaybackTracker {
    info: Option<MediaInfo>,
    server: Option<String>,
    last_update: Option<Instant>,
}

impl PlaybackTracker {
    fn is_duplicate(&self, rating_key: &str, state: PlaybackState, offset: u64) -> bool {
        let Some(ref info) = self.info else { return false };
        if info.rating_key.as_deref() != Some(rating_key) || info.state != state {
            return false;
        }
        let Some(last) = self.last_update else { return false };
        let expected = info.view_offset_ms.saturating_add(last.elapsed().as_millis() as u64);
        expected.abs_diff(offset) <= SEEK_THRESHOLD_MS
    }

    fn update(&mut self, state: PlaybackState, offset: u64) {
        if let Some(ref mut info) = self.info {
            info.state = state;
            info.view_offset_ms = offset;
            self.last_update = Some(Instant::now());
        }
    }

    fn set(&mut self, info: MediaInfo, server: &str) {
        self.info = Some(info);
        self.server = Some(server.to_string());
        self.last_update = Some(Instant::now());
    }

    fn clear_if_server(&mut self, server: &str) -> bool {
        if self.server.as_deref() == Some(server) {
            *self = Self::default();
            true
        } else {
            false
        }
    }
}

impl PlexServer {
    pub fn new(name: String, connections: Vec<ServerConnection>, access_token: String, username: Option<String>) -> Self {
        Self {
            name,
            connections,
            access_token,
            username,
            client: Client::builder().user_agent("PresenceForPlex/1.0").build().expect("HTTP client"),
        }
    }

    pub async fn start_monitoring(self, tx: mpsc::UnboundedSender<MediaUpdate>, enricher: Arc<MetadataEnricher>) {
        info!("Monitoring server: {}", self.name);
        loop {
            for conn in &self.connections {
                let _ = self.try_connection(&conn.uri, &tx, &enricher).await;
            }
            tokio::time::sleep(SSE_RECONNECT_DELAY).await;
        }
    }

    async fn try_connection(&self, uri: &str, tx: &mpsc::UnboundedSender<MediaUpdate>, enricher: &Arc<MetadataEnricher>) -> Result<(), ()> {
        let url = format!("{}/:/eventsource/notifications?filters=playing", uri);
        let client = es::ClientBuilder::for_url(&url).map_err(|_| ())?
            .header("Accept", "text/event-stream").map_err(|_| ())?
            .header("X-Plex-Token", &self.access_token).map_err(|_| ())?
            .header("X-Plex-Client-Identifier", APP_NAME).map_err(|_| ())?
            .build();

        let mut stream = Box::pin(client.stream());
        let tracker = RwLock::new(PlaybackTracker::default());
        let mut opened = false;

        while let Ok(Some(event)) = stream.try_next().await {
            match event {
                SSE::Connected(_) => { opened = true; info!("SSE connected: {}", uri); }
                SSE::Event(ev) => self.handle_message(&ev.data, uri, tx, enricher, &tracker).await,
                SSE::Comment(_) => {}
            }
        }

        if opened && tracker.write().await.clear_if_server(uri) {
            let _ = tx.send(MediaUpdate::Stopped);
        }
        Err(())
    }

    async fn handle_message(&self, data: &str, uri: &str, tx: &mpsc::UnboundedSender<MediaUpdate>, enricher: &Arc<MetadataEnricher>, tracker: &RwLock<PlaybackTracker>) {
        let Ok(notif) = serde_json::from_str::<SseNotification>(data) else { return };
        let Some(playing) = notif.play_session_state else { return };

        if playing.state == "stopped" {
            let mut t = tracker.write().await;
            if t.info.is_some() {
                *t = PlaybackTracker::default();
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
        let offset = playing.view_offset.unwrap_or(0);

        {
            let mut t = tracker.write().await;
            if t.info.as_ref().and_then(|i| i.rating_key.as_deref()) == Some(&playing.rating_key) {
                if t.is_duplicate(&playing.rating_key, state, offset) { return; }
                t.update(state, offset);
                if let Some(ref info) = t.info {
                    let _ = tx.send(MediaUpdate::Playing(Box::new(info.clone())));
                }
                return;
            }
        }

        // For server owners, verify this session belongs to them
        if self.username.is_some() && !self.is_own_session(uri, &playing.rating_key).await {
            return;
        }

        let Some(mut info) = self.fetch_metadata(uri, &playing.rating_key, state, offset).await else { return };
        info!("Now playing: {} ({:?})", info.title, info.state);
        enricher.enrich(&mut info).await;

        tracker.write().await.set(info.clone(), uri);
        let _ = tx.send(MediaUpdate::Playing(Box::new(info)));
    }

    async fn is_own_session(&self, uri: &str, rating_key: &str) -> bool {
        let Some(username) = &self.username else { return true };

        let Ok(resp) = self.client
            .get(format!("{}/status/sessions", uri))
            .header("Accept", "application/json")
            .header("X-Plex-Token", &self.access_token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .timeout(REQUEST_TIMEOUT)
            .send().await else { return false };

        // 403 means shared user (not owner) - they only receive their own session notifications
        if resp.status() == reqwest::StatusCode::FORBIDDEN {
            return true;
        }

        let Ok(sessions) = resp.json::<SessionsResponse>().await else { return false };

        sessions.media_container.metadata.iter().any(|m| {
            m.rating_key.as_deref() == Some(rating_key)
                && m.user.as_ref().map(|u| &u.title) == Some(username)
        })
    }

    async fn fetch_metadata(&self, uri: &str, rating_key: &str, state: PlaybackState, view_offset: u64) -> Option<MediaInfo> {
        let resp = self.client
            .get(format!("{}/library/metadata/{}", uri, rating_key))
            .header("Accept", "application/json")
            .header("X-Plex-Token", &self.access_token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .timeout(REQUEST_TIMEOUT)
            .send().await.ok()?;

        let meta_resp: MetadataResponse = resp.json().await.ok()?;
        let meta = meta_resp.media_container.metadata.into_iter().next()?;

        let mut info = Self::parse_metadata(meta, rating_key, state, view_offset)?;
        self.enrich_external_ids(uri, &mut info).await;
        Some(info)
    }

    async fn enrich_external_ids(&self, uri: &str, info: &mut MediaInfo) {
        let key = match info.media_type {
            MediaType::Episode => info.grandparent_key.as_deref(),
            MediaType::Movie => info.key.as_deref(),
            _ => None,
        };
        let Some(key) = key else { return };

        let Some(resp) = self.client
            .get(format!("{}{}", uri, key))
            .header("Accept", "application/json")
            .header("X-Plex-Token", &self.access_token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .timeout(REQUEST_TIMEOUT)
            .send().await.ok() else { return };

        let Ok(meta) = resp.json::<MetadataResponse>().await else { return };
        let Some(item) = meta.media_container.metadata.into_iter().next() else { return };

        for guid in &item.guids {
            if let Some(id) = guid.id.strip_prefix("imdb://") { info.imdb_id = Some(id.to_string()); }
            else if let Some(id) = guid.id.strip_prefix("tmdb://") { info.tmdb_id = Some(id.to_string()); }
        }

        if info.media_type == MediaType::Episode {
            info.genres = item.genres.into_iter().map(|g| g.tag).collect();
        }
    }

    fn parse_metadata(meta: ItemMetadata, rating_key: &str, state: PlaybackState, view_offset: u64) -> Option<MediaInfo> {
        let media_type = match meta.media_type.as_str() {
            "movie" => MediaType::Movie,
            "episode" => MediaType::Episode,
            "track" => MediaType::Track,
            _ => return None,
        };

        let (imdb_id, tmdb_id) = meta.guids.iter().fold((None, None), |(imdb, tmdb), g| {
            (imdb.or_else(|| g.id.strip_prefix("imdb://").map(String::from)),
             tmdb.or_else(|| g.id.strip_prefix("tmdb://").map(String::from)))
        });

        Some(MediaInfo {
            title: meta.title,
            media_type,
            show_name: meta.grandparent_title.clone(),
            season: meta.parent_index,
            episode: meta.index,
            artist: meta.grandparent_title,
            album: meta.parent_title,
            year: meta.year,
            genres: meta.genres.into_iter().map(|g| g.tag).collect(),
            duration_ms: meta.duration.unwrap_or(0),
            view_offset_ms: view_offset,
            state,
            imdb_id,
            tmdb_id,
            mal_id: None,
            art_url: None,
            rating_key: Some(rating_key.to_string()),
            grandparent_key: meta.grandparent_key,
            key: meta.key,
        })
    }
}

#[derive(Deserialize)]
struct SseNotification {
    #[serde(rename = "PlaySessionStateNotification")]
    play_session_state: Option<PlaySessionState>,
}

#[derive(Deserialize)]
struct PlaySessionState {
    state: String,
    #[serde(rename = "ratingKey")]
    rating_key: String,
    #[serde(rename = "viewOffset")]
    view_offset: Option<u64>,
}

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct SessionsResponse { media_container: MediaContainer }

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct MediaContainer { #[serde(default)] metadata: Vec<SessionMetadata> }

#[derive(Deserialize)]
struct SessionMetadata {
    #[serde(rename = "User")]
    user: Option<UserInfo>,
    #[serde(rename = "ratingKey")]
    rating_key: Option<String>,
}

#[derive(Deserialize)]
struct UserInfo { title: String }

#[derive(Deserialize)]
struct GuidTag { id: String }

#[derive(Deserialize)]
struct GenreTag { tag: String }

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct MetadataResponse { media_container: MetadataContainer }

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct MetadataContainer { #[serde(default)] metadata: Vec<ItemMetadata> }

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct ItemMetadata {
    title: String,
    #[serde(rename = "type")]
    media_type: String,
    duration: Option<u64>,
    year: Option<u32>,
    grandparent_title: Option<String>,
    parent_index: Option<u32>,
    index: Option<u32>,
    parent_title: Option<String>,
    #[serde(rename = "Guid", default)]
    guids: Vec<GuidTag>,
    #[serde(rename = "Genre", default)]
    genres: Vec<GenreTag>,
    #[serde(rename = "grandparentKey")]
    grandparent_key: Option<String>,
    key: Option<String>,
}
