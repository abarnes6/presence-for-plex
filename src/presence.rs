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
        if let Some(ref id) = info.imdb_id && buttons.len() < 2 { buttons.push(Button { label: "View on IMDb".into(), url: format!("https://www.imdb.com/title/{}", id) }); }
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
            let mut placeholder = String::new();
            let mut closed = false;
            for ch in chars.by_ref() {
                if ch == '}' { closed = true; break; }
                placeholder.push(ch);
            }
            if !closed {
                // Unterminated placeholder, emit verbatim
                result.push('{');
                result.push_str(&placeholder);
                break;
            }
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

#[cfg(test)]
mod tests {
    use super::*;

    fn episode_info() -> MediaInfo {
        let mut info = MediaInfo::test_stub(MediaType::Episode);
        info.title = "Pilot".into();
        info.show_name = Some("The Show".into());
        info.season = Some(1);
        info.episode = Some(2);
        info.year = Some(2020);
        info.genres = vec!["Drama".into(), "Comedy".into()];
        info
    }

    #[test]
    fn replaces_known_placeholders() {
        let info = episode_info();
        assert_eq!(format_template("{show}: {title}", &info), "The Show: Pilot");
        assert_eq!(format_template("S{season} · E{episode}", &info), "S1 · E2");
        assert_eq!(format_template("{year} [{genres}]", &info), "2020 [Drama, Comedy]");
    }

    #[test]
    fn se_placeholder_is_zero_padded() {
        let info = episode_info();
        assert_eq!(format_template("{se}", &info), "S01E02");
    }

    #[test]
    fn missing_values_render_empty() {
        let info = MediaInfo::test_stub(MediaType::Movie);
        assert_eq!(format_template("{show}{season}{episode}{year}{se}", &info), "");
        assert_eq!(format_template("{artist} - {album}", &info), " - ");
    }

    #[test]
    fn unknown_placeholders_are_preserved() {
        let info = episode_info();
        assert_eq!(format_template("{nope} {title}", &info), "{nope} Pilot");
    }

    #[test]
    fn escaped_braces_are_literal() {
        let info = episode_info();
        assert_eq!(format_template("{{title}} = {title}", &info), "{title} = Pilot");
    }

    #[test]
    fn unterminated_placeholder_is_preserved() {
        let info = episode_info();
        assert_eq!(format_template("oops {title", &info), "oops {title");
    }

    #[test]
    fn build_presence_orders_and_caps_buttons() {
        let mut info = episode_info();
        info.mal_id = Some("100".into());
        info.imdb_id = Some("tt1".into());
        let p = build_presence(&info, &Config::default());
        assert_eq!(p.buttons.len(), 2);
        assert!(p.buttons[0].url.contains("myanimelist.net/anime/100"));
        assert!(p.buttons[1].url.contains("imdb.com/title/tt1"));
    }

    #[test]
    fn build_presence_respects_show_buttons_toggle() {
        let mut info = episode_info();
        info.imdb_id = Some("tt1".into());
        let config = Config { show_buttons: false, ..Config::default() };
        assert!(build_presence(&info, &config).buttons.is_empty());
    }

    #[test]
    fn tracks_use_listening_activity() {
        let info = MediaInfo::test_stub(MediaType::Track);
        let p = build_presence(&info, &Config::default());
        assert!(matches!(p.activity_type, ActivityType::Listening));
        let p = build_presence(&episode_info(), &Config::default());
        assert!(matches!(p.activity_type, ActivityType::Watching));
    }

    #[test]
    fn artwork_toggle_falls_back_to_default_image() {
        let mut info = episode_info();
        info.art_url = Some("https://img.example/x.jpg".into());
        let p = build_presence(&info, &Config::default());
        assert_eq!(p.large_image.as_deref(), Some("https://img.example/x.jpg"));
        let config = Config { show_artwork: false, ..Config::default() };
        let p = build_presence(&info, &config);
        assert_eq!(p.large_image.as_deref(), Some(DEFAULT_IMAGE));
    }
}
