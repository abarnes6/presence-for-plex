#include "plex.h"

// API endpoints and constants
namespace
{
    constexpr const char *PLEX_TV_API_URL = "https://plex.tv/api/v2";
    constexpr const char *PLEX_PIN_URL = "https://plex.tv/api/v2/pins";
    constexpr const char *PLEX_AUTH_URL = "https://app.plex.tv/auth#";
    constexpr const char *PLEX_USER_URL = "https://plex.tv/api/v2/user";
    constexpr const char *PLEX_RESOURCES_URL = "https://plex.tv/api/v2/resources?includeHttps=1";
    constexpr const char *JIKAN_API_URL = "https://api.jikan.moe/v4/anime";
    constexpr const char *TMDB_IMAGE_BASE_URL = "https://image.tmdb.org/t/p/w500";
    constexpr const char *SSE_NOTIFICATIONS_ENDPOINT = "/:/eventsource/notifications?filters=playing";
    constexpr const char *SESSION_ENDPOINT = "/status/sessions";

    // Cache timeouts (in seconds)
    constexpr const int TMDB_CACHE_TIMEOUT = 86400;  // 24 hours
    constexpr const int MAL_CACHE_TIMEOUT = 86400;   // 24 hours
    constexpr const int MEDIA_CACHE_TIMEOUT = 3600;  // 1 hour
    constexpr const int SESSION_CACHE_TIMEOUT = 300; // 5 minutes
}

// Cache structures
struct TimedCacheEntry
{
    time_t timestamp;
    bool valid() const { return (std::time(nullptr) - timestamp) < MEDIA_CACHE_TIMEOUT; }
};

struct TMDBCacheEntry : TimedCacheEntry
{
    std::string artPath;
    bool valid() const { return (std::time(nullptr) - timestamp) < TMDB_CACHE_TIMEOUT; }
};

struct MALCacheEntry : TimedCacheEntry
{
    std::string malId;
    bool valid() const { return (std::time(nullptr) - timestamp) < MAL_CACHE_TIMEOUT; }
};

struct MediaCacheEntry : TimedCacheEntry
{
    MediaInfo info;
};

struct SessionUserCacheEntry : TimedCacheEntry
{
    std::string username;
    bool valid() const { return (std::time(nullptr) - timestamp) < SESSION_CACHE_TIMEOUT; }
};

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

