use log::{debug, info};
use reqwest::Client;
use std::collections::HashMap;
use std::time::{Duration, Instant};
use tokio::sync::RwLock;
use serde::Deserialize;

use crate::plex_server::{MediaInfo, MediaType};

const TMDB_API: &str = "https://api.themoviedb.org/3";
const TMDB_IMAGE_BASE: &str = "https://image.tmdb.org/t/p/w500";
const JIKAN_API: &str = "https://api.jikan.moe/v4/anime";
const MUSICBRAINZ_API: &str = "https://musicbrainz.org/ws/2";
const COVERART_API: &str = "https://coverartarchive.org";
const DEFAULT_TMDB_TOKEN: &str = "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiIzNmMxOTI3ZjllMTlkMzUxZWFmMjAxNGViN2JmYjNkZiIsIm5iZiI6MTc0NTQzMTA3NC4yMjcsInN1YiI6IjY4MDkyYTIyNmUxYTc2OWU4MWVmMGJhOSIsInNjb3BlcyI6WyJhcGlfcmVhZCJdLCJ2ZXJzaW9uIjoxfQ.Td6eAbW7SgQOMmQpRDwVM-_3KIMybGRqWNK8Yqw1Zzs";

const CACHE_TTL_SECS: u64 = 3600;
const CACHE_CLEANUP_THRESHOLD: usize = 100;

#[derive(Clone)]
struct CacheEntry {
    value: Option<CachedArtwork>,
    timestamp: Instant,
}

#[derive(Clone)]
pub struct CachedArtwork {
    pub art_url: String,
    pub mal_id: Option<String>,
}

pub struct MetadataEnricher {
    client: Client,
    tmdb_token: String,
    cache: RwLock<HashMap<String, CacheEntry>>,
}

impl MetadataEnricher {
    pub fn new(tmdb_token: Option<String>) -> Self {
        debug!("Creating MetadataEnricher (custom TMDB token: {})", tmdb_token.is_some());
        let client = Client::builder()
            .user_agent("PresenceForPlex/1.0")
            .timeout(Duration::from_secs(10))
            .build()
            .expect("Failed to create HTTP client");

        Self {
            client,
            tmdb_token: tmdb_token.unwrap_or_else(|| DEFAULT_TMDB_TOKEN.to_string()),
            cache: RwLock::new(HashMap::new()),
        }
    }

    pub async fn enrich(&self, info: &mut MediaInfo) {
        let start = std::time::Instant::now();
        debug!(
            "Enriching: {} (tmdb_id: {:?}, genres: {:?})",
            info.title, info.tmdb_id, info.genres
        );

        self.cleanup_cache().await;

        let cache_key = self.build_cache_key(info);

        if let Some(cached) = self.get_cached(&cache_key).await {
            debug!("Cache hit for {} in {:?}", cache_key, start.elapsed());
            if let Some(artwork) = cached {
                info.art_url = Some(artwork.art_url);
                info.mal_id = artwork.mal_id;
            }
            return;
        }

        if info.media_type == MediaType::Track {
            self.try_musicbrainz_artwork(info, &cache_key).await;
            debug!("Enrichment complete in {:?}", start.elapsed());
            return;
        }

        if self.try_tmdb_artwork(info, &cache_key).await {
            debug!("Enrichment complete in {:?}", start.elapsed());
            return;
        }

        self.try_jikan_artwork(info, &cache_key).await;
        debug!("Enrichment complete in {:?}", start.elapsed());
    }

    fn build_cache_key(&self, info: &MediaInfo) -> String {
        match info.media_type {
            MediaType::Track => {
                let artist = info.artist.as_deref().unwrap_or("");
                let album = info.album.as_deref().unwrap_or("");
                format!("musicbrainz:{}:{}", artist, album)
            }
            _ => match &info.tmdb_id {
                Some(tmdb_id) => format!("tmdb:{}:{:?}", tmdb_id, info.media_type),
                None => {
                    let search_title = info.show_name.as_ref().unwrap_or(&info.title);
                    format!("jikan:{}:{:?}", search_title, info.year)
                }
            },
        }
    }

    async fn try_tmdb_artwork(&self, info: &mut MediaInfo, cache_key: &str) -> bool {
        let Some(ref tmdb_id) = info.tmdb_id else {
            debug!("No TMDB ID, skipping TMDB artwork fetch");
            return false;
        };

        let start = std::time::Instant::now();
        debug!("Fetching TMDB artwork for id={}", tmdb_id);
        let result = self.fetch_tmdb_artwork(tmdb_id, &info.media_type).await;
        debug!("TMDB fetch completed in {:?}", start.elapsed());

        self.set_cached(
            cache_key,
            result.as_ref().map(|url| CachedArtwork {
                art_url: url.clone(),
                mal_id: None,
            }),
        )
        .await;

        if let Some(url) = result {
            info!("Got TMDB artwork: {}", url);
            info.art_url = Some(url);
            return true;
        }

        false
    }

