use futures::StreamExt;
use log::{debug, error, info, warn};
use reqwest::Client;
use reqwest_eventsource::{Event, EventSource};
use serde::Deserialize;
use std::collections::HashMap;
use std::time::{Duration, Instant};
use tokio::sync::mpsc;

pub const APP_NAME: &str = "presence-for-plex";

const PLEX_API: &str = "https://plex.tv/api/v2";
const TMDB_API: &str = "https://api.themoviedb.org/3";
const TMDB_IMAGE_BASE: &str = "https://image.tmdb.org/t/p/w500";
const JIKAN_API: &str = "https://api.jikan.moe/v4/anime";
const DEFAULT_TMDB_TOKEN: &str = "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiIzNmMxOTI3ZjllMTlkMzUxZWFmMjAxNGViN2JmYjNkZiIsIm5iZiI6MTc0NTQzMTA3NC4yMjcsInN1YiI6IjY4MDkyYTIyNmUxYTc2OWU4MWVmMGJhOSIsInNjb3BlcyI6WyJhcGlfcmVhZCJdLCJ2ZXJzaW9uIjoxfQ.Td6eAbW7SgQOMmQpRDwVM-_3KIMybGRqWNK8Yqw1Zzs";

const HTTP_TIMEOUT_SECS: u64 = 10;
const SESSION_TIMEOUT_SECS: u64 = 5;
pub const SSE_RECONNECT_DELAY_SECS: u64 = 5;
const SEEK_THRESHOLD_MS: u64 = 30_000;
const CACHE_TTL_SECS: u64 = 3600;
const CACHE_CLEANUP_THRESHOLD: usize = 100;

#[derive(Clone)]
struct CacheEntry {
    value: Option<CachedArtwork>,
    timestamp: Instant,
}

#[derive(Clone)]
struct CachedArtwork {
    art_url: String,
    mal_id: Option<String>,
}

pub struct PlexClient {
    client: Client,
    sse_client: Client,
    tmdb_token: String,
    username: Option<String>,
    cache: HashMap<String, CacheEntry>,
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
    grandparent_key: Option<String>,
    pub rating_key: Option<String>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum MediaType {
    Movie,
    Episode,
    Track,
}

#[derive(Debug, Clone, PartialEq)]
pub enum PlaybackState {
    Playing,
    Paused,
    Buffering,
    Stopped,
}

struct PlaybackTracker {
    rating_key: Option<String>,
    state: Option<PlaybackState>,
    view_offset: Option<u64>,
}

impl PlaybackTracker {
    fn new() -> Self {
        Self {
            rating_key: None,
            state: None,
            view_offset: None,
        }
    }

    fn is_duplicate(&self, info: &MediaInfo) -> bool {
        if self.rating_key != info.rating_key || self.state != Some(info.state.clone()) {
            return false;
        }

        let Some(last_offset) = self.view_offset else {
            return false;
        };

        let diff = last_offset.abs_diff(info.view_offset_ms);
        diff <= SEEK_THRESHOLD_MS
    }

    fn update(&mut self, info: &MediaInfo) {
        self.rating_key = info.rating_key.clone();
        self.state = Some(info.state.clone());
        self.view_offset = Some(info.view_offset_ms);
    }

    fn clear(&mut self) {
        self.rating_key = None;
        self.state = None;
        self.view_offset = None;
    }
}

impl PlexClient {
    pub fn new(tmdb_token: Option<String>) -> Self {
        let client = Client::builder()
            .user_agent("PresenceForPlex/1.0")
            .timeout(Duration::from_secs(HTTP_TIMEOUT_SECS))
            .build()
            .expect("Failed to create HTTP client");

        let sse_client = Client::builder()
            .user_agent("PresenceForPlex/1.0")
            .build()
            .expect("Failed to create SSE client");

        Self {
            client,
            sse_client,
            tmdb_token: tmdb_token.unwrap_or_else(|| DEFAULT_TMDB_TOKEN.to_string()),
            username: None,
            cache: HashMap::new(),
        }
    }

