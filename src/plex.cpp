#include "plex.h"
#include "config.h"
#include "logger.h"
#include <random>
#include <iomanip>
#include <sstream>
#include <curl/curl.h>
#include "uuid.h"
#ifdef _WIN32
#include <shellapi.h>
#endif

Plex::Plex() : m_initialized(false)
{
}

Plex::~Plex()
{
    LOG_INFO("Plex", "Plex object destroyed");
}

bool Plex::init()
{
    LOG_INFO("Plex", "Initializing Plex connection");

    // Check if we have a Plex auth token
    auto &config = Config::getInstance();
    std::string authToken = config.getPlexAuthToken();

    if (authToken.empty())
    {
        // No auth token, need to acquire one
        if (!acquireAuthToken())
        {
            LOG_ERROR("Plex", "Failed to acquire Plex auth token");
            return false;
        }
        authToken = config.getPlexAuthToken();
    }

    LOG_INFO("Plex", "Using Plex auth token: " + authToken.substr(0, 5) + "...");

    // Fetch available servers
    auto servers = config.getPlexServers();
    if (config.getPlexServers().size() == 0)
    {
        LOG_INFO("Plex", "No Plex servers found, fetching from Plex.tv");
        if (!fetchServers())
        {
            LOG_ERROR("Plex", "Failed to fetch Plex servers");
            return false;
        }
    }

    // Set up SSE connections to each server
    setupServerConnections();

    m_initialized = true;
    return true;
}

