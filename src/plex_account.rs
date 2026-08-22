use log::info;
use reqwest::{Client, Method};
use serde::Deserialize;
use std::time::Duration;

use crate::error::NetError;

const PLEX_API: &str = "https://plex.tv/api/v2";
const TIMEOUT: Duration = Duration::from_secs(10);

pub struct PlexAccount {
    client: Client,
    client_id: String,
    username: Option<String>,
}

pub struct ServerInfo {
    pub name: String,
    pub access_token: Option<String>,
    pub connections: Vec<ServerConnection>,
}

#[derive(Deserialize)]
pub struct ServerConnection {
    pub uri: String,
}

impl PlexAccount {
    pub fn new(client_id: &str) -> Self {
        Self {
            client: Client::builder()
                .user_agent("PresenceForPlex/1.0")
                .timeout(TIMEOUT)
                .build()
                .expect("HTTP client"),
            client_id: client_id.to_string(),
            username: None,
        }
    }

    fn api(&self, method: Method, path: &str) -> reqwest::RequestBuilder {
        self.client
            .request(method, format!("{}{}", PLEX_API, path))
            .header("Accept", "application/json")
            .header("X-Plex-Client-Identifier", &self.client_id)
    }

    pub fn username(&self) -> Option<&str> {
        self.username.as_deref()
    }

    pub async fn fetch_username(&mut self, token: &str) -> Result<String, NetError> {
        let json: serde_json::Value = self
            .api(Method::GET, "/user")
            .header("X-Plex-Token", token)
            .send()
            .await?
            .error_for_status()?
            .json()
            .await?;
        let username = json["username"]
            .as_str()
            .ok_or_else(|| NetError::Parse("no username in /user response".into()))?
            .to_string();
        info!("Logged in as: {}", username);
        self.username = Some(username.clone());
        Ok(username)
    }

    pub async fn request_pin(&self) -> Result<(u64, String), NetError> {
        let json: serde_json::Value = self
            .api(Method::POST, "/pins")
            .header("X-Plex-Product", "Presence for Plex")
            .query(&[("strong", "true")])
            .send()
            .await?
            .error_for_status()?
            .json()
            .await?;
        let id = json["id"]
            .as_u64()
            .ok_or_else(|| NetError::Parse("no id in pin response".into()))?;
        let code = json["code"]
            .as_str()
            .ok_or_else(|| NetError::Parse("no code in pin response".into()))?
            .to_string();
        Ok((id, code))
    }

    /// Polls a pin. `Ok(None)` means the user has not approved it yet.
    /// The pin code is sent along so the claim needs more than a guessable id.
    pub async fn check_pin(&self, pin_id: u64, code: &str) -> Result<Option<String>, NetError> {
        let json: serde_json::Value = self
            .api(Method::GET, &format!("/pins/{}", pin_id))
            .query(&[("code", code)])
            .send()
            .await?
            .error_for_status()?
            .json()
            .await?;
        Ok(json["authToken"]
            .as_str()
            .filter(|s| !s.is_empty())
            .map(String::from))
    }

    pub async fn get_servers(&self, token: &str) -> Result<Vec<ServerInfo>, NetError> {
        let resources: Vec<PlexResource> = self
            .api(Method::GET, "/resources")
            .header("X-Plex-Token", token)
            .query(&[("includeHttps", "1"), ("includeRelay", "1")])
            .send()
            .await?
            .error_for_status()?
            .json()
            .await?;

        Ok(resources
            .into_iter()
            .filter(|r| r.provides.contains("server") && !r.connections.is_empty())
            .map(|r| {
                info!("Server: {} ({} connections)", r.name, r.connections.len());
                ServerInfo {
                    name: r.name,
                    access_token: r.access_token,
                    connections: r.connections,
                }
            })
            .collect())
    }
}

#[derive(Deserialize)]
struct PlexResource {
    name: String,
    provides: String,
    #[serde(rename = "accessToken")]
    access_token: Option<String>,
    #[serde(default)]
    connections: Vec<ServerConnection>,
}