    pub async fn fetch_username(&mut self, token: &str) -> Option<String> {
        let response = self
            .client
            .get(format!("{}/user", PLEX_API))
            .header("Accept", "application/json")
            .header("X-Plex-Token", token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .send()
            .await;

        let resp = match response {
            Ok(r) => r,
            Err(e) => {
                warn!("Failed to fetch username: {}", e);
                return None;
            }
        };

        let json: serde_json::Value = match resp.json().await {
            Ok(j) => j,
            Err(e) => {
                warn!("Failed to parse user response: {}", e);
                return None;
            }
        };

        let username = json["username"].as_str()?.to_string();
        info!("Logged in as: {}", username);
        self.username = Some(username.clone());
        Some(username)
    }

    pub async fn request_pin(&self) -> Option<(u64, String)> {
        let response = self
            .client
            .post(format!("{}/pins", PLEX_API))
            .header("Accept", "application/json")
            .header("X-Plex-Product", "Presence for Plex")
            .header("X-Plex-Client-Identifier", APP_NAME)
            .query(&[("strong", "true")])
            .send()
            .await;

        let resp = match response {
            Ok(r) => r,
            Err(e) => {
                error!("Failed to request PIN: {}", e);
                return None;
            }
        };

        let json: serde_json::Value = match resp.json().await {
            Ok(j) => j,
            Err(e) => {
                error!("Failed to parse PIN response: {}", e);
                return None;
            }
        };

        Some((json["id"].as_u64()?, json["code"].as_str()?.to_string()))
    }

    pub async fn check_pin(&self, pin_id: u64) -> Option<String> {
        let resp = self
            .client
            .get(format!("{}/pins/{}", PLEX_API, pin_id))
            .header("Accept", "application/json")
            .header("X-Plex-Client-Identifier", APP_NAME)
            .send()
            .await
            .ok()?
            .json::<serde_json::Value>()
            .await
            .ok()?;

        resp["authToken"]
            .as_str()
            .filter(|s| !s.is_empty())
            .map(|s| s.to_string())
    }

    pub async fn start_sse_monitoring(
        &mut self,
        token: &str,
        tx: mpsc::UnboundedSender<Option<MediaInfo>>,
    ) {
        info!("Starting SSE monitoring");

        if self.username.is_none() {
            self.fetch_username(token).await;
        }

        let servers = match self.get_servers(token).await {
            Some(s) if !s.is_empty() => s,
            Some(_) => {
                warn!("No Plex servers found");
                return;
            }
            None => {
                error!("Failed to get servers");
                return;
            }
        };

        for server in &servers {
            let Some(access_token) = &server.access_token else {
                warn!("Server {} has no access token, skipping", server.name);
                continue;
            };

            info!("Connecting to server: {} ({} URIs)", server.name, server.connections.len());

            for conn in &server.connections {
                if let Err(e) = self
                    .monitor_server_connection(&conn.uri, access_token, &tx)
                    .await
                {
                    warn!("SSE connection to {} failed: {}", conn.uri, e);
                }
                tokio::time::sleep(Duration::from_secs(SSE_RECONNECT_DELAY_SECS)).await;
            }
        }
    }

    async fn monitor_server_connection(
        &mut self,
        server_uri: &str,
        access_token: &str,
        tx: &mpsc::UnboundedSender<Option<MediaInfo>>,
    ) -> Result<(), String> {
        info!("Trying SSE connection to: {}", server_uri);

        let sse_url = format!("{}/:/eventsource/notifications?filters=playing", server_uri);

        let request = self
            .sse_client
            .get(&sse_url)
            .header("Accept", "text/event-stream")
            .header("X-Plex-Token", access_token)
            .header("X-Plex-Client-Identifier", APP_NAME);

        let mut es = EventSource::new(request).map_err(|e| format!("Failed to create EventSource: {}", e))?;

        info!("SSE connected to {}", server_uri);

        let mut tracker = PlaybackTracker::new();

        while let Some(event) = es.next().await {
            match event {
                Ok(Event::Open) => {
                    info!("SSE connection opened");
                }
                Ok(Event::Message(msg)) => {
                    self.handle_sse_message(&msg.data, server_uri, access_token, tx, &mut tracker)
                        .await;
                }
                Err(e) => {
                    let _ = tx.send(None);
                    return Err(format!("SSE error: {:?}", e));
                }
            }
        }

        warn!("SSE connection closed");
        Ok(())
    }

    async fn handle_sse_message(
        &mut self,
        data: &str,
        server_uri: &str,
        access_token: &str,
        tx: &mpsc::UnboundedSender<Option<MediaInfo>>,
        tracker: &mut PlaybackTracker,
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
            if tracker.rating_key.is_some() {
                tracker.clear();
                let _ = tx.send(None);
            }
            return;
        }

        let Some(mut info) = Self::fetch_session(
            &self.client,
            server_uri,
            access_token,
            self.username.as_deref(),
        )
        .await
        else {
            return;
        };

        if tracker.is_duplicate(&info) {
            tracker.view_offset = Some(info.view_offset_ms);
            debug!("Skipping duplicate update");
            return;
        }

        tracker.update(&info);
        info!("Now playing: {} ({:?})", info.title, info.state);

        self.enrich_metadata(&mut info).await;
        let _ = tx.send(Some(info));
    }