// Standard headers helper method
std::map<std::string, std::string> Plex::getStandardHeaders(const std::string &token)
{
    std::map<std::string, std::string> headers = {
        {"X-Plex-Client-Identifier", getClientIdentifier()},
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

    // Add token if provided
    if (!token.empty())
    {
        headers["X-Plex-Token"] = token;
    }

    return headers;
}

bool Plex::acquireAuthToken()
{
    LOG_INFO("Plex", "Acquiring Plex auth token");

    std::string clientId = getClientIdentifier();
    HttpClient client;
    std::map<std::string, std::string> headers = getStandardHeaders();

    // Request a PIN from Plex
    std::string pinId, pin;
    if (!requestPlexPin(pinId, pin, client, headers))
    {
        return false;
    }

    // Open browser for authentication
    openAuthorizationUrl(pin, clientId);

    // Poll for authorization
    return pollForPinAuthorization(pinId, pin, clientId, client, headers);
}

bool Plex::requestPlexPin(std::string &pinId, std::string &pin, HttpClient &client,
                          const std::map<std::string, std::string> &headers)
{
    std::string response;
    std::string data = "strong=true";

    if (!client.post(PLEX_PIN_URL, headers, data, response))
    {
        LOG_ERROR("Plex", "Failed to request PIN from Plex");
        return false;
    }

    LOG_DEBUG("Plex", "PIN response: " + response);

    // Parse the PIN response
    try
    {
        auto json = nlohmann::json::parse(response);
        pin = json["code"].get<std::string>();
        pinId = std::to_string(json["id"].get<int>());

        LOG_INFO("Plex", "Got PIN: " + pin + " (ID: " + pinId + ")");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing PIN response: " + std::string(e.what()));
        return false;
    }
}

void Plex::openAuthorizationUrl(const std::string &pin, const std::string &clientId)
{
    // Construct the authorization URL
    std::string authUrl = std::string(PLEX_AUTH_URL) +
                          "?clientID=" + clientId +
                          "&code=" + pin +
                          "&context%5Bdevice%5D%5Bproduct%5D=Plex%20Presence";

    LOG_INFO("Plex", "Opening browser for authentication: " + authUrl);

#ifdef _WIN32
    ShellExecuteA(NULL, "open", authUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    // For non-Windows platforms
    std::string cmd = "xdg-open \"" + authUrl + "\"";
    system(cmd.c_str());
#endif
}

bool Plex::pollForPinAuthorization(const std::string &pinId, const std::string &pin,
                                   const std::string &clientId, HttpClient &client,
                                   const std::map<std::string, std::string> &headers)
{
    const int maxAttempts = 30;  // Try for about 5 minutes
    const int pollInterval = 10; // seconds

    LOG_INFO("Plex", "Waiting for user to authorize PIN...");

    for (int attempt = 0; attempt < maxAttempts; ++attempt)
    {
        // Wait before polling
        std::this_thread::sleep_for(std::chrono::seconds(pollInterval));

        // Check PIN status
        std::string statusUrl = std::string(PLEX_PIN_URL) + "/" + pinId;
        std::string statusResponse;

        if (!client.get(statusUrl, headers, statusResponse))
        {
            LOG_ERROR("Plex", "Failed to check PIN status");
            continue;
        }

        try
        {
            auto statusJson = nlohmann::json::parse(statusResponse);
            bool authDone = statusJson["authToken"].is_string() &&
                            !statusJson["authToken"].get<std::string>().empty();

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
                LOG_DEBUG("Plex", "PIN not yet authorized, waiting... (" +
                                      std::to_string(attempt + 1) + "/" +
                                      std::to_string(maxAttempts) + ")");
            }
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("Plex", "Error parsing PIN status: " + std::string(e.what()));
        }
    }

    LOG_ERROR("Plex", "Timed out waiting for PIN authorization");
    return false;
}

bool Plex::fetchAndSaveUsername(const std::string &authToken, const std::string &clientId)
{
    LOG_INFO("Plex", "Fetching Plex username");

    // Create HTTP client
    HttpClient client;
    std::map<std::string, std::string> headers = getStandardHeaders(authToken);

    // Make the request to fetch account information
    std::string response;

    if (!client.get(PLEX_USER_URL, headers, response))
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

    // Create HTTP client and headers
    HttpClient client;
    std::map<std::string, std::string> headers = getStandardHeaders(authToken);

    // Make the request to Plex.tv
    std::string response;

    if (!client.get(PLEX_RESOURCES_URL, headers, response))
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
        setupServerSSEConnection(server);
    }
}

void Plex::setupServerSSEConnection(const std::shared_ptr<PlexServer> &server)
{
    // Create a new HTTP client for this server
    server->httpClient = std::make_unique<HttpClient>();
    server->running = true;

    // Determine the URI to use (prefer local)
    std::string serverUri = !server->localUri.empty() ? server->localUri : server->publicUri;

    if (serverUri.empty())
    {
        LOG_WARNING("Plex", "No URI available for server: " + server->name);
        return;
    }

    LOG_INFO("Plex", "Setting up SSE connection to server: " + server->name);

    // Set up headers
    std::map<std::string, std::string> headers = getStandardHeaders(server->accessToken);

    // Set up SSE endpoint URL
    std::string sseUrl = serverUri + SSE_NOTIFICATIONS_ENDPOINT;

    // Set up callback for SSE events
    auto callback = [this, id = server->clientIdentifier](const std::string &event)
    {
        this->handleSSEEvent(id, event);
    };

    // Start SSE connection
    if (!server->httpClient->startSSE(sseUrl, headers, callback))
    {
        LOG_ERROR("Plex", "Failed to set up SSE connection for server: " + server->name);
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
        LOG_ERROR("Plex", "Error parsing SSE event: " + std::string(e.what()) +
                              ", Event: " + event.substr(0, 100) + (event.length() > 100 ? "..." : ""));
    }
}

