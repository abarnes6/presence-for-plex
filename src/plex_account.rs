use log::info;
use reqwest::Client;
use serde::Deserialize;
use std::time::Duration;

pub const APP_NAME: &str = "presence-for-plex";
const PLEX_API: &str = "https://plex.tv/api/v2";
const TIMEOUT: Duration = Duration::from_secs(10);

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
pub struct ServerConnection { pub uri: String }

impl PlexAccount {
    pub fn new() -> Self {
        Self { client: Client::builder().user_agent("PresenceForPlex/1.0").timeout(TIMEOUT).build().expect("HTTP client"), username: None }
    }

    pub fn username(&self) -> Option<&str> { self.username.as_deref() }

    pub async fn fetch_username(&mut self, token: &str) -> Option<String> {
        let json: serde_json::Value = self.client.get(format!("{}/user", PLEX_API))
            .header("Accept", "application/json")
            .header("X-Plex-Token", token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .send().await.ok()?.json().await.ok()?;
        let username = json["username"].as_str()?.to_string();
        info!("Logged in as: {}", username);
        self.username = Some(username.clone());
        Some(username)
    }

    pub async fn request_pin(&self) -> Option<(u64, String)> {
        let json: serde_json::Value = self.client.post(format!("{}/pins", PLEX_API))
            .header("Accept", "application/json")
            .header("X-Plex-Product", "Presence for Plex")
            .header("X-Plex-Client-Identifier", APP_NAME)
            .query(&[("strong", "true")])
            .send().await.ok()?.json().await.ok()?;
        Some((json["id"].as_u64()?, json["code"].as_str()?.to_string()))
    }

    pub async fn check_pin(&self, pin_id: u64) -> Option<String> {
        let json: serde_json::Value = self.client.get(format!("{}/pins/{}", PLEX_API, pin_id))
            .header("Accept", "application/json")
            .header("X-Plex-Client-Identifier", APP_NAME)
            .send().await.ok()?.json().await.ok()?;
        json["authToken"].as_str().filter(|s| !s.is_empty()).map(String::from)
    }

    pub async fn get_servers(&self, token: &str) -> Option<Vec<ServerInfo>> {
        let resources: Vec<PlexResource> = self.client.get(format!("{}/resources", PLEX_API))
            .header("Accept", "application/json")
            .header("X-Plex-Token", token)
            .header("X-Plex-Client-Identifier", APP_NAME)
            .query(&[("includeHttps", "1"), ("includeRelay", "1")])
            .send().await.ok()?.json().await.ok()?;

        Some(resources.into_iter()
            .filter(|r| r.provides.contains("server") && !r.connections.is_empty())
            .map(|r| {
                info!("Server: {} ({} connections)", r.name, r.connections.len());
                ServerInfo { name: r.name, access_token: r.access_token, connections: r.connections.into_iter().map(|c| ServerConnection { uri: c.uri }).collect() }
            }).collect())
    }
}

#[derive(Deserialize)]
struct PlexResource {
    name: String,
    provides: String,
    #[serde(rename = "accessToken")]
    access_token: Option<String>,
    #[serde(default)]
    connections: Vec<PlexConnection>,
}

#[derive(Deserialize)]
struct PlexConnection { uri: String }
