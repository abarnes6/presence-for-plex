use log::{info, warn};
use percent_encoding::{NON_ALPHANUMERIC, utf8_percent_encode};
use reqwest::Client;
use serde::Deserialize;
use std::collections::HashMap;
use std::sync::RwLock;
use std::time::{Duration, Instant};

use crate::error::NetError;
use crate::media::{MediaInfo, MediaType};

const TMDB_API: &str = "https://api.themoviedb.org/3";
const TMDB_IMAGE_BASE: &str = "https://image.tmdb.org/t/p/w500";
const JIKAN_API: &str = "https://api.jikan.moe/v4/anime";
const MUSICBRAINZ_API: &str = "https://musicbrainz.org/ws/2";
const COVERART_API: &str = "https://coverartarchive.org";
const MUSICBRAINZ_USER_AGENT: &str = concat!(
    "PresenceForPlex/",
    env!("CARGO_PKG_VERSION"),
    " (https://github.com/abarnes6/presence-for-plex)"
);
const DEFAULT_TMDB_TOKEN: &str = "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiIzNmMxOTI3ZjllMTlkMzUxZWFmMjAxNGViN2JmYjNkZiIsIm5iZiI6MTc0NTQzMTA3NC4yMjcsInN1YiI6IjY4MDkyYTIyNmUxYTc2OWU4MWVmMGJhOSIsInNjb3BlcyI6WyJhcGlfcmVhZCJdLCJ2ZXJzaW9uIjoxfQ.Td6eAbW7SgQOMmQpRDwVM-_3KIMybGRqWNK8Yqw1Zzs";
const CACHE_TTL: Duration = Duration::from_secs(28800);
const CACHE_CLEANUP_THRESHOLD: usize = 100;

struct CacheEntry {
    value: Option<String>,
    timestamp: Instant,
}

impl CacheEntry {
    fn is_fresh(&self) -> bool {
        self.timestamp.elapsed() < CACHE_TTL
    }
}

#[derive(Debug, PartialEq)]
enum Lookup {
    Found(String),
    KnownMiss,
    Absent,
}

struct Cache(RwLock<HashMap<String, CacheEntry>>);

impl Cache {
    fn new() -> Self {
        Self(RwLock::new(HashMap::new()))
    }

    fn get(&self, key: &str) -> Lookup {
        let map = self.0.read().unwrap();
        let Some(entry) = map.get(key).filter(|e| e.is_fresh()) else {
            return Lookup::Absent;
        };
        match entry.value.clone() {
            Some(url) => Lookup::Found(url),
            None => Lookup::KnownMiss,
        }
    }

    fn insert(&self, key: &str, value: Option<String>) {
        self.0.write().unwrap().insert(
            key.to_string(),
            CacheEntry {
                value,
                timestamp: Instant::now(),
            },
        );
    }

    fn record_miss(&self, key: &str) {
        self.insert(key, None);
    }

    fn prune(&self) {
        if self.0.read().unwrap().len() < CACHE_CLEANUP_THRESHOLD {
            return;
        }
        let mut entries = self.0.write().unwrap();
        entries.retain(|_, e| e.is_fresh());
        if entries.len() >= CACHE_CLEANUP_THRESHOLD {
            evict_older_half(&mut entries);
        }
    }

    #[cfg(test)]
    fn len(&self) -> usize {
        self.0.read().unwrap().len()
    }
}

fn evict_older_half(entries: &mut HashMap<String, CacheEntry>) {
    let mut stamps: Vec<Instant> = entries.values().map(|e| e.timestamp).collect();
    stamps.sort();
    let cutoff = stamps[stamps.len() / 2];
    entries.retain(|_, e| e.timestamp > cutoff);
}

pub struct MetadataEnricher {
    client: Client,
    tmdb_token: String,
    art_cache: Cache,
    mal_cache: Cache,
}

impl MetadataEnricher {
    pub fn new(tmdb_token: Option<String>) -> Self {
        Self {
            client: Client::builder()
                .user_agent("PresenceForPlex/1.0")
                .timeout(Duration::from_secs(10))
                .build()
                .expect("HTTP client"),
            tmdb_token: tmdb_token.unwrap_or_else(|| DEFAULT_TMDB_TOKEN.to_string()),
            art_cache: Cache::new(),
            mal_cache: Cache::new(),
        }
    }

