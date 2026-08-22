use eventsource_client::{self as es, Client as EsClient, SSE};
use futures_util::TryStreamExt;
use log::{debug, info, warn};
use reqwest::Client;
use serde::{Deserialize, Deserializer};
use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::sync::mpsc;

use crate::error::NetError;
use crate::media::{MediaInfo, MediaType, MediaUpdate, PlaybackState, SessionId};
use crate::metadata::MetadataEnricher;
use crate::plex_account::ServerConnection;

const SSE_RECONNECT_DELAY: Duration = Duration::from_secs(5);
const SSE_CONNECT_TIMEOUT: Duration = Duration::from_secs(10);
// Plex sends keepalive comments every few seconds; silence far beyond that
// means the connection died even if TCP never noticed (sleep/wake, NAT expiry).
const SSE_READ_TIMEOUT: Duration = Duration::from_secs(90);
const REQUEST_TIMEOUT: Duration = Duration::from_secs(10);
const SEEK_THRESHOLD_MS: u64 = 30_000;

pub struct PlexServer {
    name: String,
    connections: Vec<ServerConnection>,
    access_token: String,
    username: Option<String>,
    client_id: String,
    client: Client,
}

/// Sessions this connection has admitted, keyed by Plex sessionKey
/// (ratingKey when absent).
#[derive(Default)]
struct PlaybackTracker {
    sessions: HashMap<String, TrackedSession>,
}

struct TrackedSession {
    info: MediaInfo,
    last_update: Instant,
}

impl TrackedSession {
    fn is_duplicate(&self, state: PlaybackState, offset: u64) -> bool {
        if self.info.state != state {
            return false;
        }
        // Extrapolating a frozen (paused/buffering) offset would misread
        // repeated notifications as seeks.
        let expected = if self.info.state == PlaybackState::Playing {
            self.info
                .view_offset_ms
                .saturating_add(self.last_update.elapsed().as_millis() as u64)
        } else {
            self.info.view_offset_ms
        };
        expected.abs_diff(offset) <= SEEK_THRESHOLD_MS
    }

    fn update(&mut self, state: PlaybackState, offset: u64) {
        self.info.state = state;
        self.info.view_offset_ms = offset;
        self.last_update = Instant::now();
    }
}

impl PlexServer {
    pub fn new(
        name: String,
        connections: Vec<ServerConnection>,
        access_token: String,
        username: Option<String>,
        client_id: String,
    ) -> Self {
        Self {
            name,
            connections,
            access_token,
            username,
            client_id,
            client: Client::builder()
                .user_agent("PresenceForPlex/1.0")
                .build()
                .expect("HTTP client"),
        }
    }

    pub async fn start_monitoring(
        self,
        tx: &mpsc::UnboundedSender<MediaUpdate>,
        enricher: Arc<MetadataEnricher>,
    ) {
        info!("Monitoring server: {}", self.name);
        loop {
            for conn in &self.connections {
                self.try_connection(&conn.uri, tx, &enricher).await;
            }
            tokio::time::sleep(SSE_RECONNECT_DELAY).await;
        }
    }

    fn session_id(&self, key: String) -> SessionId {
        SessionId {
            server: self.name.clone(),
            key,
        }
    }

    fn build_sse_client(&self, url: &str) -> Result<impl EsClient, Box<es::Error>> {
        Ok(es::ClientBuilder::for_url(url)?
            .header("Accept", "text/event-stream")?
            .header("X-Plex-Token", &self.access_token)?
            .header("X-Plex-Client-Identifier", &self.client_id)?
            .connect_timeout(SSE_CONNECT_TIMEOUT)
            .read_timeout(SSE_READ_TIMEOUT)
            .build())
    }

    fn get(&self, url: String) -> reqwest::RequestBuilder {
        self.client
            .get(url)
            .header("Accept", "application/json")
            .header("X-Plex-Token", &self.access_token)
            .header("X-Plex-Client-Identifier", &self.client_id)
            .timeout(REQUEST_TIMEOUT)
    }