    async fn get_servers(&self, token: &str) -> Option<Vec<PlexServer>> {
        debug!("Fetching servers...");

        let response = match self
            .client
            .get(format!("{}/resources", PLEX_API))
            .header("Accept", "application/json")
            .header("X-Plex-Token", token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .query(&[("includeHttps", "1"), ("includeRelay", "1")])
            .send()
            .await
        {
            Ok(r) => r,
            Err(e) => {
                error!("Failed to fetch servers: {}", e);
                return None;
            }
        };

        let resources: Vec<PlexServer> = match response.json().await {
            Ok(r) => r,
            Err(e) => {
                error!("Failed to parse servers: {}", e);
                return None;
            }
        };

        let servers: Vec<_> = resources
            .into_iter()
            .filter(|r| r.provides.contains("server") && !r.connections.is_empty())
            .collect();

        for server in &servers {
            info!(
                "Server: {} ({} connections, has_token: {})",
                server.name,
                server.connections.len(),
                server.access_token.is_some()
            );
        }

        Some(servers)
    }

    async fn fetch_session(
        client: &Client,
        server_uri: &str,
        access_token: &str,
        target_username: Option<&str>,
    ) -> Option<MediaInfo> {
        let resp = client
            .get(format!("{}/status/sessions", server_uri))
            .header("Accept", "application/json")
            .header("X-Plex-Token", access_token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .timeout(Duration::from_secs(SESSION_TIMEOUT_SECS))
            .send()
            .await
            .ok()?;

        if !resp.status().is_success() {
            return None;
        }

        let sessions: SessionsResponse = resp.json().await.ok()?;

        let meta = sessions
            .media_container
            .metadata
            .into_iter()
            .find(|m| match (target_username, &m.user) {
                (Some(target), Some(user)) => user.title == target,
                (Some(_), None) => false,
                (None, _) => true,
            })?;

        let mut info = Self::parse_session(meta)?;

        if info.media_type == MediaType::Episode {
            Self::enrich_episode_metadata(client, server_uri, access_token, &mut info).await;
        }

        Some(info)
    }

    async fn enrich_episode_metadata(
        client: &Client,
        server_uri: &str,
        access_token: &str,
        info: &mut MediaInfo,
    ) {
        let Some(gp_key) = info.grandparent_key.take() else {
            return;
        };

        let Some(show_meta) = Self::fetch_item_metadata(client, server_uri, access_token, &gp_key).await else {
            return;
        };

        for guid in &show_meta.guids {
            if let Some(id) = guid.id.strip_prefix("imdb://") {
                info.imdb_id = Some(id.to_string());
            } else if let Some(id) = guid.id.strip_prefix("tmdb://") {
                info.tmdb_id = Some(id.to_string());
            }
        }

        info.genres = show_meta.genres.into_iter().map(|g| g.tag).collect();
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
            grandparent_key: meta.grandparent_key,
            rating_key: meta.rating_key,
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

    async fn fetch_item_metadata(
        client: &Client,
        server_uri: &str,
        access_token: &str,
        key: &str,
    ) -> Option<ItemMetadata> {
        let resp = client
            .get(format!("{}{}", server_uri, key))
            .header("Accept", "application/json")
            .header("X-Plex-Token", access_token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .timeout(Duration::from_secs(SESSION_TIMEOUT_SECS))
            .send()
            .await
            .ok()?;

        let meta: MetadataResponse = resp.json().await.ok()?;
        meta.media_container.metadata.into_iter().next()
    }

    async fn enrich_metadata(&mut self, info: &mut MediaInfo) {
        debug!(
            "Enriching: {} (tmdb_id: {:?}, genres: {:?})",
            info.title, info.tmdb_id, info.genres
        );

        self.cleanup_cache();

        let cache_key = Self::build_cache_key(info);

        if let Some(cached) = self.get_cached(&cache_key) {
            debug!("Cache hit for {}", cache_key);
            if let Some(artwork) = cached {
                info.art_url = Some(artwork.art_url);
                info.mal_id = artwork.mal_id;
            }
            return;
        }

        if self.try_tmdb_artwork(info, &cache_key).await {
            return;
        }

        self.try_jikan_artwork(info, &cache_key).await;
    }

    fn build_cache_key(info: &MediaInfo) -> String {
        match &info.tmdb_id {
            Some(tmdb_id) => format!("tmdb:{}:{:?}", tmdb_id, info.media_type),
            None => {
                let search_title = info.show_name.as_ref().unwrap_or(&info.title);
                format!("jikan:{}:{:?}", search_title, info.year)
            }
        }
    }

    async fn try_tmdb_artwork(&mut self, info: &mut MediaInfo, cache_key: &str) -> bool {
        let Some(ref tmdb_id) = info.tmdb_id else {
            return false;
        };

        let result = Self::fetch_tmdb_artwork(&self.client, &self.tmdb_token, tmdb_id, &info.media_type).await;

        self.set_cached(
            cache_key,
            result.as_ref().map(|url| CachedArtwork {
                art_url: url.clone(),
                mal_id: None,
            }),
        );

        if let Some(url) = result {
            info!("Got TMDB artwork: {}", url);
            info.art_url = Some(url);
            return true;
        }

        false
    }

    async fn try_jikan_artwork(&mut self, info: &mut MediaInfo, cache_key: &str) {
        let is_anime = info
            .genres
            .iter()
            .any(|g| matches!(g.to_lowercase().as_str(), "anime" | "animation"));

        if !is_anime {
            self.set_cached(cache_key, None);
            return;
        }

        let search_title = info.show_name.as_ref().unwrap_or(&info.title);
        let result = Self::fetch_jikan_artwork(&self.client, search_title, info.year).await;

        self.set_cached(cache_key, result.clone());

        if let Some(artwork) = result {
            info!("Got Jikan artwork for MAL {:?}: {}", artwork.mal_id, artwork.art_url);
            info.mal_id = artwork.mal_id;
            info.art_url = Some(artwork.art_url);
        }
    }

    fn cleanup_cache(&mut self) {
        if self.cache.len() < CACHE_CLEANUP_THRESHOLD {
            return;
        }
        let ttl = Duration::from_secs(CACHE_TTL_SECS);
        self.cache.retain(|_, entry| entry.timestamp.elapsed() < ttl);
    }

    fn get_cached(&self, key: &str) -> Option<Option<CachedArtwork>> {
        let entry = self.cache.get(key)?;
        let ttl = Duration::from_secs(CACHE_TTL_SECS);

        if entry.timestamp.elapsed() < ttl {
            Some(entry.value.clone())
        } else {
            None
        }
    }

    fn set_cached(&mut self, key: &str, value: Option<CachedArtwork>) {
        self.cache.insert(
            key.to_string(),
            CacheEntry {
                value,
                timestamp: Instant::now(),
            },
        );
    }

    async fn fetch_tmdb_artwork(
        client: &Client,
        tmdb_token: &str,
        tmdb_id: &str,
        media_type: &MediaType,
    ) -> Option<String> {
        let media_path = match media_type {
            MediaType::Movie => "movie",
            MediaType::Episode => "tv",
            MediaType::Track => return None,
        };

        let endpoint = format!("{}/{}/{}/images", TMDB_API, media_path, tmdb_id);

        let resp: TmdbImagesResponse = client
            .get(&endpoint)
            .header("Authorization", format!("Bearer {}", tmdb_token))
            .header("Accept", "application/json")
            .send()
            .await
            .ok()?
            .json()
            .await
            .ok()?;

        resp.posters
            .first()
            .or(resp.backdrops.first())
            .map(|img| format!("{}{}", TMDB_IMAGE_BASE, img.file_path))
    }

    async fn fetch_jikan_artwork(
        client: &Client,
        title: &str,
        year: Option<u32>,
    ) -> Option<CachedArtwork> {
        debug!("Searching Jikan for: {}", title);

        let mut url = format!("{}?q={}", JIKAN_API, urlencoding::encode(title));

        if let Some(y) = year {
            url.push_str(&format!("&start_date={y}-01-01&end_date={y}-12-31"));
        }

        let resp: JikanResponse = client.get(&url).send().await.ok()?.json().await.ok()?;

        let anime = resp.data.first()?;
        let art_url = anime
            .images
            .as_ref()?
            .jpg
            .as_ref()?
            .large_image_url
            .as_ref()?
            .clone();

        Some(CachedArtwork {
            art_url,
            mal_id: Some(anime.mal_id.to_string()),
        })
    }
}

// SSE notification types - direct format: {"PlaySessionStateNotification":{...}}
#[derive(Debug, Deserialize)]
struct SseNotification {
    #[serde(rename = "PlaySessionStateNotification")]
    play_session_state: Option<PlaySessionState>,
}

#[derive(Debug, Deserialize)]
struct PlaySessionState {
    state: String,
}

// Plex API response types
#[derive(Debug, Deserialize)]
struct PlexServer {
    name: String,
    provides: String,
    #[serde(rename = "accessToken")]
    access_token: Option<String>,
    #[serde(default)]
    connections: Vec<PlexConnection>,
}

#[derive(Debug, Deserialize)]
struct PlexConnection {
    uri: String,
}

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
    #[serde(default)]
    genre: Vec<GenreTag>,
    #[serde(rename = "Player")]
    player: Option<PlayerInfo>,
    #[serde(rename = "User")]
    user: Option<UserInfo>,
    #[serde(rename = "Guid", default)]
    guids: Vec<GuidTag>,
    #[serde(rename = "grandparentKey")]
    grandparent_key: Option<String>,
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

// TMDB response types
#[derive(Deserialize)]
struct TmdbImagesResponse {
    #[serde(default)]
    posters: Vec<TmdbImage>,
    #[serde(default)]
    backdrops: Vec<TmdbImage>,
}

#[derive(Deserialize)]
struct TmdbImage {
    file_path: String,
}

// Jikan response types
#[derive(Deserialize)]
struct JikanResponse {
    #[serde(default)]
    data: Vec<JikanAnime>,
}

#[derive(Deserialize)]
struct JikanAnime {
    mal_id: u64,
    images: Option<JikanImages>,
}

#[derive(Deserialize)]
struct JikanImages {
    jpg: Option<JikanJpg>,
}

#[derive(Deserialize)]
struct JikanJpg {
    large_image_url: Option<String>,
}