    async fn try_jikan_artwork(&self, info: &mut MediaInfo, cache_key: &str) {
        let is_anime = info
            .genres
            .iter()
            .any(|g| matches!(g.to_lowercase().as_str(), "anime" | "animation"));

        if !is_anime {
            debug!("Not anime (genres: {:?}), skipping Jikan", info.genres);
            self.set_cached(cache_key, None).await;
            return;
        }

        let search_title = info.show_name.as_ref().unwrap_or(&info.title);
        debug!("Fetching Jikan artwork for: {}", search_title);
        let result = self.fetch_jikan_artwork(search_title, info.year).await;

        self.set_cached(cache_key, result.clone()).await;

        if let Some(artwork) = result {
            info!("Got Jikan artwork for MAL {:?}: {}", artwork.mal_id, artwork.art_url);
            info.mal_id = artwork.mal_id;
            info.art_url = Some(artwork.art_url);
        }
    }

    async fn try_musicbrainz_artwork(&self, info: &mut MediaInfo, cache_key: &str) {
        let (Some(artist), Some(album)) = (&info.artist, &info.album) else {
            debug!("Missing artist or album, skipping MusicBrainz");
            self.set_cached(cache_key, None).await;
            return;
        };

        debug!("Fetching MusicBrainz artwork for: {} - {}", artist, album);
        let result = self.fetch_musicbrainz_artwork(artist, album).await;

        self.set_cached(
            cache_key,
            result.as_ref().map(|url| CachedArtwork {
                art_url: url.clone(),
                mal_id: None,
            }),
        )
        .await;

        if let Some(url) = result {
            info!("Got MusicBrainz artwork: {}", url);
            info.art_url = Some(url);
        }
    }

    async fn cleanup_cache(&self) {
        let mut cache = self.cache.write().await;
        if cache.len() < CACHE_CLEANUP_THRESHOLD {
            return;
        }
        let ttl = Duration::from_secs(CACHE_TTL_SECS);
        cache.retain(|_, entry| entry.timestamp.elapsed() < ttl);
    }

    async fn get_cached(&self, key: &str) -> Option<Option<CachedArtwork>> {
        let cache = self.cache.read().await;
        let entry = cache.get(key)?;
        let ttl = Duration::from_secs(CACHE_TTL_SECS);

        if entry.timestamp.elapsed() < ttl {
            Some(entry.value.clone())
        } else {
            None
        }
    }

    async fn set_cached(&self, key: &str, value: Option<CachedArtwork>) {
        self.cache.write().await.insert(
            key.to_string(),
            CacheEntry {
                value,
                timestamp: Instant::now(),
            },
        );
    }

    async fn fetch_tmdb_artwork(&self, tmdb_id: &str, media_type: &MediaType) -> Option<String> {
        let media_path = match media_type {
            MediaType::Movie => "movie",
            MediaType::Episode => "tv",
            MediaType::Track => return None,
        };

        let endpoint = format!("{}/{}/{}/images", TMDB_API, media_path, tmdb_id);

        let resp: TmdbImagesResponse = self.client
            .get(&endpoint)
            .header("Authorization", format!("Bearer {}", self.tmdb_token))
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

    async fn fetch_jikan_artwork(&self, title: &str, year: Option<u32>) -> Option<CachedArtwork> {
        debug!("Searching Jikan for: {}", title);

        let mut url = format!("{}?q={}", JIKAN_API, urlencoding::encode(title));

        if let Some(y) = year {
            url.push_str(&format!("&start_date={y}-01-01&end_date={y}-12-31"));
        }

        let resp: JikanResponse = self.client.get(&url).send().await.ok()?.json().await.ok()?;

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

    async fn fetch_musicbrainz_artwork(&self, artist: &str, album: &str) -> Option<String> {
        debug!("Searching MusicBrainz for: {} - {}", artist, album);

        let query = format!(
            "artist:\"{}\" AND release:\"{}\"",
            artist.replace('"', ""),
            album.replace('"', "")
        );

        let search_url = format!(
            "{}/release?query={}&fmt=json&limit=1",
            MUSICBRAINZ_API,
            urlencoding::encode(&query)
        );

        let resp = self.client
            .get(&search_url)
            .header("User-Agent", concat!("PresenceForPlex/", env!("CARGO_PKG_VERSION"), " (https://github.com/abarnes6/presence-for-plex)"))
            .header("Accept", "application/json")
            .send()
            .await
            .ok()?;

        let search_result: MusicBrainzSearchResponse = resp.json().await.ok()?;
        let release = search_result.releases.first()?;
        let mbid = &release.id;

        debug!("Found MusicBrainz release: {}", mbid);

        let cover_url = format!("{}/release/{}/front", COVERART_API, mbid);

        let cover_resp = self.client
            .head(&cover_url)
            .header("User-Agent", concat!("PresenceForPlex/", env!("CARGO_PKG_VERSION"), " (https://github.com/abarnes6/presence-for-plex)"))
            .send()
            .await
            .ok()?;

        if cover_resp.status().is_success() || cover_resp.status().is_redirection() {
            Some(cover_url)
        } else {
            debug!("No cover art found for release {}", mbid);
            None
        }
    }
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

// MusicBrainz response types
#[derive(Deserialize)]
struct MusicBrainzSearchResponse {
    releases: Vec<MusicBrainzRelease>,
}

#[derive(Deserialize)]
struct MusicBrainzRelease {
    id: String,
}