    async fn fetch_first_item(&self, url: String) -> Result<Option<ItemMetadata>, NetError> {
        let resp = self.get(url).send().await?;
        let meta: Response<ItemMetadata> = resp.error_for_status()?.json().await?;
        Ok(meta.media_container.metadata.into_iter().next())
    }

    async fn try_connection(
        &self,
        uri: &str,
        tx: &mpsc::UnboundedSender<MediaUpdate>,
        enricher: &Arc<MetadataEnricher>,
    ) {
        let url = format!("{}/:/eventsource/notifications?filters=playing", uri);
        let client = match self.build_sse_client(&url) {
            Ok(client) => client,
            Err(e) => {
                warn!("SSE setup failed for {}: {}", uri, e);
                return;
            }
        };

        let mut stream = Box::pin(client.stream());
        let mut tracker = PlaybackTracker::default();

        loop {
            match stream.try_next().await {
                Ok(Some(SSE::Connected(_))) => {
                    info!("SSE connected: {}", uri);
                    // The client also reconnects internally after some errors;
                    // sessions from before the gap may have died without a stop
                    // event. Live ones are re-admitted by Plex's periodic
                    // notifications within seconds.
                    self.drain_sessions(&mut tracker, tx);
                }
                Ok(Some(SSE::Event(ev))) => {
                    self.handle_message(&ev.data, uri, tx, enricher, &mut tracker)
                        .await
                }
                Ok(Some(SSE::Comment(_))) => {}
                Ok(None) => {
                    info!("SSE stream ended: {}", uri);
                    break;
                }
                Err(e) => {
                    warn!("SSE stream error on {}: {}", uri, e);
                    break;
                }
            }
        }

        // Playback state is unknowable while disconnected
        self.drain_sessions(&mut tracker, tx);
    }

    fn drain_sessions(
        &self,
        tracker: &mut PlaybackTracker,
        tx: &mpsc::UnboundedSender<MediaUpdate>,
    ) {
        for key in std::mem::take(&mut tracker.sessions).into_keys() {
            let _ = tx.send(MediaUpdate::Stopped(self.session_id(key)));
        }
    }

    async fn handle_message(
        &self,
        data: &str,
        uri: &str,
        tx: &mpsc::UnboundedSender<MediaUpdate>,
        enricher: &Arc<MetadataEnricher>,
        tracker: &mut PlaybackTracker,
    ) {
        let Ok(notif) = serde_json::from_str::<SseNotification>(data) else {
            return;
        };
        let Some(playing) = notif.play_session_state else {
            return;
        };

        let session_key = playing
            .session_key
            .clone()
            .unwrap_or_else(|| playing.rating_key.clone());

        if playing.state == "stopped" {
            if tracker.sessions.remove(&session_key).is_some() {
                let _ = tx.send(MediaUpdate::Stopped(self.session_id(session_key)));
            }
            return;
        }

        let state = match playing.state.as_str() {
            "playing" => PlaybackState::Playing,
            "paused" => PlaybackState::Paused,
            "buffering" => PlaybackState::Buffering,
            _ => return,
        };

        if let Some(t) = tracker.sessions.get_mut(&session_key)
            && t.info.rating_key.as_deref() == Some(playing.rating_key.as_str())
        {
            // A missing offset means "unchanged", not "back to the start"
            let offset = playing.view_offset.unwrap_or(t.info.view_offset_ms);
            if t.is_duplicate(state, offset) {
                return;
            }
            t.update(state, offset);
            let _ = tx.send(MediaUpdate::Playing(
                self.session_id(session_key),
                Box::new(t.info.clone()),
            ));
            return;
        }

        // Owner check for new sessions only; a tracked session that switched
        // items (e.g. the next episode) was vetted when admitted
        if !tracker.sessions.contains_key(&session_key)
            && self.username.is_some()
            && !self.is_own_session(uri, &playing.rating_key).await
        {
            return;
        }

        let offset = playing.view_offset.unwrap_or(0);
        let mut info = match self
            .fetch_metadata(uri, &playing.rating_key, state, offset)
            .await
        {
            Ok(Some(info)) => info,
            Ok(None) => return, // unsupported media type
            Err(e) => {
                warn!(
                    "Metadata fetch for item {} on {} failed: {}",
                    playing.rating_key, uri, e
                );
                return;
            }
        };
        info!("Now playing: {} ({:?})", info.title, info.state);
        enricher.enrich(&mut info).await;

        tracker.sessions.insert(
            session_key.clone(),
            TrackedSession {
                info: info.clone(),
                last_update: Instant::now(),
            },
        );
        let _ = tx.send(MediaUpdate::Playing(
            self.session_id(session_key),
            Box::new(info),
        ));
    }

