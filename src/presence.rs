use std::collections::HashMap;
use std::collections::hash_map::Entry;
use std::time::Instant;

use crate::config::Config;
use crate::media::{MediaInfo, MediaType, PlaybackState, SessionId};

const DEFAULT_IMAGE: &str = "plex_logo";

#[derive(Debug, Clone)]
pub struct Presence {
    pub details: String,
    pub state: String,
    pub large_image: String,
    pub large_image_text: String,
    pub progress_ms: u64,
    pub duration_ms: u64,
    pub show_timestamps: bool,
    pub activity_type: ActivityType,
    pub playback_state: PlaybackState,
    pub buttons: Vec<Button>,
}

#[derive(Debug, Clone, Copy)]
pub enum ActivityType {
    Watching,
    Listening,
}

#[derive(Debug, Clone)]
pub struct Button {
    pub label: String,
    pub url: String,
}

/// Decides which of possibly several concurrent sessions the single Discord
/// presence should show. Active playback beats paused/buffering; ties go to
/// the most recently started session, so periodic progress updates from one
/// session never steal the slot from another.
#[derive(Default)]
pub struct SessionArbiter {
    sessions: HashMap<SessionId, ActiveSession>,
    seq: u64,
}

struct ActiveSession {
    info: Box<MediaInfo>,
    started: u64,
    updated: Instant,
}

impl SessionArbiter {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn playing(&mut self, id: SessionId, info: Box<MediaInfo>) {
        self.playing_at(id, info, Instant::now());
    }

    fn playing_at(&mut self, id: SessionId, info: Box<MediaInfo>, now: Instant) {
        match self.sessions.entry(id) {
            Entry::Occupied(mut e) => {
                let s = e.get_mut();
                s.info = info;
                s.updated = now;
            }
            Entry::Vacant(e) => {
                self.seq += 1;
                e.insert(ActiveSession {
                    info,
                    started: self.seq,
                    updated: now,
                });
            }
        }
    }

    pub fn stopped(&mut self, id: &SessionId) {
        self.sessions.remove(id);
    }

    pub fn server_gone(&mut self, server: &str) {
        self.sessions.retain(|id, _| id.server != server);
    }

    pub fn current(&self) -> Option<Current<'_>> {
        self.sessions
            .iter()
            .max_by_key(|(_, s)| (state_rank(s.info.state), s.started))
            .map(|(id, session)| Current { id, session })
    }
}

pub struct Current<'a> {
    pub id: &'a SessionId,
    session: &'a ActiveSession,
}

impl Current<'_> {
    pub fn info(&self) -> &MediaInfo {
        &self.session.info
    }

    /// The playback position advanced by the time elapsed since the last
    /// update, so a re-asserted presence (e.g. after Discord restarts) does
    /// not rewind to a stale offset.
    pub fn extrapolated(&self) -> MediaInfo {
        self.extrapolated_at(Instant::now())
    }

    fn extrapolated_at(&self, now: Instant) -> MediaInfo {
        let mut info = (*self.session.info).clone();
        if info.state == PlaybackState::Playing {
            let elapsed = now
                .saturating_duration_since(self.session.updated)
                .as_millis() as u64;
            info.view_offset_ms = info.view_offset_ms.saturating_add(elapsed);
            if info.duration_ms > 0 {
                info.view_offset_ms = info.view_offset_ms.min(info.duration_ms);
            }
        }
        info
    }
}

fn state_rank(state: PlaybackState) -> u8 {
    match state {
        PlaybackState::Playing => 2,
        PlaybackState::Buffering => 1,
        PlaybackState::Paused => 0,
    }
}

