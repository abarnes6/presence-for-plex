use log::info;
use percent_encoding::{utf8_percent_encode, NON_ALPHANUMERIC};
use reqwest::Client;
use serde::Deserialize;
use std::collections::HashMap;
use std::sync::RwLock;
use std::time::{Duration, Instant};

use crate::plex_server::{MediaInfo, MediaType};

const TMDB_API: &str = "https://api.themoviedb.org/3";
const TMDB_IMAGE_BASE: &str = "https://image.tmdb.org/t/p/w500";
const JIKAN_API: &str = "https://api.jikan.moe/v4/anime";
const MUSICBRAINZ_API: &str = "https://musicbrainz.org/ws/2";
const COVERART_API: &str = "https://coverartarchive.org";
const DEFAULT_TMDB_TOKEN: &str = "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiIzNmMxOTI3ZjllMTlkMzUxZWFmMjAxNGViN2JmYjNkZiIsIm5iZiI6MTc0NTQzMTA3NC4yMjcsInN1YiI6IjY4MDkyYTIyNmUxYTc2OWU4MWVmMGJhOSIsInNjb3BlcyI6WyJhcGlfcmVhZCJdLCJ2ZXJzaW9uIjoxfQ.Td6eAbW7SgQOMmQpRDwVM-_3KIMybGRqWNK8Yqw1Zzs";
const CACHE_TTL: Duration = Duration::from_secs(28800);
const CACHE_CLEANUP_THRESHOLD: usize = 100;

#[derive(Clone)]
struct CacheEntry {
    value: Option<String>,
    timestamp: Instant,
}

pub struct MetadataEnricher {
    client: Client,
    tmdb_token: String,
    art_cache: RwLock<HashMap<String, CacheEntry>>,
    mal_cache: RwLock<HashMap<String, CacheEntry>>,
}

impl MetadataEnricher {
    pub fn new(tmdb_token: Option<String>) -> Self {
        Self {
            client: Client::builder().user_agent("PresenceForPlex/1.0").timeout(Duration::from_secs(10)).build().expect("HTTP client"),
            tmdb_token: tmdb_token.unwrap_or_else(|| DEFAULT_TMDB_TOKEN.to_string()),
            art_cache: RwLock::new(HashMap::new()),
            mal_cache: RwLock::new(HashMap::new()),
        }
    }

    pub async fn enrich(&self, info: &mut MediaInfo) {
        self.cleanup_cache();
        let key = self.cache_key(info);

        let cached = self.get_art_cached(&key);
        if let Some(Some(url)) = &cached {
            info.art_url = Some(url.clone());
        }

        // Fetch artwork if not cached
        if cached.is_none() {
            if info.media_type == MediaType::Track {
                self.try_musicbrainz(info, &key).await;
            } else {
                self.try_tmdb(info, &key).await;
            }
        }

        // For anime, fetch MAL ID for the link
        let is_anime = info.genres.iter().any(|g| matches!(g.to_lowercase().as_str(), "anime" | "animation"));
        if is_anime && info.media_type != MediaType::Track {
            self.fetch_mal_id(info).await;
        }
    }

    fn cache_key(&self, info: &MediaInfo) -> String {
        match info.media_type {
            MediaType::Track => format!("mb:{}:{}", info.artist.as_deref().unwrap_or(""), info.album.as_deref().unwrap_or("")),
            _ => info.tmdb_id.as_ref()
                .map(|id| format!("tmdb:{}:{:?}", id, info.media_type))
                .unwrap_or_else(|| format!("jikan:{}:{:?}", info.show_name.as_ref().unwrap_or(&info.title), info.year)),
        }
    }

    async fn try_tmdb(&self, info: &mut MediaInfo, key: &str) -> bool {
        let Some(ref tmdb_id) = info.tmdb_id else { return false };
        let path = match info.media_type { MediaType::Movie => "movie", MediaType::Episode => "tv", _ => return false };

        let result = async {
            let resp = self.client
                .get(format!("{}/{}/{}/images", TMDB_API, path, tmdb_id))
                .header("Authorization", format!("Bearer {}", self.tmdb_token))
                .send().await.ok()?;
            let images: TmdbImages = resp.json().await.ok()?;
            images.posters.first().or(images.backdrops.first()).map(|i| format!("{}{}", TMDB_IMAGE_BASE, i.file_path))
        }.await;

        self.set_art_cached(key, result.clone());
        if let Some(url) = result { info!("TMDB artwork: {}", url); info.art_url = Some(url); true } else { false }
    }