    async fn is_own_session(&self, uri: &str, rating_key: &str) -> bool {
        let Some(username) = &self.username else {
            return true;
        };

        let result: Result<bool, NetError> = async {
            let resp = self.get(format!("{}/status/sessions", uri)).send().await?;

            // 403 means shared user (not owner) - they only receive their own
            // session notifications
            if resp.status() == reqwest::StatusCode::FORBIDDEN {
                return Ok(true);
            }

            let sessions: Response<SessionMetadata> = resp.error_for_status()?.json().await?;
            Ok(sessions.media_container.metadata.iter().any(|m| {
                m.rating_key.as_deref() == Some(rating_key)
                    && m.user.as_ref().map(|u| &u.title) == Some(username)
            }))
        }
        .await;

        match result {
            Ok(own) => own,
            Err(e) => {
                warn!(
                    "Session ownership check on {} failed (session ignored): {}",
                    uri, e
                );
                false
            }
        }
    }

    async fn fetch_metadata(
        &self,
        uri: &str,
        rating_key: &str,
        state: PlaybackState,
        view_offset: u64,
    ) -> Result<Option<MediaInfo>, NetError> {
        let url = format!("{}/library/metadata/{}", uri, rating_key);
        let Some(meta) = self.fetch_first_item(url).await? else {
            return Err(NetError::Parse("empty metadata container".into()));
        };

        let Some(mut info) = Self::parse_metadata(meta, rating_key, state, view_offset) else {
            return Ok(None);
        };
        self.enrich_external_ids(uri, &mut info).await;
        Ok(Some(info))
    }

    async fn enrich_external_ids(&self, uri: &str, info: &mut MediaInfo) {
        let key = match info.media_type {
            MediaType::Episode => info.grandparent_key.as_deref(),
            MediaType::Movie => info.key.as_deref(),
            _ => None,
        };
        let Some(key) = key else { return };

        let item = match self.fetch_first_item(format!("{}{}", uri, key)).await {
            Ok(Some(item)) => item,
            Ok(None) => return,
            Err(e) => {
                debug!("External id lookup for {} failed (best effort): {}", key, e);
                return;
            }
        };

        let (imdb, tmdb) = external_ids(&item.guids);
        if let Some(id) = imdb {
            info.imdb_id = Some(id);
        }
        if let Some(id) = tmdb {
            info.tmdb_id = Some(id);
        }

        if info.media_type == MediaType::Episode {
            info.genres = item.genres.into_iter().map(|g| g.tag).collect();
        }
    }

    fn parse_metadata(
        meta: ItemMetadata,
        rating_key: &str,
        state: PlaybackState,
        view_offset: u64,
    ) -> Option<MediaInfo> {
        let media_type = match meta.media_type.as_str() {
            "movie" => MediaType::Movie,
            "episode" => MediaType::Episode,
            "track" => MediaType::Track,
            _ => return None,
        };

        let (imdb_id, tmdb_id) = external_ids(&meta.guids);

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

fn external_ids(guids: &[GuidTag]) -> (Option<String>, Option<String>) {
    let find = |scheme: &str| {
        guids
            .iter()
            .find_map(|g| g.id.strip_prefix(scheme))
            .map(String::from)
    };
    (find("imdb://"), find("tmdb://"))
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
    #[serde(
        rename = "sessionKey",
        default,
        deserialize_with = "de_opt_string_or_num"
    )]
    session_key: Option<String>,
}

