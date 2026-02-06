use crate::config::Config;
use crate::discord::{ActivityType, Button, Presence};
use crate::plex_server::{MediaInfo, MediaType};

const DEFAULT_IMAGE: &str = "plex_logo";

pub fn build_presence(info: &MediaInfo, config: &Config) -> Presence {
    let (details_tpl, state_tpl, image_tpl) = match info.media_type {
        MediaType::Episode => (&config.tv_details, &config.tv_state, &config.tv_image_text),
        MediaType::Movie => (&config.movie_details, &config.movie_state, &config.movie_image_text),
        MediaType::Track => (&config.music_details, &config.music_state, &config.music_image_text),
    };

    let mut buttons = Vec::new();
    if config.show_buttons {
        if let Some(ref id) = info.mal_id { buttons.push(Button { label: "View on MyAnimeList".into(), url: format!("https://myanimelist.net/anime/{}", id) }); }
        if let Some(ref id) = info.imdb_id { if buttons.len() < 2 { buttons.push(Button { label: "View on IMDb".into(), url: format!("https://www.imdb.com/title/{}", id) }); } }
    }

    Presence {
        details: format_template(details_tpl, info),
        state: format_template(state_tpl, info),
        large_image: Some(if config.show_artwork { info.art_url.clone().unwrap_or_else(|| DEFAULT_IMAGE.into()) } else { DEFAULT_IMAGE.into() }),
        large_image_text: format_template(image_tpl, info),
        progress_ms: info.view_offset_ms,
        duration_ms: info.duration_ms,
        show_timestamps: config.show_progress,
        activity_type: if info.media_type == MediaType::Track { ActivityType::Listening } else { ActivityType::Watching },
        playback_state: info.state,
        buttons,
    }
}

fn format_template(template: &str, info: &MediaInfo) -> String {
    let mut result = String::with_capacity(template.len() + 32);
    let mut chars = template.chars().peekable();

    while let Some(c) = chars.next() {
        if c == '{' {
            if chars.peek() == Some(&'{') { chars.next(); result.push('{'); continue; }
            let placeholder: String = chars.by_ref().take_while(|&ch| ch != '}').collect();
            match placeholder.as_str() {
                "show" => result.push_str(info.show_name.as_deref().unwrap_or("")),
                "title" => result.push_str(&info.title),
                "se" => if let (Some(s), Some(e)) = (info.season, info.episode) { result.push_str(&format!("S{s:02}E{e:02}")); },
                "season" => if let Some(s) = info.season { result.push_str(&s.to_string()); },
                "episode" => if let Some(e) = info.episode { result.push_str(&e.to_string()); },
                "year" => if let Some(y) = info.year { result.push_str(&y.to_string()); },
                "genres" => result.push_str(&info.genres.join(", ")),
                "artist" => result.push_str(info.artist.as_deref().unwrap_or("")),
                "album" => result.push_str(info.album.as_deref().unwrap_or("")),
                _ => { result.push('{'); result.push_str(&placeholder); result.push('}'); }
            }
        } else if c == '}' && chars.peek() == Some(&'}') { chars.next(); result.push('}'); }
        else { result.push(c); }
    }
    result
}