    pub async fn enrich(&self, info: &mut MediaInfo) {
        self.art_cache.prune();
        self.mal_cache.prune();

        let key = cache_key(info);
        match self.art_cache.get(&key) {
            Lookup::Found(url) => info.art_url = Some(url),
            Lookup::KnownMiss => {}
            Lookup::Absent if info.media_type == MediaType::Track => {
                self.try_musicbrainz(info, &key).await;
            }
            Lookup::Absent => self.try_tmdb(info, &key).await,
        }

        // Plex's "Animation" genre covers Western cartoons too; only the
        // explicit Anime tag counts
        let has_anime_genre = info.genres.iter().any(|g| g.eq_ignore_ascii_case("anime"));
        if has_anime_genre && info.media_type != MediaType::Track {
            self.fetch_mal_id(info).await;
        }
    }

    async fn try_tmdb(&self, info: &mut MediaInfo, key: &str) {
        let Some(ref tmdb_id) = info.tmdb_id else {
            return;
        };

        let result = match info.media_type {
            MediaType::Movie => {
                self.fetch_tmdb_images(&format!("/movie/{}/images", tmdb_id))
                    .await
            }
            MediaType::Episode => {
                let season = info.season.unwrap_or(1);
                match self
                    .fetch_tmdb_images(&format!("/tv/{}/season/{}/images", tmdb_id, season))
                    .await
                {
                    Ok(None) => {
                        self.fetch_tmdb_images(&format!("/tv/{}/images", tmdb_id))
                            .await
                    }
                    season_art => season_art,
                }
            }
            MediaType::Track => return,
        };

        match result {
            Ok(result) => {
                self.art_cache.insert(key, result.clone());
                if let Some(url) = result {
                    info!("TMDB artwork: {}", url);
                    info.art_url = Some(url);
                }
            }
            Err(e) => warn!("TMDB artwork fetch failed for {} (not cached): {}", key, e),
        }
    }

    async fn fetch_tmdb_images(&self, path: &str) -> Result<Option<String>, NetError> {
        let resp = self
            .client
            .get(format!("{}{}", TMDB_API, path))
            .header("Authorization", format!("Bearer {}", self.tmdb_token))
            .send()
            .await?;
        if resp.status() == reqwest::StatusCode::NOT_FOUND {
            return Ok(None);
        }
        let images: TmdbImages = resp.error_for_status()?.json().await?;
        Ok(images
            .posters
            .first()
            .or(images.backdrops.first())
            .map(|i| format!("{}{}", TMDB_IMAGE_BASE, i.file_path)))
    }

    async fn fetch_mal_id(&self, info: &mut MediaInfo) {
        let title = info.show_name.as_ref().unwrap_or(&info.title);
        let cache_key = format!("{}_{}", title, info.year.unwrap_or(0));

        match self.mal_cache.get(&cache_key) {
            Lookup::Found(id) => {
                info.mal_id = Some(id);
                return;
            }
            Lookup::KnownMiss => return,
            Lookup::Absent => {}
        }

        match self.search_jikan(title).await {
            Ok(mal_id) => {
                self.mal_cache.insert(&cache_key, mal_id.clone());
                if let Some(id) = mal_id {
                    info!("MAL ID: {}", id);
                    info.mal_id = Some(id);
                }
            }
            Err(e) => warn!("MAL lookup failed for {} (not cached): {}", title, e),
        }
    }

    async fn search_jikan(&self, title: &str) -> Result<Option<String>, NetError> {
        let url = format!(
            "{}?q={}&limit=1",
            JIKAN_API,
            utf8_percent_encode(title, NON_ALPHANUMERIC)
        );
        let resp = self.client.get(url).send().await?;
        let data: JikanResponse = resp.error_for_status()?.json().await?;
        Ok(data.data.first().map(|a| a.mal_id.to_string()))
    }

    async fn search_musicbrainz(
        &self,
        artist: &str,
        album: &str,
    ) -> Result<Option<String>, NetError> {
        let query = format!(
            "artist:\"{}\" AND release:\"{}\"",
            artist.replace('"', ""),
            album.replace('"', "")
        );
        let resp = self
            .client
            .get(format!(
                "{}/release?query={}&fmt=json&limit=1",
                MUSICBRAINZ_API,
                utf8_percent_encode(&query, NON_ALPHANUMERIC)
            ))
            .header("User-Agent", MUSICBRAINZ_USER_AGENT)
            .send()
            .await?;
        let data: MbSearch = resp.error_for_status()?.json().await?;
        Ok(data.releases.first().map(|rel| rel.id.clone()))
    }