/// Plex is inconsistent about whether sessionKey is a JSON string or number.
fn de_opt_string_or_num<'de, D: Deserializer<'de>>(d: D) -> Result<Option<String>, D::Error> {
    #[derive(Deserialize)]
    #[serde(untagged)]
    enum V {
        S(String),
        N(u64),
    }
    Ok(Option::<V>::deserialize(d)?.map(|v| match v {
        V::S(s) => s,
        V::N(n) => n.to_string(),
    }))
}

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct Response<T> {
    media_container: Container<T>,
}

#[derive(Deserialize)]
#[serde(rename_all = "PascalCase")]
struct Container<T> {
    // plain #[serde(default)] would add a `T: Default` bound
    #[serde(default = "Vec::new")]
    metadata: Vec<T>,
}

#[derive(Deserialize)]
struct SessionMetadata {
    #[serde(rename = "User")]
    user: Option<UserInfo>,
    #[serde(rename = "ratingKey")]
    rating_key: Option<String>,
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
    grandparent_key: Option<String>,
    key: Option<String>,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn playing_info(rating_key: &str, offset: u64) -> MediaInfo {
        let mut info = MediaInfo::test_stub(MediaType::Movie);
        info.rating_key = Some(rating_key.to_string());
        info.view_offset_ms = offset;
        info
    }

    fn tracked(rating_key: &str, offset: u64, state: PlaybackState) -> TrackedSession {
        let mut info = playing_info(rating_key, offset);
        info.state = state;
        TrackedSession {
            info,
            last_update: Instant::now(),
        }
    }

    #[test]
    fn session_detects_duplicate_progress_updates() {
        let t = tracked("1", 1000, PlaybackState::Playing);
        // Same state with offset within the seek threshold is a duplicate
        assert!(t.is_duplicate(PlaybackState::Playing, 1000));
        assert!(t.is_duplicate(PlaybackState::Playing, 20_000));
    }

    #[test]
    fn session_treats_seek_as_new_update() {
        let t = tracked("1", 1000, PlaybackState::Playing);
        assert!(!t.is_duplicate(PlaybackState::Playing, 1000 + SEEK_THRESHOLD_MS + 1));
    }

    #[test]
    fn session_treats_state_change_as_new_update() {
        let t = tracked("1", 1000, PlaybackState::Playing);
        assert!(!t.is_duplicate(PlaybackState::Paused, 1000));
    }

    #[test]
    fn paused_session_does_not_drift() {
        // While paused the expected offset is frozen: identical repeats stay
        // duplicates no matter how much wall-clock time passes.
        let mut t = tracked("1", 1000, PlaybackState::Paused);
        t.last_update = Instant::now() - Duration::from_secs(120);
        assert!(t.is_duplicate(PlaybackState::Paused, 1000));
    }

    #[test]
    fn session_update_applies_state_and_offset() {
        let mut t = tracked("1", 1000, PlaybackState::Playing);
        t.update(PlaybackState::Paused, 5000);
        assert_eq!(t.info.state, PlaybackState::Paused);
        assert_eq!(t.info.view_offset_ms, 5000);
    }

    #[test]
    fn tracker_tracks_sessions_independently() {
        let mut tracker = PlaybackTracker::default();
        tracker
            .sessions
            .insert("s1".into(), tracked("1", 0, PlaybackState::Playing));
        tracker
            .sessions
            .insert("s2".into(), tracked("2", 0, PlaybackState::Paused));
        assert!(tracker.sessions.remove("s3").is_none());
        assert!(tracker.sessions.remove("s1").is_some());
        assert!(tracker.sessions.contains_key("s2"));
    }

