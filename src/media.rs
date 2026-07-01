#[derive(Debug, Clone)]
pub enum MediaUpdate {
    Playing(Box<MediaInfo>),
    Stopped,
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
    pub(crate) grandparent_key: Option<String>,
    pub(crate) key: Option<String>,
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
