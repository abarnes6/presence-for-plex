use log::info;
use percent_encoding::{NON_ALPHANUMERIC, utf8_percent_encode};
use reqwest::Client;
use serde::Deserialize;
use std::collections::HashMap;
use std::sync::RwLock;
use std::time::{Duration, Instant};

use crate::media::{MediaInfo, MediaType};

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

struct Cache(RwLock<HashMap<String, CacheEntry>>);

impl Cache {
    fn new() -> Self {
        Self(RwLock::new(HashMap::new()))
    }

    // Outer None = not cached, inner None = cached miss
    fn get(&self, key: &str) -> Option<Option<String>> {
        self.0
            .read()
            .unwrap()
            .get(key)
            .filter(|e| e.timestamp.elapsed() < CACHE_TTL)
            .map(|e| e.value.clone())
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

    fn prune(&self) {
        if self.0.read().unwrap().len() < CACHE_CLEANUP_THRESHOLD {
            return;
        }
        let mut entries = self.0.write().unwrap();
        entries.retain(|_, e| e.timestamp.elapsed() < CACHE_TTL);
        if entries.len() >= CACHE_CLEANUP_THRESHOLD {
            // Still full, evict the older half
            let mut stamps: Vec<Instant> = entries.values().map(|e| e.timestamp).collect();
            stamps.sort();
            let cutoff = stamps[stamps.len() / 2];
            entries.retain(|_, e| e.timestamp > cutoff);
        }
    }

    #[cfg(test)]
    fn len(&self) -> usize {
        self.0.read().unwrap().len()
    }
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
            Some(Some(url)) => info.art_url = Some(url),
            Some(None) => {}
            None if info.media_type == MediaType::Track => {
                self.try_musicbrainz(info, &key).await;
            }
            None => self.try_tmdb(info, &key).await,
        }

        // For anime, fetch MAL ID for the link ("animation" alone is not anime)
        let is_anime = info.genres.iter().any(|g| g.eq_ignore_ascii_case("anime"));
        if is_anime && info.media_type != MediaType::Track {
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
                    Some(url) => Some(url),
                    None => {
                        self.fetch_tmdb_images(&format!("/tv/{}/images", tmdb_id))
                            .await
                    }
                }
            }
            MediaType::Track => return,
        };

        self.art_cache.insert(key, result.clone());
        if let Some(url) = result {
            info!("TMDB artwork: {}", url);
            info.art_url = Some(url);
        }
    }

    async fn fetch_tmdb_images(&self, path: &str) -> Option<String> {
        let resp = self
            .client
            .get(format!("{}{}", TMDB_API, path))
            .header("Authorization", format!("Bearer {}", self.tmdb_token))
            .send()
            .await
            .ok()?;
        let images: TmdbImages = resp.json().await.ok()?;
        images
            .posters
            .first()
            .or(images.backdrops.first())
            .map(|i| format!("{}{}", TMDB_IMAGE_BASE, i.file_path))
    }

    async fn fetch_mal_id(&self, info: &mut MediaInfo) {
        let title = info.show_name.as_ref().unwrap_or(&info.title);
        let cache_key = format!("{}_{}", title, info.year.unwrap_or(0));

        if let Some(cached) = self.mal_cache.get(&cache_key) {
            info.mal_id = cached;
            return;
        }

        let url = format!(
            "{}?q={}&limit=1",
            JIKAN_API,
            utf8_percent_encode(title, NON_ALPHANUMERIC)
        );

        let mal_id = async {
            let resp = self.client.get(&url).send().await.ok()?;
            let data: JikanResponse = resp.json().await.ok()?;
            Some(data.data.first()?.mal_id.to_string())
        }
        .await;

        self.mal_cache.insert(&cache_key, mal_id.clone());
        if let Some(id) = mal_id {
            info!("MAL ID: {}", id);
            info.mal_id = Some(id);
        }
    }

    async fn try_musicbrainz(&self, info: &mut MediaInfo, key: &str) {
        let (Some(artist), Some(album)) = (&info.artist, &info.album) else {
            self.art_cache.insert(key, None);
            return;
        };
        let query = format!(
            "artist:\"{}\" AND release:\"{}\"",
            artist.replace('"', ""),
            album.replace('"', "")
        );
        let ua = concat!(
            "PresenceForPlex/",
            env!("CARGO_PKG_VERSION"),
            " (https://github.com/abarnes6/presence-for-plex)"
        );

        let mbid = async {
            let resp = self
                .client
                .get(format!(
                    "{}/release?query={}&fmt=json&limit=1",
                    MUSICBRAINZ_API,
                    utf8_percent_encode(&query, NON_ALPHANUMERIC)
                ))
                .header("User-Agent", ua)
                .send()
                .await
                .ok()?;
            let data: MbSearch = resp.json().await.ok()?;
            data.releases.first().map(|rel| rel.id.clone())
        }
        .await;

        let Some(mbid) = mbid else {
            self.art_cache.insert(key, None);
            return;
        };
        let cover_url = format!("{}/release/{}/front", COVERART_API, mbid);

        let exists = self
            .client
            .head(&cover_url)
            .header("User-Agent", ua)
            .send()
            .await
            .map(|r| r.status().is_success() || r.status().is_redirection())
            .unwrap_or(false);

        let result = if exists { Some(cover_url) } else { None };
        self.art_cache.insert(key, result.clone());
        if let Some(url) = result {
            info!("MusicBrainz artwork: {}", url);
            info.art_url = Some(url);
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
        MediaType::Episode => info
            .tmdb_id
            .as_ref()
            .map(|id| format!("tmdb:{}:s{}", id, info.season.unwrap_or(1)))
            .unwrap_or_else(|| {
                format!(
                    "title:{}:s{}",
                    info.show_name.as_ref().unwrap_or(&info.title),
                    info.season.unwrap_or(1)
                )
            }),
        MediaType::Movie => info
            .tmdb_id
            .as_ref()
            .map(|id| format!("tmdb:{}", id))
            .unwrap_or_else(|| format!("title:{}:{}", info.title, info.year.unwrap_or(0))),
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
        assert_eq!(cache.get("k"), Some(Some("url".into())));
        // Negative results are cached too
        cache.insert("miss", None);
        assert_eq!(cache.get("miss"), Some(None));
        assert_eq!(cache.get("absent"), None);
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
