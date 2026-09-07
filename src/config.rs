use serde::{Deserialize, Serialize};
use std::path::PathBuf;

use crate::media::MediaType;

/// Discord application backing the rich presence.
const DISCORD_CLIENT_ID: &str = "1546505850435149985";

/// Applications that no longer exist. Discord answers their handshake with an
/// "Invalid Client ID" close frame, so installs carrying one in their saved
/// config must be moved forward or presence never updates again.
const RETIRED_CLIENT_IDS: [&str; 1] = ["1359742002618564618"];

#[derive(Serialize, Deserialize)]
#[serde(default)]
pub struct Config {
    pub discord_client_id: String,
    pub show_buttons: bool,
    pub show_progress: bool,
    pub show_artwork: bool,

    pub plex_token: Option<String>,
    // Per-install X-Plex-Client-Identifier; generated on first run. Plex ties
    // pin claims to this value, so it must not be a constant shared by every install.
    pub client_identifier: Option<String>,
    pub enable_movies: bool,
    pub enable_tv_shows: bool,
    pub enable_music: bool,

    pub tmdb_token: Option<String>,

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

pub struct Templates<'a> {
    pub details: &'a str,
    pub state: &'a str,
    pub image_text: &'a str,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            discord_client_id: DISCORD_CLIENT_ID.to_string(),
            show_buttons: true,
            show_progress: true,
            show_artwork: true,
            plex_token: None,
            client_identifier: None,
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
            Ok(contents) => match serde_yml::from_str::<Config>(&contents) {
                Ok(mut config) => {
                    if config.retire_dead_client_id() {
                        if let Err(e) = config.save() {
                            log::warn!("Could not persist Discord client id: {}", e);
                        }
                    }
                    config
                }
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

    /// The client id is written into every install's config on first run, so a
    /// new default alone would never reach existing users.
    fn retire_dead_client_id(&mut self) -> bool {
        if !RETIRED_CLIENT_IDS.contains(&self.discord_client_id.as_str()) {
            return false;
        }
        log::info!(
            "Discord application {} was retired; switching to {}",
            self.discord_client_id,
            DISCORD_CLIENT_ID
        );
        self.discord_client_id = DISCORD_CLIENT_ID.to_string();
        true
    }

    pub fn enables(&self, media: MediaType) -> bool {
        match media {
            MediaType::Movie => self.enable_movies,
            MediaType::Episode => self.enable_tv_shows,
            MediaType::Track => self.enable_music,
        }
    }

    pub fn templates(&self, media: MediaType) -> Templates<'_> {
        match media {
            MediaType::Episode => Templates {
                details: &self.tv_details,
                state: &self.tv_state,
                image_text: &self.tv_image_text,
            },
            MediaType::Movie => Templates {
                details: &self.movie_details,
                state: &self.movie_state,
                image_text: &self.movie_image_text,
            },
            MediaType::Track => Templates {
                details: &self.music_details,
                state: &self.music_state,
                image_text: &self.music_image_text,
            },
        }
    }

    /// A fresh identifier is saved immediately so pin auth and API calls
    /// agree on it across restarts.
    pub fn ensure_client_identifier(&mut self) -> String {
        if let Some(ref id) = self.client_identifier {
            return id.clone();
        }
        let id = uuid::Uuid::new_v4().to_string();
        self.client_identifier = Some(id.clone());
        if let Err(e) = self.save() {
            log::warn!("Could not persist client identifier: {}", e);
        }
        id
    }

    pub fn save(&self) -> std::io::Result<()> {
        Self::ensure_app_dir()?;
        let path = Self::config_path();
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

    pub fn ensure_app_dir() -> std::io::Result<PathBuf> {
        let dir = Self::app_dir();
        std::fs::create_dir_all(&dir)?;
        Ok(dir)
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
    fn retired_client_id_is_replaced() {
        let mut config: Config =
            serde_yml::from_str("discord_client_id: '1359742002618564618'\n").unwrap();
        assert!(config.retire_dead_client_id());
        assert_eq!(config.discord_client_id, DISCORD_CLIENT_ID);
    }

    #[test]
    fn current_client_id_is_left_alone() {
        let mut config = Config::default();
        assert!(!config.retire_dead_client_id());
        assert_eq!(config.discord_client_id, DISCORD_CLIENT_ID);
    }

    #[test]
    fn invalid_yaml_fails_to_parse() {
        assert!(serde_yml::from_str::<Config>("plex_token: [unclosed").is_err());
    }
}