    async fn try_musicbrainz(&self, info: &mut MediaInfo, key: &str) {
        let (Some(artist), Some(album)) = (&info.artist, &info.album) else {
            self.art_cache.record_miss(key);
            return;
        };

        let mbid = match self.search_musicbrainz(artist, album).await {
            Ok(Some(mbid)) => mbid,
            Ok(None) => {
                self.art_cache.record_miss(key);
                return;
            }
            Err(e) => {
                warn!("MusicBrainz search failed for {} (not cached): {}", key, e);
                return;
            }
        };
        let cover_url = format!("{}/release/{}/front", COVERART_API, mbid);

        match self
            .client
            .head(&cover_url)
            .header("User-Agent", MUSICBRAINZ_USER_AGENT)
            .send()
            .await
        {
            Ok(r) if r.status().is_success() || r.status().is_redirection() => {
                self.art_cache.insert(key, Some(cover_url.clone()));
                info!("MusicBrainz artwork: {}", cover_url);
                info.art_url = Some(cover_url);
            }
            Ok(r) if r.status() == reqwest::StatusCode::NOT_FOUND => {
                self.art_cache.record_miss(key);
            }
            Ok(r) => warn!(
                "Cover art check for {} got {} (not cached)",
                key,
                r.status()
            ),
            Err(e) => warn!("Cover art check failed for {} (not cached): {}", key, e),
        }
    }
}

fn cache_key(info: &MediaInfo) -> String {
    match info.media_type {
        MediaType::Track => format!(
            "mb:{}:{}",
            info.artist.as_deref().unwrap_or(""),
            info.album.as_deref().unwrap_or("")
        ),
        MediaType::Episode => match &info.tmdb_id {
            Some(id) => format!("tmdb:{}:s{}", id, info.season.unwrap_or(1)),
            None => format!(
                "title:{}:s{}",
                info.show_name.as_ref().unwrap_or(&info.title),
                info.season.unwrap_or(1)
            ),
        },
        MediaType::Movie => match &info.tmdb_id {
            Some(id) => format!("tmdb:{}", id),
            None => format!("title:{}:{}", info.title, info.year.unwrap_or(0)),
        },
    }
}

#[derive(Deserialize)]
struct TmdbImages {
    #[serde(default)]
    posters: Vec<TmdbImage>,
    #[serde(default)]
    backdrops: Vec<TmdbImage>,
}
#[derive(Deserialize)]
struct TmdbImage {
    file_path: String,
}
#[derive(Deserialize)]
struct JikanResponse {
    #[serde(default)]
    data: Vec<JikanAnime>,
}
#[derive(Deserialize)]
struct JikanAnime {
    mal_id: u64,
}
#[derive(Deserialize)]
struct MbSearch {
    #[serde(default)]
    releases: Vec<MbRelease>,
}
#[derive(Deserialize)]
struct MbRelease {
    id: String,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cache_key_for_track_uses_artist_and_album() {
        let mut info = MediaInfo::test_stub(MediaType::Track);
        info.artist = Some("Artist".into());
        info.album = Some("Album".into());
        assert_eq!(cache_key(&info), "mb:Artist:Album");
    }

    #[test]
    fn cache_key_for_episode_prefers_tmdb_id_and_season() {
        let mut info = MediaInfo::test_stub(MediaType::Episode);
        info.tmdb_id = Some("42".into());
        info.season = Some(3);
        assert_eq!(cache_key(&info), "tmdb:42:s3");
    }

    #[test]
    fn cache_key_for_episode_falls_back_to_show_name() {
        let mut info = MediaInfo::test_stub(MediaType::Episode);
        info.show_name = Some("Some Show".into());
        assert_eq!(cache_key(&info), "title:Some Show:s1");
    }

    #[test]
    fn cache_key_for_movie_falls_back_to_title_and_year() {
        let mut info = MediaInfo::test_stub(MediaType::Movie);
        info.title = "Some Movie".into();
        info.year = Some(1999);
        assert_eq!(cache_key(&info), "title:Some Movie:1999");
    }

    #[test]
    fn cache_distinguishes_misses_from_absent_entries() {
        let cache = Cache::new();
        cache.insert("k", Some("url".into()));
        assert_eq!(cache.get("k"), Lookup::Found("url".into()));
        cache.insert("miss", None);
        assert_eq!(cache.get("miss"), Lookup::KnownMiss);
        assert_eq!(cache.get("absent"), Lookup::Absent);
    }

    #[test]
    fn prune_caps_cache_size_even_when_entries_are_fresh() {
        let cache = Cache::new();
        for i in 0..CACHE_CLEANUP_THRESHOLD {
            cache.insert(&format!("k{}", i), None);
        }
        cache.prune();
        assert!(cache.len() < CACHE_CLEANUP_THRESHOLD);
    }
}
