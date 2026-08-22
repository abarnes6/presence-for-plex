/// Identifies one playback session so updates and stops can be matched up:
/// a stop may only clear the presence it belongs to.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct SessionId {
    pub server: String,
    pub key: String,
}

#[derive(Debug, Clone)]
pub enum MediaUpdate {
    Playing(SessionId, Box<MediaInfo>),
    Stopped(SessionId),
    /// A monitoring task for this server ended without the chance to send
    /// individual stops (cancellation, reauth); its sessions are unknowable.
    ServerGone(String),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AppStatus {
    Idle,
    Playing,
    Paused,
    Buffering,
    NotAuthenticated,
}

impl From<PlaybackState> for AppStatus {
    fn from(s: PlaybackState) -> Self {
        match s {
            PlaybackState::Playing => Self::Playing,
            PlaybackState::Paused => Self::Paused,
            PlaybackState::Buffering => Self::Buffering,
        }
    }
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
    // Plex library keys for follow-up metadata requests
    pub grandparent_key: Option<String>,
    pub key: Option<String>,
}

#[cfg(test)]
impl MediaInfo {
    pub fn test_stub(media_type: MediaType) -> Self {
        Self {
            title: "Title".into(),
            media_type,
            show_name: None,
            season: None,
            episode: None,
            artist: None,
            album: None,
            year: None,
            genres: Vec::new(),
            duration_ms: 0,
            view_offset_ms: 0,
            state: PlaybackState::Playing,
            imdb_id: None,
            tmdb_id: None,
            mal_id: None,
            art_url: None,
            rating_key: None,
            grandparent_key: None,
            key: None,
        }
    }
}