bool Plex::acquireAuthToken()
{
    LOG_INFO("Plex", "Acquiring Plex auth token");

    std::string clientId = getClientIdentifier();

    // Create HTTP client
    HttpClient client;

    // Set up request headers
    std::map<std::string, std::string> headers = {
        {"X-Plex-Client-Identifier", clientId},
        {"X-Plex-Product", "Plex Presence"},
        {"X-Plex-Version", "0.2.1"},
        {"X-Plex-Device", "PC"},
#if defined(_WIN32)
        {"X-Plex-Platform", "Windows"},
#elif defined(__APPLE__)
        {"X-Plex-Platform", "macOS"},
#else
        {"X-Plex-Platform", "Linux"},
#endif
        {"Accept", "application/json"}};

    // Request a PIN from Plex
    std::string response;
    std::string pinUrl = "https://plex.tv/api/v2/pins";
    std::string data = "strong=true";

    if (!client.post(pinUrl, headers, data, response))
    {
        LOG_ERROR("Plex", "Failed to request PIN from Plex");
        return false;
    }

    LOG_DEBUG("Plex", "PIN response: " + response);

    // Parse the PIN response
    try
    {
        auto json = nlohmann::json::parse(response);
        std::string pin = json["code"].get<std::string>();
        std::string pinId = std::to_string(json["id"].get<int>());

        LOG_INFO("Plex", "Got PIN: " + pin + " (ID: " + pinId + ")");

        // Construct the authorization URL
        std::string authUrl = "https://app.plex.tv/auth#?clientID=" + clientId + "&code=" + pin + "&context%5Bdevice%5D%5Bproduct%5D=Plex%20Presence";

        // Open the authorization URL in the default browser
        LOG_INFO("Plex", "Opening browser for authentication: " + authUrl);

#ifdef _WIN32
        ShellExecuteA(NULL, "open", authUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
        // For non-Windows platforms
        std::string cmd = "xdg-open \"" + authUrl + "\"";
        system(cmd.c_str());
#endif

        // Poll for PIN authorization
        const int maxAttempts = 30;  // Try for about 5 minutes
        const int pollInterval = 10; // seconds

        LOG_INFO("Plex", "Waiting for user to authorize PIN...");

        for (int attempt = 0; attempt < maxAttempts; ++attempt)
        {
            // Wait before polling
            std::this_thread::sleep_for(std::chrono::seconds(pollInterval));

            // Check PIN status
            std::string statusUrl = "https://plex.tv/api/v2/pins/" + pinId;
            std::string statusResponse;

            if (!client.get(statusUrl, headers, statusResponse))
            {
                LOG_ERROR("Plex", "Failed to check PIN status");
                continue;
            }

            try
            {
                auto statusJson = nlohmann::json::parse(statusResponse);
                bool authDone = statusJson["authToken"].is_string() && !statusJson["authToken"].get<std::string>().empty();

                if (authDone)
                {
                    std::string authToken = statusJson["authToken"].get<std::string>();
                    LOG_INFO("Plex", "Successfully authenticated with Plex!");

                    // Save the auth token
                    Config::getInstance().setPlexAuthToken(authToken);

                    // Fetch and save the username
                    fetchAndSaveUsername(authToken, clientId);

                    Config::getInstance().saveConfig();

                    return true;
                }
                else
                {
                    LOG_DEBUG("Plex", "PIN not yet authorized, waiting... (" + std::to_string(attempt + 1) + "/" + std::to_string(maxAttempts) + ")");
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Plex", "Error parsing PIN status: " + std::string(e.what()));
            }
        }

        LOG_ERROR("Plex", "Timed out waiting for PIN authorization");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing PIN response: " + std::string(e.what()));
    }

    return false;
}

// Add a new helper method to fetch the username
bool Plex::fetchAndSaveUsername(const std::string &authToken, const std::string &clientId)
{
    LOG_INFO("Plex", "Fetching Plex username");

    // Create HTTP client
    HttpClient client;

    // Set up request headers
    std::map<std::string, std::string> headers = {
        {"X-Plex-Token", authToken},
        {"X-Plex-Client-Identifier", clientId},
        {"Accept", "application/json"}};

    // Make the request to fetch account information
    std::string response;
    std::string url = "https://plex.tv/api/v2/user";

    if (!client.get(url, headers, response))
    {
        LOG_ERROR("Plex", "Failed to fetch user information");
        return false;
    }

    try
    {
        auto json = nlohmann::json::parse(response);

        if (json.contains("username"))
        {
            std::string username = json["username"].get<std::string>();
            LOG_INFO("Plex", "Username: " + username);

            // Save the username
            Config::getInstance().setPlexUsername(username);
            return true;
        }
        else if (json.contains("title"))
        {
            // Some accounts may have title instead of username
            std::string username = json["title"].get<std::string>();
            LOG_INFO("Plex", "Username (from title): " + username);

            // Save the username
            Config::getInstance().setPlexUsername(username);
            return true;
        }

        LOG_ERROR("Plex", "Username not found in response");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing user response: " + std::string(e.what()));
    }

    return false;
}

std::string Plex::getClientIdentifier()
{
    auto &config = Config::getInstance();
    std::string clientId = config.getPlexClientIdentifier();

    if (clientId.empty())
    {
        clientId = uuid::generate_uuid_v4();
        config.setPlexClientIdentifier(clientId);
        config.saveConfig();
    }

    return clientId;
}

bool Plex::fetchServers()
{
    LOG_INFO("Plex", "Fetching Plex servers");

    auto &config = Config::getInstance();
    std::string authToken = config.getPlexAuthToken();
    std::string clientId = config.getPlexClientIdentifier();

    if (authToken.empty() || clientId.empty())
    {
        LOG_ERROR("Plex", "Missing auth token or client identifier");
        return false;
    }

    // Create HTTP client
    HttpClient client;

    // Set up request headers
    std::map<std::string, std::string> headers = {
        {"X-Plex-Token", authToken},
        {"X-Plex-Client-Identifier", clientId},
        {"X-Plex-Product", "Plex Presence"},
        {"X-Plex-Version", "0.2.1"},
        {"X-Plex-Device", "PC"},
        {"X-Plex-Platform", "Windows"},
        {"Accept", "application/json"}};

    // Make the request to Plex.tv
    std::string response;
    std::string url = "https://plex.tv/api/v2/resources?includeHttps=1";

    if (!client.get(url, headers, response))
    {
        LOG_ERROR("Plex", "Failed to fetch servers from Plex.tv");
        return false;
    }

    LOG_DEBUG("Plex", "Received server response: " + response);

    // Parse the JSON response
    return parseServerJson(response);
}

bool Plex::parseServerJson(const std::string &jsonStr)
{
    LOG_INFO("Plex", "Parsing server JSON");

    try
    {
        auto json = nlohmann::json::parse(jsonStr);

        auto &config = Config::getInstance();

        // Clear existing servers in config
        config.clearPlexServers();

        // Process each resource (server)
        for (const auto &resource : json)
        {
            // Check if this is a Plex Media Server
            std::string provides = resource.value("provides", "");
            if (provides != "server")
            {
                continue;
            }

            std::shared_ptr<PlexServer> server = std::make_shared<PlexServer>();
            server->name = resource.value("name", "Unknown");
            server->clientIdentifier = resource.value("clientIdentifier", "");
            server->accessToken = resource.value("accessToken", "");
            server->running = false;
            server->owned = resource.value("owned", false);
            ;

            LOG_INFO("Plex", "Found server: " + server->name +
                                 " (" + server->clientIdentifier + ")" +
                                 (server->owned ? " [owned]" : " [shared]"));

            // Process connections (we want both local and remote)
            if (resource.contains("connections") && resource["connections"].is_array())
            {
                for (const auto &connection : resource["connections"])
                {
                    std::string uri = connection.value("uri", "");
                    bool isLocal = connection.value("local", false);

                    if (isLocal)
                    {
                        server->localUri = uri;
                        LOG_INFO("Plex", "  Local URI: " + uri);
                    }
                    else
                    {
                        server->publicUri = uri;
                        LOG_INFO("Plex", "  Public URI: " + uri);
                    }
                }
            }

            // Add server to our map and config
            if (!server->localUri.empty() || !server->publicUri.empty())
            {
                // Save to config with ownership status
                config.addPlexServer(server->name, server->clientIdentifier,
                                     server->localUri, server->publicUri,
                                     server->accessToken, server->owned);
            }
        }

        config.saveConfig();

        LOG_INFO("Plex", "Found " + std::to_string(config.getPlexServers().size()) + " Plex servers");
        return !config.getPlexServers().empty();
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Failed to parse server JSON: " + std::string(e.what()));
        return false;
    }
}

void Plex::setupServerConnections()
{
    LOG_INFO("Plex", "Setting up server connections");

    for (auto &[id, server] : Config::getInstance().getPlexServers())
    {
        // Create a new HTTP client for this server
        server->httpClient = std::make_unique<HttpClient>();
        server->running = true;

        // Determine the URI to use (prefer local)
        std::string serverUri = !server->localUri.empty() ? server->localUri : server->publicUri;

        if (serverUri.empty())
        {
            LOG_WARNING("Plex", "No URI available for server: " + server->name);
            continue;
        }

        LOG_INFO("Plex", "Setting up SSE connection to server: " + server->name);

        // Set up headers
        std::map<std::string, std::string> headers = {
            {"X-Plex-Token", server->accessToken},
            {"X-Plex-Client-Identifier", Config::getInstance().getPlexClientIdentifier()},
            {"X-Plex-Product", "Plex Presence"},
            {"X-Plex-Version", "0.2.1"},
            {"X-Plex-Device", "PC"},
            {"X-Plex-Platform", "Windows"}};

        // Set up SSE endpoint URL
        std::string sseUrl = serverUri + "/:/eventsource/notifications?filters=playing";

        // Set up callback for SSE events
        auto callback = [this, id](const std::string &event)
        {
            this->handleSSEEvent(id, event);
        };

        // Start SSE connection
        if (!server->httpClient->startSSE(sseUrl, headers, callback))
        {
            LOG_ERROR("Plex", "Failed to set up SSE connection for server: " + server->name);
        }
    }
}

void Plex::handleSSEEvent(const std::string &serverId, const std::string &event)
{
    try
    {
        auto json = nlohmann::json::parse(event);

        LOG_DEBUG("Plex", "Received event from server " + serverId + ": " + event);

        // Check for PlaySessionStateNotification
        if (json.contains("PlaySessionStateNotification"))
        {
            processPlaySessionStateNotification(serverId, json["PlaySessionStateNotification"]);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing SSE event: " + std::string(e.what()));
    }
}

void Plex::processPlaySessionStateNotification(const std::string &serverId, const nlohmann::json &notification)
{
    LOG_DEBUG("Plex", "Processing PlaySessionStateNotification: " + notification.dump());

    auto server_itr = Config::getInstance().getPlexServers().find(serverId);
    if (server_itr == Config::getInstance().getPlexServers().end())
    {
        LOG_ERROR("Plex", "Unknown server ID: " + serverId);
        return;
    }
    auto server = server_itr->second;

    std::string sessionKey = notification.value("sessionKey", "");
    std::string state = notification.value("state", "");
    std::string mediaKey = notification.value("key", "");
    int64_t viewOffset = notification.value("viewOffset", 0);

    LOG_INFO("Plex", "Playback state update: " + state + " at " + std::to_string(viewOffset) + "ms");

    std::lock_guard<std::mutex> lock(m_sessionMutex);

    // Determine the URI to use (prefer local)
    std::string serverUri = !server->localUri.empty() ? server->localUri : server->publicUri;

    if (state == "playing" || state == "paused" || state == "buffering")
    {
        // Update or create playback info
        MediaInfo info;

        // Fetch detailed media info
        info = fetchMediaDetails(serverUri, server->accessToken, mediaKey);

        // Fetch user info from session data
        if (server->owned)
        {
            fetchSessionUserInfo(serverUri, server->accessToken, sessionKey, info);
            if (info.username != Config::getInstance().getPlexUsername())
            {
                return;
            }
        }

        // Update state
        if (state == "playing")
        {
            info.state = PlaybackState::Playing;
        }
        else if (state == "paused")
        {
            info.state = PlaybackState::Paused;
        }
        else if (state == "buffering")
        {
            info.state = PlaybackState::Buffering;
        }

        // Update progress in seconds
        info.progress = viewOffset / 1000.0;

        // Update session key and server ID
        info.sessionKey = sessionKey;
        info.serverId = serverId;

        // Update timestamp
        info.startTime = std::time(nullptr) - static_cast<time_t>(info.progress);

        // Store the updated info
        m_activeSessions[sessionKey] = info;

        LOG_INFO("Plex", "Updated session " + sessionKey + ": " + info.title +
                             " (" + std::to_string(info.progress) + "/" + std::to_string(info.duration) + "s)");
    }
    else if (state == "stopped")
    {
        // Remove the session if it exists
        if (m_activeSessions.find(sessionKey) != m_activeSessions.end())
        {
            LOG_INFO("Plex", "Removing stopped session: " + sessionKey);
            m_activeSessions.erase(sessionKey);
        }
    }
}

void Plex::fetchSessionUserInfo(const std::string &serverUri, const std::string &accessToken,
                                const std::string &sessionKey, MediaInfo &info)
{
    LOG_DEBUG("Plex", "Fetching user info for session: " + sessionKey);

    // Create HTTP client
    HttpClient client;

    // Set up headers
    std::map<std::string, std::string> headers = {
        {"X-Plex-Token", accessToken},
        {"X-Plex-Client-Identifier", Config::getInstance().getPlexClientIdentifier()},
        {"Accept", "application/json"}};

    // Make the request to get session data
    std::string url = serverUri + "/status/sessions";
    std::string response;

    if (!client.get(url, headers, response))
    {
        LOG_ERROR("Plex", "Failed to fetch session information");
        return;
    }

    try
    {
        auto json = nlohmann::json::parse(response);

        if (!json.contains("MediaContainer") || !json["MediaContainer"].contains("Metadata"))
        {
            LOG_ERROR("Plex", "Invalid session response format");
            return;
        }

        // Find the matching session by sessionKey
        for (const auto &session : json["MediaContainer"]["Metadata"])
        {
            if (session.contains("sessionKey") && session["sessionKey"].get<std::string>() == sessionKey)
            {
                // Extract user info
                if (session.contains("User") && session["User"].contains("title"))
                {
                    info.username = session["User"]["title"].get<std::string>();
                    LOG_INFO("Plex", "Found user for session " + sessionKey + ": " + info.username);
                }
                break;
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing session data: " + std::string(e.what()));
    }
}

MediaInfo Plex::fetchMediaDetails(const std::string &serverUri, const std::string &accessToken,
                                  const std::string &mediaKey)
{
    LOG_DEBUG("Plex", "Fetching media details for key: " + mediaKey);

    MediaInfo info;
    info.state = PlaybackState::Stopped;

    // Create HTTP client
    HttpClient client;

    // Set up headers
    std::map<std::string, std::string> headers = {
        {"X-Plex-Token", accessToken},
        {"X-Plex-Client-Identifier", Config::getInstance().getPlexClientIdentifier()},
        {"Accept", "application/json"}};

    // Make the request
    std::string url = serverUri + mediaKey;
    std::string response;

    if (!client.get(url, headers, response))
    {
        LOG_ERROR("Plex", "Failed to fetch media details");
        return info;
    }

    try
    {
        auto json = nlohmann::json::parse(response);

        if (!json.contains("MediaContainer") || !json["MediaContainer"].contains("Metadata") ||
            json["MediaContainer"]["Metadata"].empty())
        {
            LOG_ERROR("Plex", "Invalid media details response");
            return info;
        }

        auto metadata = json["MediaContainer"]["Metadata"][0];

        // Set basic info
        info.title = metadata.value("title", "Unknown");
        info.originalTitle = metadata.value("originalTitle", info.title);
        info.duration = metadata.value("duration", 0) / 1000.0; // Convert from milliseconds to seconds
        info.summary = metadata.value("summary", "No summary available");
        std::string type = metadata.value("type", "unknown");

        // Movie/TV specifics
        if (type == "movie")
        {
            info.type = MediaType::Movie;
        }
        else if (type == "episode")
        {
            info.type = MediaType::TVShow;
            info.grandparentTitle = metadata.value("grandparentTitle", "Unknown");
            info.season = metadata.value("parentIndex", 0);
            info.episode = metadata.value("index", 0);

            // Get the grandparent key (TV show) to fetch its metadata
            if (metadata.contains("grandparentKey"))
            {
                std::string grandparentKey = metadata.value("grandparentKey", "");
                fetchGrandparentArtwork(serverUri, accessToken, grandparentKey, info);
            }
        }
        else
        {
            info.type = MediaType::Unknown;
        }

        // Get external IDs that can be used to fetch public artwork
        if (metadata.contains("Guid") && metadata["Guid"].is_array())
        {
            for (const auto &guid : metadata["Guid"])
            {
                std::string id = guid.value("id", "");
                if (id.find("tmdb://") == 0)
                {
                    // Extract TMDB ID
                    std::string tmdbId = id.substr(7);

                    // For movies, use TMDB movie endpoint
                    if (info.type == MediaType::Movie)
                    {
                        fetchTMDBMovieArtwork(tmdbId, info);
                    }
                    // For TV shows, only use if we didn't get artwork from grandparent
                    else if (info.type == MediaType::TVShow && info.artPath.empty())
                    {
                        fetchTMDBTVShowArtwork(tmdbId, info);
                    }
                    break;
                }
                else if (id.find("tvdb://") == 0 && info.artPath.empty())
                {
                    // Use TVDB as fallback only if needed
                    std::string tvdbId = id.substr(7);
                    info.artPath = "https://thetvdb.com/banners/" + tvdbId;
                    break;
                }
            }
        }

        info.year = metadata.value("year", 0);

        LOG_INFO("Plex", "Media details: " + info.title + " (" + type + ")");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing media details: " + std::string(e.what()));
    }

    return info;
}

void Plex::fetchGrandparentArtwork(const std::string &serverUri, const std::string &accessToken,
                                   const std::string &grandparentKey, MediaInfo &info)
{
    LOG_DEBUG("Plex", "Fetching TV show metadata for key: " + grandparentKey);

    // Create HTTP client
    HttpClient client;

    // Set up headers
    std::map<std::string, std::string> headers = {
        {"X-Plex-Token", accessToken},
        {"X-Plex-Client-Identifier", Config::getInstance().getPlexClientIdentifier()},
        {"Accept", "application/json"}};

    // Make the request
    std::string url = serverUri + grandparentKey;
    std::string response;

    if (!client.get(url, headers, response))
    {
        LOG_ERROR("Plex", "Failed to fetch TV show metadata");
        return;
    }

    try
    {
        auto json = nlohmann::json::parse(response);

        if (!json.contains("MediaContainer") || !json["MediaContainer"].contains("Metadata") ||
            json["MediaContainer"]["Metadata"].empty())
        {
            LOG_ERROR("Plex", "Invalid TV show metadata response");
            return;
        }

        auto metadata = json["MediaContainer"]["Metadata"][0];

        // Process TMDB IDs from the show level
        if (metadata.contains("Guid") && metadata["Guid"].is_array())
        {
            for (const auto &guid : metadata["Guid"])
            {
                std::string id = guid.value("id", "");
                if (id.find("tmdb://") == 0)
                {
                    // Extract TMDB ID
                    std::string tmdbId = id.substr(7);
                    fetchTMDBTVShowArtwork(tmdbId, info);
                    LOG_INFO("Plex", "Found TV show TMDB ID: " + tmdbId);
                    return;
                }
                else if (id.find("tvdb://") == 0 && info.artPath.empty())
                {
                    // Extract TVDB ID as fallback
                    std::string tvdbId = id.substr(7);
                    info.artPath = "https://thetvdb.com/banners/" + tvdbId;
                    LOG_INFO("Plex", "Found TV show TVDB ID: " + tvdbId);
                    return;
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing TV show metadata: " + std::string(e.what()));
    }
}

void Plex::fetchTMDBMovieArtwork(const std::string &tmdbId, MediaInfo &info)
{
    LOG_DEBUG("Plex", "Fetching TMDB movie artwork for ID: " + tmdbId);

    // TMDB API requires an access token - get it from config
    std::string accessToken = Config::getInstance().getTMDBAccessToken();

    if (accessToken.empty())
    {
        LOG_INFO("Plex", "No TMDB access token available");
        return;
    }

    // Create HTTP client
    HttpClient client;

    // Set up API URL for v4 method (using read access token)
    std::string url = "https://api.themoviedb.org/3/movie/" + tmdbId + "/images";

    // Set up headers with Bearer token for v4 authentication
    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + accessToken},
        {"Content-Type", "application/json;charset=utf-8"}};

    // Make the request
    std::string response;
    if (!client.get(url, headers, response))
    {
        LOG_ERROR("Plex", "Failed to fetch TMDB movie images");
        return;
    }

    try
    {
        auto json = nlohmann::json::parse(response);

        // First try to get a poster
        if (json.contains("posters") && !json["posters"].empty())
        {
            std::string posterPath = json["posters"][0]["file_path"];
            info.artPath = "https://image.tmdb.org/t/p/w500" + posterPath;
            LOG_INFO("Plex", "Found TMDB movie poster: " + info.artPath);
        }
        // Fallback to backdrops
        else if (json.contains("backdrops") && !json["backdrops"].empty())
        {
            std::string backdropPath = json["backdrops"][0]["file_path"];
            info.artPath = "https://image.tmdb.org/t/p/w500" + backdropPath;
            LOG_INFO("Plex", "Found TMDB movie backdrop: " + info.artPath);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing TMDB movie response: " + std::string(e.what()));
    }
}

void Plex::fetchTMDBTVShowArtwork(const std::string &tmdbId, MediaInfo &info)
{
    LOG_DEBUG("Plex", "Fetching TMDB TV show artwork for ID: " + tmdbId);

    // Get access token from config
    std::string accessToken = Config::getInstance().getTMDBAccessToken();

    if (accessToken.empty())
    {
        LOG_INFO("Plex", "No TMDB access token available");
        return;
    }

    // Create HTTP client
    HttpClient client;

    // Set up API URL
    std::string url = "https://api.themoviedb.org/3/tv/" + tmdbId + "/images";

    // Set up headers with Bearer token for v4 authentication
    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + accessToken},
        {"Content-Type", "application/json;charset=utf-8"}};

    // Make the request
    std::string response;
    if (!client.get(url, headers, response))
    {
        LOG_ERROR("Plex", "Failed to fetch TMDB TV show images");
        return;
    }

    try
    {
        auto json = nlohmann::json::parse(response);

        // First try to get a poster
        if (json.contains("posters") && !json["posters"].empty())
        {
            std::string posterPath = json["posters"][0]["file_path"];
            info.artPath = "https://image.tmdb.org/t/p/w500" + posterPath;
            LOG_INFO("Plex", "Found TMDB TV show poster: " + info.artPath);
        }
        // Fallback to backdrops
        else if (json.contains("backdrops") && !json["backdrops"].empty())
        {
            std::string backdropPath = json["backdrops"][0]["file_path"];
            info.artPath = "https://image.tmdb.org/t/p/w500" + backdropPath;
            LOG_INFO("Plex", "Found TMDB TV show backdrop: " + info.artPath);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing TMDB TV show response: " + std::string(e.what()));
    }
}

MediaInfo Plex::getCurrentPlayback()
{
    if (!m_initialized)
    {
        LOG_WARNING("Plex", "Plex not initialized");
        MediaInfo info;
        info.state = PlaybackState::BadToken;
        return info;
    }

    std::lock_guard<std::mutex> lock(m_sessionMutex);

    // If no active sessions, return stopped state
    if (m_activeSessions.empty())
    {
        LOG_DEBUG("Plex", "No active sessions");
        MediaInfo info;
        info.state = PlaybackState::Stopped;
        return info;
    }

    // Find the oldest playing/paused/buffering session
    MediaInfo oldest;
    time_t oldestTime = (std::numeric_limits<time_t>::max)();

    for (const auto &[key, info] : m_activeSessions)
    {
        if (info.state == PlaybackState::Playing ||
            info.state == PlaybackState::Paused ||
            info.state == PlaybackState::Buffering)
        {

            if (info.startTime < oldestTime)
            {
                oldest = info;
                oldestTime = info.startTime;
            }
        }
    }

    if (oldestTime == (std::numeric_limits<time_t>::max)())
    {
        // No playing/paused/buffering sessions
        LOG_DEBUG("Plex", "No active playing sessions");
        MediaInfo info;
        info.state = PlaybackState::Stopped;
        return info;
    }

    LOG_DEBUG("Plex", "Returning playback info for: " + oldest.title);
    return oldest;
}

void Plex::stopConnections()
{
    LOG_INFO("Plex", "Stopping all Plex connections");

    // Stop all SSE connections with a very short timeout since we're shutting down
    for (auto &[id, server] : Config::getInstance().getPlexServers())
    {
        if (server->httpClient)
        {
            LOG_INFO("Plex", "Stopping SSE connection for server: " + server->name);
            server->running = false;

            // Explicitly reset the client to ensure destruction
            server->httpClient.reset();
        }
    }

    LOG_INFO("Plex", "All Plex connections stopped");
}
