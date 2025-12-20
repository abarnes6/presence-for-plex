use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct Config {
    pub discord_client_id: String,
    pub discord_enabled: bool,
    pub show_buttons: bool,
    pub show_progress: bool,
    pub show_artwork: bool,

    pub plex_token: Option<String>,
    pub enable_movies: bool,
    pub enable_tv_shows: bool,
    pub enable_music: bool,

    pub tmdb_token: Option<String>,

    // Format templates
    pub tv_details: String,
    pub tv_state: String,
    pub tv_image_text: String,
    pub movie_details: String,
    pub movie_state: String,
    pub movie_image_text: String,
    pub music_details: String,
    pub music_state: String,
    pub music_image_text: String,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            discord_client_id: "1359742002618564618".to_string(),
            discord_enabled: true,
            show_buttons: true,
            show_progress: true,
            show_artwork: true,
            plex_token: None,
            enable_movies: true,
            enable_tv_shows: true,
            enable_music: true,
            tmdb_token: None,
            tv_details: "{show}".to_string(),
            tv_state: "S{season} · E{episode} - {title}".to_string(),
            tv_image_text: "{title}".to_string(),
            movie_details: "{title} ({year})".to_string(),
            movie_state: "{genres}".to_string(),
            movie_image_text: "{title}".to_string(),
            music_details: "{title}".to_string(),
            music_state: "{artist} - {album}".to_string(),
            music_image_text: "{album}".to_string(),
        }
    }
}

impl Config {
    pub fn load() -> Self {
        let path = Self::config_path();
        if path.exists() {
            if let Ok(contents) = std::fs::read_to_string(&path) {
                if let Ok(config) = serde_yaml::from_str(&contents) {
                    return config;
                }
            }
        }
        let config = Config::default();
        let _ = config.save();
        config
    }

    pub fn save(&self) -> std::io::Result<()> {
        let path = Self::config_path();
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let contents = serde_yaml::to_string(self).unwrap_or_default();
        std::fs::write(&path, contents)
    }

    fn config_path() -> PathBuf {
        Self::app_dir().join("config.yaml")
    }

    pub fn log_path() -> PathBuf {
        Self::app_dir().join("presence-for-plex.log")
    }

    pub fn app_dir() -> PathBuf {
        dirs::config_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join("presence-for-plex")
    }
}