pub fn build_presence(info: &MediaInfo, config: &Config) -> Presence {
    let tpl = config.templates(info.media_type);

    let mut buttons = Vec::new();
    if config.show_buttons {
        if let Some(ref id) = info.mal_id {
            buttons.push(Button {
                label: "View on MyAnimeList".into(),
                url: format!("https://myanimelist.net/anime/{}", id),
            });
        }
        if let Some(ref id) = info.imdb_id {
            buttons.push(Button {
                label: "View on IMDb".into(),
                url: format!("https://www.imdb.com/title/{}", id),
            });
        }
    }

    Presence {
        details: format_template(tpl.details, info),
        state: format_template(tpl.state, info),
        large_image: if config.show_artwork {
            info.art_url.clone().unwrap_or_else(|| DEFAULT_IMAGE.into())
        } else {
            DEFAULT_IMAGE.into()
        },
        large_image_text: format_template(tpl.image_text, info),
        progress_ms: info.view_offset_ms,
        duration_ms: info.duration_ms,
        show_timestamps: config.show_progress,
        activity_type: if info.media_type == MediaType::Track {
            ActivityType::Listening
        } else {
            ActivityType::Watching
        },
        playback_state: info.state,
        buttons,
    }
}

fn format_template(template: &str, info: &MediaInfo) -> String {
    let mut result = String::with_capacity(template.len() + 32);
    let mut chars = template.chars().peekable();

    while let Some(c) = chars.next() {
        if c == '{' && chars.peek() == Some(&'{') {
            chars.next();
            result.push('{');
        } else if c == '}' && chars.peek() == Some(&'}') {
            chars.next();
            result.push('}');
        } else if c == '{' {
            let mut name = String::new();
            let mut closed = false;
            for ch in chars.by_ref() {
                if ch == '}' {
                    closed = true;
                    break;
                }
                name.push(ch);
            }
            if !closed {
                result.push('{');
                result.push_str(&name);
                break;
            }
            match placeholder_value(&name, info) {
                Some(value) => result.push_str(&value),
                None => {
                    result.push('{');
                    result.push_str(&name);
                    result.push('}');
                }
            }
        } else {
            result.push(c);
        }
    }
    result
}