void Plex::processPlaySessionStateNotification(const std::string &serverId, const nlohmann::json &notification)
{
    LOG_DEBUG("Plex", "Processing PlaySessionStateNotification: " + notification.dump());

    // Find the server
    auto server_itr = Config::getInstance().getPlexServers().find(serverId);
    if (server_itr == Config::getInstance().getPlexServers().end())
    {
        LOG_ERROR("Plex", "Unknown server ID: " + serverId);
        return;
    }
    auto server = server_itr->second;

    // Extract essential session information
    std::string sessionKey = notification.value("sessionKey", "");
    std::string state = notification.value("state", "");
    std::string mediaKey = notification.value("key", "");
    int64_t viewOffset = notification.value("viewOffset", 0);

    LOG_INFO("Plex", "Playback state update: " + state + " at " + std::to_string(viewOffset) + "ms");

    std::lock_guard<std::mutex> lock(m_sessionMutex);

    if (state == "playing" || state == "paused" || state == "buffering")
    {
        updateSessionInfo(serverId, sessionKey, state, mediaKey, viewOffset, server);
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

void Plex::updateSessionInfo(const std::string &serverId, const std::string &sessionKey,
                             const std::string &state, const std::string &mediaKey,
                             int64_t viewOffset, const std::shared_ptr<PlexServer> &server)
{
    // Determine the URI to use (prefer local)
    std::string serverUri = !server->localUri.empty() ? server->localUri : server->publicUri;

    // Create a cache key for media info
    std::string mediaInfoCacheKey = serverUri + mediaKey;

    // Check if we have cached media info that's still valid
    MediaInfo info;
    bool needMediaFetch = true;

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        auto cacheIt = m_mediaInfoCache.find(mediaInfoCacheKey);
        if (cacheIt != m_mediaInfoCache.end() && cacheIt->second.valid())
        {
            info = cacheIt->second.info;
            needMediaFetch = false;
            LOG_DEBUG("Plex", "Using cached media info for key: " + mediaKey);
        }
    }

    // Fetch media details if needed
    if (needMediaFetch)
    {
        info = fetchMediaDetails(serverUri, server->accessToken, mediaKey);

        // Cache the result
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        MediaCacheEntry entry;
        entry.timestamp = std::time(nullptr);
        entry.info = info;
        m_mediaInfoCache[mediaInfoCacheKey] = entry;
    }

    // For owned servers, we need to check if this session belongs to the current user
    if (server->owned)
    {
        // Create cache key for session user info
        std::string sessionUserCacheKey = serverUri + sessionKey;
        bool needUserFetch = true;

        {
            std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
            auto cacheIt = m_sessionUserCache.find(sessionUserCacheKey);
            if (cacheIt != m_sessionUserCache.end() && cacheIt->second.valid())
            {
                info.username = cacheIt->second.username;
                needUserFetch = false;
                LOG_DEBUG("Plex", "Using cached user info for session: " + sessionKey);
            }
        }

        if (needUserFetch)
        {
            fetchSessionUserInfo(serverUri, server->accessToken, sessionKey, info);

            // Cache the result
            std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
            SessionUserCacheEntry entry;
            entry.timestamp = std::time(nullptr);
            entry.username = info.username;
            m_sessionUserCache[sessionUserCacheKey] = entry;
        }

        // Skip sessions that don't belong to the current user
        if (info.username != Config::getInstance().getPlexUsername())
        {
            LOG_DEBUG("Plex", "Ignoring session for different user: " + info.username);
            return;
        }
    }

    // Update playback state
    updatePlaybackState(info, state, viewOffset);

    // Update session key and server ID
    info.sessionKey = sessionKey;
    info.serverId = serverId;

    // Store the updated info
    m_activeSessions[sessionKey] = info;

    LOG_INFO("Plex", "Updated session " + sessionKey + ": " + info.title +
                         " (" + std::to_string(info.progress) + "/" + std::to_string(info.duration) + "s)");
}

// Helper method to update playback state (extracted from updateSessionInfo for clarity)
void Plex::updatePlaybackState(MediaInfo &info, const std::string &state, int64_t viewOffset)
{
    // Update state based on string value
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

    // Convert milliseconds to seconds and update progress
    info.progress = viewOffset / 1000.0;

    // Calculate start time based on current time and progress
    info.startTime = std::time(nullptr) - static_cast<time_t>(info.progress);
}

void Plex::fetchSessionUserInfo(const std::string &serverUri, const std::string &accessToken,
                                const std::string &sessionKey, MediaInfo &info)
{
    LOG_DEBUG("Plex", "Fetching user info for session: " + sessionKey);

    // Create HTTP client
    HttpClient client;

    // Set up headers
    std::map<std::string, std::string> headers = getStandardHeaders(accessToken);

    // Make the request to get session data
    std::string url = serverUri + SESSION_ENDPOINT;
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
    std::map<std::string, std::string> headers = getStandardHeaders(accessToken);

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

        // Extract basic media info that applies to all media types
        extractBasicMediaInfo(metadata, info);

        // Handle media type-specific details
        std::string type = metadata.value("type", "unknown");
        if (type == "movie")
        {
            extractMovieSpecificInfo(metadata, info);
        }
        else if (type == "episode")
        {
            extractTVShowSpecificInfo(metadata, info);
            fetchGrandparentMetadata(serverUri, accessToken, info);
        }
        else
        {
            info.type = MediaType::Unknown;
        }

        LOG_INFO("Plex", "Media details: " + info.title + " (" + type + ")");
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing media details: " + std::string(e.what()));
    }

    return info;
}