    #[test]
    fn parse_metadata_maps_episode_fields() {
        let meta: ItemMetadata = serde_json::from_str(
            r#"{
            "title": "The One Where It Works",
            "type": "episode",
            "duration": 1320000,
            "year": 1994,
            "grandparentTitle": "Friends",
            "parentIndex": 1,
            "index": 2,
            "parentTitle": "Season 1",
            "Guid": [{"id": "imdb://tt0583459"}, {"id": "tmdb://123"}],
            "Genre": [{"tag": "Comedy"}],
            "grandparentKey": "/library/metadata/100"
        }"#,
        )
        .unwrap();

        let info = PlexServer::parse_metadata(meta, "42", PlaybackState::Playing, 5000).unwrap();
        assert_eq!(info.media_type, MediaType::Episode);
        assert_eq!(info.title, "The One Where It Works");
        assert_eq!(info.show_name.as_deref(), Some("Friends"));
        assert_eq!(info.season, Some(1));
        assert_eq!(info.episode, Some(2));
        assert_eq!(info.imdb_id.as_deref(), Some("tt0583459"));
        assert_eq!(info.tmdb_id.as_deref(), Some("123"));
        assert_eq!(info.genres, vec!["Comedy".to_string()]);
        assert_eq!(info.duration_ms, 1320000);
        assert_eq!(info.view_offset_ms, 5000);
        assert_eq!(info.rating_key.as_deref(), Some("42"));
        assert_eq!(
            info.grandparent_key.as_deref(),
            Some("/library/metadata/100")
        );
    }

    #[test]
    fn parse_metadata_maps_track_fields() {
        let meta: ItemMetadata = serde_json::from_str(
            r#"{
            "title": "Song",
            "type": "track",
            "grandparentTitle": "Artist",
            "parentTitle": "Album"
        }"#,
        )
        .unwrap();

        let info = PlexServer::parse_metadata(meta, "7", PlaybackState::Paused, 0).unwrap();
        assert_eq!(info.media_type, MediaType::Track);
        assert_eq!(info.artist.as_deref(), Some("Artist"));
        assert_eq!(info.album.as_deref(), Some("Album"));
        assert_eq!(info.duration_ms, 0);
    }

    #[test]
    fn parse_metadata_rejects_unknown_types() {
        let meta: ItemMetadata =
            serde_json::from_str(r#"{"title": "Photo", "type": "photo"}"#).unwrap();
        assert!(PlexServer::parse_metadata(meta, "1", PlaybackState::Playing, 0).is_none());
    }

    #[test]
    fn sse_notification_parses_plex_payload() {
        let notif: SseNotification = serde_json::from_str(
            r#"{
            "PlaySessionStateNotification": {
                "state": "playing",
                "ratingKey": "123",
                "viewOffset": 60000,
                "sessionKey": "7"
            }
        }"#,
        )
        .unwrap();
        let playing = notif.play_session_state.unwrap();
        assert_eq!(playing.state, "playing");
        assert_eq!(playing.rating_key, "123");
        assert_eq!(playing.view_offset, Some(60000));
        assert_eq!(playing.session_key.as_deref(), Some("7"));
    }

    #[test]
    fn sse_notification_accepts_numeric_or_missing_session_key() {
        let notif: SseNotification = serde_json::from_str(
            r#"{"PlaySessionStateNotification": {"state": "playing", "ratingKey": "1", "sessionKey": 7}}"#,
        )
        .unwrap();
        assert_eq!(
            notif.play_session_state.unwrap().session_key.as_deref(),
            Some("7")
        );

        let notif: SseNotification = serde_json::from_str(
            r#"{"PlaySessionStateNotification": {"state": "playing", "ratingKey": "1"}}"#,
        )
        .unwrap();
        assert_eq!(notif.play_session_state.unwrap().session_key, None);
    }
}