/// `None` means "not a placeholder we know"; a known placeholder with no
/// data renders as an empty string.
fn placeholder_value(name: &str, info: &MediaInfo) -> Option<String> {
    let value = match name {
        "show" => info.show_name.clone().unwrap_or_default(),
        "title" => info.title.clone(),
        "se" => match (info.season, info.episode) {
            (Some(s), Some(e)) => format!("S{s:02}E{e:02}"),
            _ => String::new(),
        },
        "season" => info.season.map(|s| s.to_string()).unwrap_or_default(),
        "episode" => info.episode.map(|e| e.to_string()).unwrap_or_default(),
        "year" => info.year.map(|y| y.to_string()).unwrap_or_default(),
        "genres" => info.genres.join(", "),
        "artist" => info.artist.clone().unwrap_or_default(),
        "album" => info.album.clone().unwrap_or_default(),
        _ => return None,
    };
    Some(value)
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
        assert_eq!(
            format_template("{year} [{genres}]", &info),
            "2020 [Drama, Comedy]"
        );
    }

    #[test]
    fn se_placeholder_is_zero_padded() {
        let info = episode_info();
        assert_eq!(format_template("{se}", &info), "S01E02");
    }

    #[test]
    fn missing_values_render_empty() {
        let info = MediaInfo::test_stub(MediaType::Movie);
        assert_eq!(
            format_template("{show}{season}{episode}{year}{se}", &info),
            ""
        );
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
        assert_eq!(
            format_template("{{title}} = {title}", &info),
            "{title} = Pilot"
        );
    }

    #[test]
    fn unterminated_placeholder_is_preserved() {
        let info = episode_info();
        assert_eq!(format_template("oops {title", &info), "oops {title");
    }

    #[test]
    fn build_presence_orders_buttons() {
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
        let config = Config {
            show_buttons: false,
            ..Config::default()
        };
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

    fn session(server: &str, key: &str) -> SessionId {
        SessionId {
            server: server.into(),
            key: key.into(),
        }
    }

    fn info_with_state(state: PlaybackState) -> Box<MediaInfo> {
        let mut info = MediaInfo::test_stub(MediaType::Movie);
        info.state = state;
        Box::new(info)
    }

    #[test]
    fn arbiter_is_empty_by_default() {
        assert!(SessionArbiter::new().current().is_none());
    }

    #[test]
    fn arbiter_prefers_playing_over_paused() {
        let mut arb = SessionArbiter::new();
        arb.playing(session("a", "1"), info_with_state(PlaybackState::Paused));
        arb.playing(session("b", "2"), info_with_state(PlaybackState::Playing));
        assert_eq!(arb.current().unwrap().id, &session("b", "2"));
        // ...even when the paused session updates afterwards
        arb.playing(session("a", "1"), info_with_state(PlaybackState::Paused));
        assert_eq!(arb.current().unwrap().id, &session("b", "2"));
    }

    #[test]
    fn arbiter_updates_do_not_steal_the_slot_between_playing_sessions() {
        let mut arb = SessionArbiter::new();
        arb.playing(session("a", "1"), info_with_state(PlaybackState::Playing));
        arb.playing(session("b", "2"), info_with_state(PlaybackState::Playing));
        assert_eq!(arb.current().unwrap().id, &session("b", "2"));
        // A progress update for the older session must not flip the selection
        arb.playing(session("a", "1"), info_with_state(PlaybackState::Playing));
        assert_eq!(arb.current().unwrap().id, &session("b", "2"));
    }

    #[test]
    fn arbiter_falls_back_when_the_owner_stops_or_pauses() {
        let mut arb = SessionArbiter::new();
        arb.playing(session("a", "1"), info_with_state(PlaybackState::Playing));
        arb.playing(session("b", "2"), info_with_state(PlaybackState::Playing));
        arb.stopped(&session("b", "2"));
        assert_eq!(arb.current().unwrap().id, &session("a", "1"));
        arb.playing(session("a", "1"), info_with_state(PlaybackState::Paused));
        arb.playing(session("b", "3"), info_with_state(PlaybackState::Playing));
        assert_eq!(arb.current().unwrap().id, &session("b", "3"));
        arb.stopped(&session("b", "3"));
        assert_eq!(arb.current().unwrap().id, &session("a", "1"));
        arb.stopped(&session("a", "1"));
        assert!(arb.current().is_none());
    }

    #[test]
    fn arbiter_server_gone_purges_only_that_server() {
        let mut arb = SessionArbiter::new();
        arb.playing(session("a", "1"), info_with_state(PlaybackState::Playing));
        arb.playing(session("a", "2"), info_with_state(PlaybackState::Paused));
        arb.playing(session("b", "1"), info_with_state(PlaybackState::Paused));
        arb.server_gone("a");
        assert_eq!(arb.current().unwrap().id, &session("b", "1"));
        arb.server_gone("b");
        assert!(arb.current().is_none());
    }

    #[test]
    fn arbiter_ignores_stop_for_unknown_session() {
        let mut arb = SessionArbiter::new();
        arb.playing(session("a", "1"), info_with_state(PlaybackState::Playing));
        arb.stopped(&session("a", "other"));
        arb.stopped(&session("b", "1"));
        assert!(arb.current().is_some());
    }

    #[test]
    fn arbiter_extrapolates_offset_only_while_playing() {
        let mut arb = SessionArbiter::new();
        let now = Instant::now();
        let mut info = info_with_state(PlaybackState::Playing);
        info.view_offset_ms = 10_000;
        info.duration_ms = 60_000;
        arb.playing_at(session("a", "1"), info, now);
        let at = |arb: &SessionArbiter, secs| {
            arb.current()
                .unwrap()
                .extrapolated_at(now + std::time::Duration::from_secs(secs))
                .view_offset_ms
        };
        assert_eq!(at(&arb, 20), 30_000);
        // Extrapolation never runs past the end of the item
        assert_eq!(at(&arb, 600), 60_000);

        let mut paused = info_with_state(PlaybackState::Paused);
        paused.view_offset_ms = 10_000;
        arb.playing_at(session("a", "1"), paused, now);
        assert_eq!(at(&arb, 20), 10_000);
    }

    #[test]
    fn artwork_toggle_falls_back_to_default_image() {
        let mut info = episode_info();
        info.art_url = Some("https://img.example/x.jpg".into());
        let p = build_presence(&info, &Config::default());
        assert_eq!(p.large_image, "https://img.example/x.jpg");
        let config = Config {
            show_artwork: false,
            ..Config::default()
        };
        let p = build_presence(&info, &config);
        assert_eq!(p.large_image, DEFAULT_IMAGE);
    }
}