void Plex::extractBasicMediaInfo(const nlohmann::json &metadata, MediaInfo &info)
{
    // Set basic info common to all media types
    info.title = metadata.value("title", "Unknown");
    info.originalTitle = metadata.value("originalTitle", info.title);
    info.duration = metadata.value("duration", 0) / 1000.0; // Convert from milliseconds to seconds
    info.summary = metadata.value("summary", "No summary available");
    info.year = metadata.value("year", 0);
}

void Plex::extractMovieSpecificInfo(const nlohmann::json &metadata, MediaInfo &info)
{
    info.type = MediaType::Movie;
    parseGuid(metadata, info);
    parseGenres(metadata, info);
}

void Plex::extractTVShowSpecificInfo(const nlohmann::json &metadata, MediaInfo &info)
{
    info.type = MediaType::TVShow;
    info.grandparentTitle = metadata.value("grandparentTitle", "Unknown");
    info.season = metadata.value("parentIndex", 0);
    info.episode = metadata.value("index", 0);

    if (metadata.contains("grandparentKey"))
    {
        info.grandparentKey = metadata.value("grandparentKey", "");
    }
}

void Plex::fetchGrandparentMetadata(const std::string &serverUrl, const std::string &accessToken,
                                    MediaInfo &info)
{

    if (info.grandparentKey.empty())
    {
        LOG_ERROR("Plex", "No grandparent key available for TV show metadata");
        return;
    }

    LOG_DEBUG("Plex", "Fetching TV show metadata for key: " + info.grandparentKey);

    // Create HTTP client
    HttpClient client;

    // Set up headers
    std::map<std::string, std::string> headers = getStandardHeaders(accessToken);

    // Make the request
    std::string url = serverUrl + info.grandparentKey;
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

        parseGuid(metadata, info);

        // Parse genres
        parseGenres(metadata, info);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing TV show metadata: " + std::string(e.what()));
    }
}

void Plex::parseGuid(const nlohmann::json &metadata, MediaInfo &info)
{
    if (metadata.contains("Guid") && metadata["Guid"].is_array())
    {
        for (const auto &guid : metadata["Guid"])
        {
            std::string id = guid.value("id", "");
            if (id.find("imdb://") == 0)
            {
                info.imdbId = id.substr(7);
                LOG_INFO("Plex", "Found IMDb ID: " + info.imdbId);
            }
            else if (id.find("tmdb://") == 0)
            {
                info.tmdbId = id.substr(7);

                // Check TMDB artwork cache
                bool needArtworkFetch = true;
                {
                    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
                    auto cacheIt = m_tmdbArtworkCache.find(info.tmdbId);
                    if (cacheIt != m_tmdbArtworkCache.end() && cacheIt->second.valid())
                    {
                        info.artPath = cacheIt->second.artPath;
                        needArtworkFetch = false;
                        LOG_DEBUG("Plex", "Using cached TMDB artwork for ID: " + info.tmdbId);
                    }
                }

                if (needArtworkFetch)
                {
                    fetchTMDBArtwork(info.tmdbId, info);

                    // Cache the result if we found artwork
                    if (!info.artPath.empty())
                    {
                        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
                        TMDBCacheEntry entry;
                        entry.timestamp = std::time(nullptr);
                        entry.artPath = info.artPath;
                        m_tmdbArtworkCache[info.tmdbId] = entry;
                    }
                }

                LOG_INFO("Plex", "Found TMDB ID: " + info.tmdbId);
            }
        }
    }
}

void Plex::parseGenres(const nlohmann::json &metadata, MediaInfo &info)
{
    if (metadata.contains("Genre") && metadata["Genre"].is_array())
    {
        for (const auto &genre : metadata["Genre"])
        {
            info.genres.push_back(genre.value("tag", ""));
        }
    }

    if (isAnimeContent(metadata))
    {
        fetchAnimeMetadata(info);
    }
}