    async fn fetch_mal_id(&self, info: &mut MediaInfo) {
        let title = info.show_name.as_ref().unwrap_or(&info.title);
        let cache_key = format!("{}_{}", title, info.year.unwrap_or(0));

        // Check cache first
        if let Some(cached) = self.get_mal_cached(&cache_key) {
            info.mal_id = cached;
            return;
        }

        let url = format!("{}?q={}&limit=1", JIKAN_API, utf8_percent_encode(&cache_key, NON_ALPHANUMERIC));

        let mal_id = async {
            let resp = self.client.get(&url).send().await.ok()?;
            let data: JikanResponse = resp.json().await.ok()?;
            Some(data.data.first()?.mal_id.to_string())
        }.await;

        self.set_mal_cached(&cache_key, mal_id.clone());
        if let Some(id) = mal_id { info!("MAL ID: {}", id); info.mal_id = Some(id); }
    }

    async fn try_musicbrainz(&self, info: &mut MediaInfo, key: &str) {
        let (Some(artist), Some(album)) = (&info.artist, &info.album) else { self.set_art_cached(key, None); return };
        let query = format!("artist:\"{}\" AND release:\"{}\"", artist.replace('"', ""), album.replace('"', ""));
        let ua = concat!("PresenceForPlex/", env!("CARGO_PKG_VERSION"), " (https://github.com/abarnes6/presence-for-plex)");

        let mbid = async {
            let resp = self.client
                .get(format!("{}/release?query={}&fmt=json&limit=1", MUSICBRAINZ_API, utf8_percent_encode(&query, NON_ALPHANUMERIC)))
                .header("User-Agent", ua)
                .send().await.ok()?;
            let data: MbSearch = resp.json().await.ok()?;
            data.releases.first().map(|rel| rel.id.clone())
        }.await;

        let Some(mbid) = mbid else { self.set_art_cached(key, None); return };
        let cover_url = format!("{}/release/{}/front", COVERART_API, mbid);

        let exists = self.client.head(&cover_url).header("User-Agent", ua).send().await
            .map(|r| r.status().is_success() || r.status().is_redirection()).unwrap_or(false);

        let result = if exists { Some(cover_url) } else { None };
        self.set_art_cached(key, result.clone());
        if let Some(url) = result { info!("MusicBrainz artwork: {}", url); info.art_url = Some(url); }
    }

    fn cleanup_cache(&self) {
        for cache in [&self.art_cache, &self.mal_cache] {
            let mut c = cache.write().unwrap();
            if c.len() >= CACHE_CLEANUP_THRESHOLD {
                c.retain(|_, e| e.timestamp.elapsed() < CACHE_TTL);
            }
        }
    }

    fn get_art_cached(&self, key: &str) -> Option<Option<String>> {
        let cache = self.art_cache.read().unwrap();
        cache.get(key).filter(|e| e.timestamp.elapsed() < CACHE_TTL).map(|e| e.value.clone())
    }

    fn set_art_cached(&self, key: &str, value: Option<String>) {
        self.art_cache.write().unwrap().insert(key.to_string(), CacheEntry { value, timestamp: Instant::now() });
    }

    fn get_mal_cached(&self, key: &str) -> Option<Option<String>> {
        let cache = self.mal_cache.read().unwrap();
        cache.get(key).filter(|e| e.timestamp.elapsed() < CACHE_TTL).map(|e| e.value.clone())
    }

    fn set_mal_cached(&self, key: &str, value: Option<String>) {
        self.mal_cache.write().unwrap().insert(key.to_string(), CacheEntry { value, timestamp: Instant::now() });
    }
}

#[derive(Deserialize)]
struct TmdbImages { #[serde(default)] posters: Vec<TmdbImage>, #[serde(default)] backdrops: Vec<TmdbImage> }
#[derive(Deserialize)]
struct TmdbImage { file_path: String }
#[derive(Deserialize)]
struct JikanResponse { #[serde(default)] data: Vec<JikanAnime> }
#[derive(Deserialize)]
struct JikanAnime { mal_id: u64 }
#[derive(Deserialize)]
struct MbSearch { releases: Vec<MbRelease> }
#[derive(Deserialize)]
struct MbRelease { id: String }
