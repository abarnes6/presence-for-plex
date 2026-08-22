use std::fmt;

/// The one distinction that changes behavior: auth rejections stop retry
/// loops, everything else may retry.
#[derive(Debug)]
pub enum NetError {
    /// 401 only. 403 is deliberately not auth: plex.tv fronting can return it
    /// transiently, and Plex servers use it to mean "shared user".
    Auth,
    Http(reqwest::StatusCode),
    Transport(reqwest::Error),
    Parse(String),
}

impl NetError {
    pub fn is_auth(&self) -> bool {
        matches!(self, Self::Auth)
    }
}

impl fmt::Display for NetError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Auth => write!(f, "authentication rejected (401 Unauthorized)"),
            Self::Http(s) => write!(f, "unexpected HTTP status {s}"),
            Self::Transport(e) => write!(f, "request failed: {e}"),
            Self::Parse(e) => write!(f, "unexpected response: {e}"),
        }
    }
}

impl From<reqwest::Error> for NetError {
    fn from(e: reqwest::Error) -> Self {
        if e.is_decode() {
            return Self::Parse(e.to_string());
        }
        match e.status() {
            Some(s) if s == reqwest::StatusCode::UNAUTHORIZED => Self::Auth,
            Some(s) => Self::Http(s),
            None => Self::Transport(e),
        }
    }
}