bool Plex::isAnimeContent(const nlohmann::json &metadata)
{
    if (metadata.contains("Genre") && metadata["Genre"].is_array())
    {
        for (const auto &genre : metadata["Genre"])
        {
            if (genre.value("tag", "") == "Anime")
            {
                LOG_INFO("Plex", "Detected Anime genre tag");
                return true;
            }
        }
    }
    return false;
}

void Plex::fetchAnimeMetadata(MediaInfo &info)
{
    LOG_INFO("Plex", "Anime detected, searching MyAnimeList via Jikan API");

    // Create cache key (use title as key)
    std::string cacheKey = info.originalTitle.empty() ? info.title : info.originalTitle;

    // Check if we have cached MAL info
    bool needMALFetch = true;
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        auto cacheIt = m_malIdCache.find(cacheKey);
        if (cacheIt != m_malIdCache.end() && cacheIt->second.valid())
        {
            info.malId = cacheIt->second.malId;
            needMALFetch = false;
            LOG_DEBUG("Plex", "Using cached MAL ID for: " + cacheKey);
        }
    }

    if (needMALFetch)
    {
        HttpClient jikanClient;
        std::string encodedTitle = cacheKey;
        std::string::size_type pos = 0;
        while ((pos = encodedTitle.find(' ', pos)) != std::string::npos)
        {
            encodedTitle.replace(pos, 1, "%20");
            pos += 3;
        }

        std::string jikanUrl = std::string(JIKAN_API_URL) + "?q=" + encodedTitle;

        std::string jikanResponse;
        if (jikanClient.get(jikanUrl, {}, jikanResponse))
        {
            try
            {
                auto jikanJson = nlohmann::json::parse(jikanResponse);
                if (jikanJson.contains("data") && !jikanJson["data"].empty())
                {
                    auto firstResult = jikanJson["data"][0];
                    if (firstResult.contains("mal_id"))
                    {
                        info.malId = std::to_string(firstResult["mal_id"].get<int>());
                        LOG_INFO("Plex", "Found MyAnimeList ID: " + info.malId);

                        // Cache the result
                        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
                        MALCacheEntry entry;
                        entry.timestamp = std::time(nullptr);
                        entry.malId = info.malId;
                        m_malIdCache[cacheKey] = entry;
                    }
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Plex", "Error parsing Jikan API response: " + std::string(e.what()));
            }
        }
        else
        {
            LOG_ERROR("Plex", "Failed to fetch data from Jikan API");
        }
    }
}

void Plex::fetchTMDBArtwork(const std::string &tmdbId, MediaInfo &info)
{
    LOG_DEBUG("Plex", "Fetching TMDB artwork for ID: " + tmdbId);

    // TMDB API requires an access token - get it from config
    std::string accessToken = Config::getInstance().getTMDBAccessToken();

    if (accessToken.empty())
    {
        LOG_INFO("Plex", "No TMDB access token available");
        return;
    }

    // Create HTTP client
    HttpClient client;
    std::string url;

    // Construct proper endpoint URL based on media type
    if (info.type == MediaType::Movie)
    {
        url = "https://api.themoviedb.org/3/movie/" + tmdbId + "/images";
    }
    else
    {
        url = "https://api.themoviedb.org/3/tv/" + tmdbId + "/images";
    }

    // Set up headers with Bearer token for v4 authentication
    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer " + accessToken},
        {"Content-Type", "application/json;charset=utf-8"}};

    // Make the request
    std::string response;
    if (!client.get(url, headers, response))
    {
        LOG_ERROR("Plex", "Failed to fetch TMDB images");
        return;
    }

    try
    {
        auto json = nlohmann::json::parse(response);

        // First try to get a poster
        if (json.contains("posters") && !json["posters"].empty())
        {
            std::string posterPath = json["posters"][0]["file_path"];
            info.artPath = std::string(TMDB_IMAGE_BASE_URL) + posterPath;
            LOG_INFO("Plex", "Found TMDB poster: " + info.artPath);
        }
        // Fallback to backdrops
        else if (json.contains("backdrops") && !json["backdrops"].empty())
        {
            std::string backdropPath = json["backdrops"][0]["file_path"];
            info.artPath = std::string(TMDB_IMAGE_BASE_URL) + backdropPath;
            LOG_INFO("Plex", "Found TMDB backdrop: " + info.artPath);
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing TMDB response: " + std::string(e.what()));
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

    // Clear any cached data
    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
    m_tmdbArtworkCache.clear();
    m_malIdCache.clear();
    m_mediaInfoCache.clear();
    m_sessionUserCache.clear();

    LOG_INFO("Plex", "All Plex connections stopped");
}
