use crate::config::Config;
use crate::discord::{ActivityType, Button, Presence};
use crate::plex::{MediaInfo, MediaType};

const MAX_BUTTONS: usize = 2;
const DEFAULT_IMAGE: &str = "plex_logo";

pub fn build_presence(info: &MediaInfo, config: &Config) -> Presence {
    let template_set = match info.media_type {
        MediaType::Episode => (&config.tv_details, &config.tv_state, &config.tv_image_text),
        MediaType::Movie => (&config.movie_details, &config.movie_state, &config.movie_image_text),
        MediaType::Track => (&config.music_details, &config.music_state, &config.music_image_text),
    };

    let activity_type = match info.media_type {
        MediaType::Track => ActivityType::Listening,
        _ => ActivityType::Watching,
    };

    let large_image = match config.show_artwork {
        true => info.art_url.clone().unwrap_or_else(|| DEFAULT_IMAGE.to_string()),
        false => DEFAULT_IMAGE.to_string(),
    };

    Presence {
        details: format_template(template_set.0, info),
        state: format_template(template_set.1, info),
        large_image: Some(large_image),
        large_image_text: format_template(template_set.2, info),
        progress_ms: info.view_offset_ms,
        duration_ms: info.duration_ms,
        show_timestamps: config.show_progress,
        activity_type,
        playback_state: info.state.clone(),
        buttons: build_buttons(info, config.show_buttons),
    }
}

fn build_buttons(info: &MediaInfo, show_buttons: bool) -> Vec<Button> {
    if !show_buttons {
        return Vec::new();
    }

    let mut buttons = Vec::with_capacity(MAX_BUTTONS);

    if let Some(ref mal_id) = info.mal_id {
        buttons.push(Button {
            label: "View on MyAnimeList".to_string(),
            url: format!("https://myanimelist.net/anime/{}", mal_id),
        });
    }

    if let Some(ref imdb_id) = info.imdb_id {
        if buttons.len() < MAX_BUTTONS {
            buttons.push(Button {
                label: "View on IMDb".to_string(),
                url: format!("https://www.imdb.com/title/{}", imdb_id),
            });
        }
    }

    buttons
}

fn format_template(template: &str, info: &MediaInfo) -> String {
    let mut result = String::with_capacity(template.len() + 32);
    let mut chars = template.chars().peekable();

    while let Some(c) = chars.next() {
        if c == '{' {
            if chars.peek() == Some(&'{') {
                chars.next();
                result.push('{');
                continue;
            }

            let mut placeholder = String::new();
            let mut found_closing = false;
            for ch in chars.by_ref() {
                if ch == '}' {
                    found_closing = true;
                    break;
                }
                placeholder.push(ch);
            }

            if !found_closing {
                result.push('{');
                result.push_str(&placeholder);
                continue;
            }

            let value = match placeholder.as_str() {
                "show" => info.show_name.as_deref().unwrap_or(""),
                "title" => &info.title,
                "se" => {
                    if let (Some(s), Some(e)) = (info.season, info.episode) {
                        use std::fmt::Write;
                        let _ = write!(result, "S{s:02}E{e:02}");
                    }
                    continue;
                }
                "season" => {
                    if let Some(s) = info.season {
                        use std::fmt::Write;
                        let _ = write!(result, "{s}");
                    }
                    continue;
                }
                "episode" => {
                    if let Some(e) = info.episode {
                        use std::fmt::Write;
                        let _ = write!(result, "{e}");
                    }
                    continue;
                }
                "year" => {
                    if let Some(y) = info.year {
                        use std::fmt::Write;
                        let _ = write!(result, "{y}");
                    }
                    continue;
                }
                "genres" => {
                    result.push_str(&info.genres.join(", "));
                    continue;
                }
                "artist" => info.artist.as_deref().unwrap_or(""),
                "album" => info.album.as_deref().unwrap_or(""),
                _ => {
                    result.push('{');
                    result.push_str(&placeholder);
                    result.push('}');
                    continue;
                }
            };
            result.push_str(value);
        } else if c == '}' {
            if chars.peek() == Some(&'}') {
                chars.next();
            }
            result.push('}');
        } else {
            result.push(c);
        }
    }

    result
}
