use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct Config {
    pub discord_client_id: String,
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
            music_state: "{artist}".to_string(),
            music_image_text: "{album}".to_string(),
        }
    }
}

impl Config {
    pub fn load() -> Self {
        let path = Self::config_path();
        match std::fs::read_to_string(&path) {
            Ok(contents) => match serde_yml::from_str(&contents) {
                Ok(config) => config,
                Err(e) => {
                    log::error!("Failed to parse {}: {}", path.display(), e);
                    let backup = path.with_extension("yaml.bak");
                    match std::fs::rename(&path, &backup) {
                        Ok(_) => log::warn!("Config backed up to {}", backup.display()),
                        Err(e) => log::warn!("Config backup failed: {}", e),
                    }
                    Config::default()
                }
            },
            Err(e) => {
                let config = Config::default();
                if e.kind() == std::io::ErrorKind::NotFound {
                    let _ = config.save();
                } else {
                    log::warn!("Could not read {}: {}", path.display(), e);
                }
                config
            }
        }
    }

    pub fn save(&self) -> std::io::Result<()> {
        let path = Self::config_path();
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let contents = serde_yml::to_string(self).map_err(std::io::Error::other)?;
        std::fs::write(&path, contents)?;
        // Contains the Plex token
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let _ = std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600));
        }
        Ok(())
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_roundtrip_through_yaml() {
        let original = Config::default();
        let yaml = serde_yml::to_string(&original).unwrap();
        let parsed: Config = serde_yml::from_str(&yaml).unwrap();
        assert_eq!(parsed.discord_client_id, original.discord_client_id);
        assert_eq!(parsed.tv_state, original.tv_state);
        assert_eq!(parsed.show_buttons, original.show_buttons);
        assert_eq!(parsed.plex_token, original.plex_token);
    }

    #[test]
    fn partial_config_fills_in_defaults() {
        let parsed: Config =
            serde_yml::from_str("plex_token: abc123\nenable_music: false\n").unwrap();
        assert_eq!(parsed.plex_token.as_deref(), Some("abc123"));
        assert!(!parsed.enable_music);
        assert!(parsed.enable_movies);
        assert_eq!(
            parsed.discord_client_id,
            Config::default().discord_client_id
        );
        assert_eq!(parsed.movie_details, Config::default().movie_details);
    }

    #[test]
    fn invalid_yaml_fails_to_parse() {
        assert!(serde_yml::from_str::<Config>("plex_token: [unclosed").is_err());
    }
}
