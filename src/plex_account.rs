use log::{debug, error, info, warn};
use reqwest::Client;
use serde::Deserialize;
use std::time::Duration;

pub const APP_NAME: &str = "presence-for-plex";
const PLEX_API: &str = "https://plex.tv/api/v2";
const HTTP_TIMEOUT_SECS: u64 = 10;

pub struct PlexAccount {
    client: Client,
    username: Option<String>,
}

#[derive(Debug)]
pub struct ServerInfo {
    pub name: String,
    pub access_token: Option<String>,
    pub connections: Vec<ServerConnection>,
}

#[derive(Debug, Clone)]
pub struct ServerConnection {
    pub uri: String,
}

impl PlexAccount {
    pub fn new() -> Self {
        debug!("Creating PlexAccount");
        let client = Client::builder()
            .user_agent("PresenceForPlex/1.0")
            .timeout(Duration::from_secs(HTTP_TIMEOUT_SECS))
            .build()
            .expect("Failed to create HTTP client");

        Self {
            client,
            username: None,
        }
    }

    pub fn username(&self) -> Option<&str> {
        self.username.as_deref()
    }

    pub async fn fetch_username(&mut self, token: &str) -> Option<String> {
        debug!("Fetching username from Plex API");
        let response = self
            .client
            .get(format!("{}/user", PLEX_API))
            .header("Accept", "application/json")
            .header("X-Plex-Token", token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .send()
            .await;

        let resp = match response {
            Ok(r) => r,
            Err(e) => {
                warn!("Failed to fetch username: {}", e);
                return None;
            }
        };

        let json: serde_json::Value = match resp.json().await {
            Ok(j) => j,
            Err(e) => {
                warn!("Failed to parse user response: {}", e);
                return None;
            }
        };

        let username = json["username"].as_str()?.to_string();
        info!("Logged in as: {}", username);
        self.username = Some(username.clone());
        Some(username)
    }

    pub async fn request_pin(&self) -> Option<(u64, String)> {
        debug!("Requesting PIN from Plex API");
        let response = self
            .client
            .post(format!("{}/pins", PLEX_API))
            .header("Accept", "application/json")
            .header("X-Plex-Product", "Presence for Plex")
            .header("X-Plex-Client-Identifier", APP_NAME)
            .query(&[("strong", "true")])
            .send()
            .await;

        let resp = match response {
            Ok(r) => r,
            Err(e) => {
                error!("Failed to request PIN: {}", e);
                return None;
            }
        };

        let json: serde_json::Value = match resp.json().await {
            Ok(j) => j,
            Err(e) => {
                error!("Failed to parse PIN response: {}", e);
                return None;
            }
        };

        let pin_id = json["id"].as_u64()?;
        let code = json["code"].as_str()?.to_string();
        debug!("Got PIN id={}, code={}", pin_id, code);
        Some((pin_id, code))
    }

    pub async fn check_pin(&self, pin_id: u64) -> Option<String> {
        debug!("Checking PIN status for id={}", pin_id);
        let resp = self
            .client
            .get(format!("{}/pins/{}", PLEX_API, pin_id))
            .header("Accept", "application/json")
            .header("X-Plex-Client-Identifier", APP_NAME)
            .send()
            .await
            .ok()?
            .json::<serde_json::Value>()
            .await
            .ok()?;

        let token = resp["authToken"]
            .as_str()
            .filter(|s| !s.is_empty())
            .map(|s| s.to_string());

        if token.is_some() {
            debug!("PIN authenticated successfully");
        }
        token
    }

    pub async fn get_servers(&self, token: &str) -> Option<Vec<ServerInfo>> {
        debug!("Fetching servers from Plex API");
        let response = match self
            .client
            .get(format!("{}/resources", PLEX_API))
            .header("Accept", "application/json")
            .header("X-Plex-Token", token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .query(&[("includeHttps", "1"), ("includeRelay", "1")])
            .send()
            .await
        {
            Ok(r) => r,
            Err(e) => {
                error!("Failed to fetch servers: {}", e);
                return None;
            }
        };

        let resources: Vec<PlexResource> = match response.json().await {
            Ok(r) => r,
            Err(e) => {
                error!("Failed to parse servers: {}", e);
                return None;
            }
        };

        debug!("Got {} resources from Plex API", resources.len());

        let servers: Vec<_> = resources
            .into_iter()
            .filter(|r| r.provides.contains("server") && !r.connections.is_empty())
            .map(|r| {
                info!(
                    "Server: {} ({} connections, has_token: {})",
                    r.name,
                    r.connections.len(),
                    r.access_token.is_some()
                );
                ServerInfo {
                    name: r.name,
                    access_token: r.access_token,
                    connections: r.connections.into_iter().map(|c| ServerConnection { uri: c.uri }).collect(),
                }
            })
            .collect();

        Some(servers)
    }
}

#[derive(Deserialize)]
struct PlexResource {
    name: String,
    provides: String,
    #[serde(rename = "accessToken")]
    access_token: Option<String>,
    #[serde(default)]
    connections: Vec<PlexConnectionResponse>,
}

#[derive(Deserialize)]
struct PlexConnectionResponse {
    uri: String,
}
