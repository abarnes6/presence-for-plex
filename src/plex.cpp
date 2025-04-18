#include "plex.h"

using json = nlohmann::json;

Plex::~Plex()
{
    stopPolling();
    curl_global_cleanup();
}

Plex::Plex()
{
    curl_global_init(CURL_GLOBAL_ALL);
    authToken = Config::getInstance().getAuthToken();
    if (authToken.empty())
    {
        // Generate a random UUID
        std::string uuid = uuid::generate_uuid_v4();
        LOG_INFO("Plex", "Generated UUID: " + uuid);

        // Request a new PIN from Plex
        std::string pinCode;
        std::string pinId;
        if (requestPlexPin(uuid, pinCode, pinId))
        {
            // Construct the auth URL for the user
            std::string authUrl = "https://app.plex.tv/auth#?clientID=" + uuid + "&code=" + pinCode;

            // Show instructions to the user
            LOG_INFO("Plex", "Please open the following URL in your browser to authorize this application:");
            LOG_INFO("Plex", authUrl);
            LOG_INFO("Plex", "Waiting for authorization...");

            // Poll for the auth token
            if (pollForAuthToken(pinId, uuid))
            {
                LOG_INFO("Plex", "Successfully authorized with Plex!");
            }
            else
            {
                LOG_ERROR("Plex", "Failed to get authorization from Plex.");
            }
        }
        else
        {
            LOG_ERROR("Plex", "Failed to request PIN from Plex.");
            exit(1);
        }
    }
}

// Start the polling thread
void Plex::startPolling()
{
    running = true;
    pollingThread = std::thread(&Plex::plexPollingThread, this);
}

void Plex::stopPolling()
{
    running = false;
    if (pollingThread.joinable())
        pollingThread.join();
}

// Utility function for HTTP requests
size_t Plex::WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s)
{
    size_t newLength = size * nmemb;
    try
    {
        s->append((char *)contents, newLength);
        return newLength;
    }
    catch (std::bad_alloc &)
    {
        return 0;
    }
}

// Function to get Plex Direct hash from server identity
std::string Plex::getPlexDirectHash() const
{
    CURL *curl;
    CURLcode res;
    std::string hash;

    curl = curl_easy_init();
    if (curl)
    {
        std::string identityUrl = Config::getInstance().serverUrl + "/web/identity";
        std::string serverCert;

        // Configure curl to get the certificate information
        curl_easy_setopt(curl, CURLOPT_URL, identityUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        res = curl_easy_perform(curl);
        if (res == CURLE_OK)
        {
            struct curl_certinfo *certinfo;
            res = curl_easy_getinfo(curl, CURLINFO_CERTINFO, &certinfo);

            if (res == CURLE_OK && certinfo)
            {
                // Look for the subject field in certificate info
                for (int i = 0; i < certinfo->num_of_certs; i++)
                {
                    struct curl_slist *slist = certinfo->certinfo[i];
                    while (slist)
                    {
                        std::string certData = slist->data;

                        // Look for subject line with the hash
                        if (certData.find("subject:") != std::string::npos &&
                            certData.find("plex.direct") != std::string::npos)
                        {
                            // Parse out the hash from the subject format: CN=*.HASH.plex.direct
                            size_t start = certData.find("CN=*.");
                            if (start != std::string::npos)
                            {
                                start += 5; // Skip "CN=*."
                                size_t end = certData.find(".plex.direct", start);
                                if (end != std::string::npos)
                                {
                                    hash = certData.substr(start, end - start);
                                    break;
                                }
                            }
                        }
                        slist = slist->next;
                    }
                    if (!hash.empty())
                        break;
                }
            }
        }

        curl_easy_cleanup(curl);
    }

    return hash;
}

// Function to perform HTTP request to Plex API
std::string Plex::makeRequest(const std::string &url) const
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl)
    {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, ("X-Plex-Token: " + this->authToken).c_str());

        std::string requestUrl = url;

        // Parse server URL to extract IP and port
        std::string serverUrl = Config::getInstance().serverUrl;
        size_t protocolEnd = serverUrl.find("://");
        std::string serverIp;
        std::string serverPort = "32400"; // Default Plex port

        if (protocolEnd != std::string::npos)
        {
            size_t portPos = serverUrl.find(":", protocolEnd + 3);
            if (portPos != std::string::npos)
            {
                serverIp = serverUrl.substr(protocolEnd + 3, portPos - (protocolEnd + 3));
                size_t pathPos = serverUrl.find("/", portPos);
                if (pathPos != std::string::npos)
                {
                    serverPort = serverUrl.substr(portPos + 1, pathPos - (portPos + 1));
                }
                else
                {
                    serverPort = serverUrl.substr(portPos + 1);
                }
            }
            else
            {
                size_t pathPos = serverUrl.find("/", protocolEnd + 3);
                if (pathPos != std::string::npos)
                {
                    serverIp = serverUrl.substr(protocolEnd + 3, pathPos - (protocolEnd + 3));
                }
                else
                {
                    serverIp = serverUrl.substr(protocolEnd + 3);
                }
            }
        }

        // Use Plex Direct URL if we can get the hash
        std::string plexDirectHash = getPlexDirectHash();

        if (!plexDirectHash.empty() && !serverIp.empty())
        {
            // Format: https://IP-WITH-DASHES.HASH.plex.direct:PORT/path
            std::string ipWithDashes = serverIp;
            std::replace(ipWithDashes.begin(), ipWithDashes.end(), '.', '-');

            // Create Plex Direct URL
            std::string plexDirectUrl = "https://" + ipWithDashes + "." + plexDirectHash + ".plex.direct:" + serverPort;

            // Replace the server URL part with the Plex Direct URL
            size_t pathPos = url.find("/", protocolEnd + 3);
            if (pathPos != std::string::npos)
            {
                requestUrl = plexDirectUrl + url.substr(pathPos);
            }
            else
            {
                requestUrl = plexDirectUrl;
            }

            // Set up DNS resolution for the Plex Direct hostname
            std::string resolve = ipWithDashes + "." + plexDirectHash + ".plex.direct:" + serverPort + ":" + serverIp;

            curl_easy_setopt(curl, CURLOPT_RESOLVE, curl_slist_append(NULL, resolve.c_str()));
        }

        curl_easy_setopt(curl, CURLOPT_URL, requestUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            LOG_ERROR("Plex", "curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
            return "";
        }
    }

    return readBuffer;
}

// Parse Plex API response
bool Plex::parseSessionsResponse(const std::string &response, PlaybackInfo &info)
{
    try
    {
        // Check if response is empty or doesn't look like JSON
        if (response.empty() || (response[0] != '{' && response[0] != '['))
        {
            LOG_ERROR("Plex", "Invalid Plex response format: Response doesn't appear to be JSON");
            if (!response.empty())
            {
                LOG_ERROR_STREAM("Plex", "Response begins with: " << response.substr(0, std::min(50, (int)response.size())) << "...");
            }
            info.state = PlaybackState::Stopped;
            return false;
        }

        json j = json::parse(response);

        // Check if there are any active sessions
        if (j["MediaContainer"].contains("size") && j["MediaContainer"]["size"].get<int>() > 0)
        {
            auto sessions = j["MediaContainer"]["Metadata"];

            // First, get the user ID of the authenticated user
            std::string authenticatedUserId = "";
            std::string authenticatedUsername = "";

            // Get authenticated user info from Plex
            std::string accountInfoResponse = makeRequest("https://plex.tv/api/v2/user");
            if (!accountInfoResponse.empty() && accountInfoResponse[0] == '{')
            {
                try
                {
                    json accountInfo = json::parse(accountInfoResponse);
                    if (accountInfo.contains("id"))
                    {
                        // Handle user ID - could be number or string
                        if (accountInfo["id"].is_string())
                            authenticatedUserId = accountInfo["id"].get<std::string>();
                        else if (accountInfo["id"].is_number())
                            authenticatedUserId = std::to_string(accountInfo["id"].get<int>());

                        if (accountInfo.contains("username"))
                            authenticatedUsername = accountInfo["username"].get<std::string>();
                        else if (accountInfo.contains("title"))
                            authenticatedUsername = accountInfo["title"].get<std::string>();
                    }
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR("Plex", "Error parsing user account info: " + std::string(e.what()));
                }
            }

            // Try to find the session by matching various attributes
            for (const auto &session : sessions)
            {
                bool isAuthenticatedUser = false;
                std::string sessionUserId = "";

                // Check if this session belongs to a user
                if (session.contains("User"))
                {
                    auto user = session["User"];
                    // Handle user ID - could be number or string
                    if (user["id"].is_string())
                        sessionUserId = user["id"].get<std::string>();
                    else if (user["id"].is_number())
                        sessionUserId = std::to_string(user["id"].get<int>());

                    // Check if this is the authenticated user's session
                    if (!authenticatedUserId.empty() && sessionUserId == authenticatedUserId)
                    {
                        isAuthenticatedUser = true;
                    }
                }

                // If we haven't confirmed this is the user's session, check for local playback
                if (!isAuthenticatedUser && session.contains("Player"))
                {
                    // First check if Player.userId matches the authenticated user ID
                    if (session["Player"].contains("userID") && !authenticatedUserId.empty())
                    {
                        std::string playerUserId;
                        if (session["Player"]["userID"].is_string())
                            playerUserId = session["Player"]["userID"].get<std::string>();
                        else if (session["Player"]["userID"].is_number())
                            playerUserId = std::to_string(session["Player"]["userID"].get<int>());

                        if (playerUserId == authenticatedUserId)
                        {
                            isAuthenticatedUser = true;
                        }
                    }

                    // If still not matching, check for local playback as fallback
                    if (!isAuthenticatedUser &&
                        session["Player"].contains("local") &&
                        session["Player"]["local"].get<bool>() == true)
                    {
                        isAuthenticatedUser = true;
                    }
                }

                // If we still haven't found the user but have a userId of 1,
                // this might be an admin account that owns the Plex server
                if (!isAuthenticatedUser && sessionUserId == "1")
                {
                    isAuthenticatedUser = true;
                }

                if (isAuthenticatedUser)
                {
                    // Reset all fields before populating
                    info = PlaybackInfo{};
                    
                    info.title = session["title"].get<std::string>();
                    info.mediaType = session["type"].get<std::string>();

                    // Check for playback state if Player information is available
                    if (session.contains("Player"))
                    {
                        // Get the playback state
                        if (session["Player"].contains("state"))
                        {
                            std::string state = session["Player"]["state"].get<std::string>();
                            // Map the state to PlaybackState enum
                            
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
                            else
                            {
                                info.state = PlaybackState::Stopped;
                            }
                        }
                        else
                        {
                            info.state = PlaybackState::Playing; // Default to playing if state is not specified
                        }
                    }
                    else
                    {
                        info.state = PlaybackState::Playing; // Default to playing if Player is not available
                    }

                    if (session.contains("User"))
                    {
                        auto user = session["User"];
                        // Handle user ID - could be number or string
                        if (user["id"].is_string())
                            info.userId = user["id"].get<std::string>();
                        else if (user["id"].is_number())
                            info.userId = std::to_string(user["id"].get<int>());

                        info.username = user["title"].get<std::string>();
                    }
                    else if (!authenticatedUsername.empty())
                    {
                        // Use the authenticated username if available
                        info.userId = authenticatedUserId;
                        info.username = authenticatedUsername;
                    }
                    else
                    {
                        // Fallback
                        info.userId = "authenticated_user";
                        info.username = "Authenticated User";
                    }

                    // Handle episode-specific information
                    if (info.mediaType == "episode")
                    {
                        // Extract season and episode number
                        std::string seasonNum = session["parentIndex"].is_string() ? 
                            session["parentIndex"].get<std::string>() : 
                            std::to_string(session["parentIndex"].get<int>());
                        
                        std::string episodeNum = session["index"].is_string() ? 
                            session["index"].get<std::string>() : 
                            std::to_string(session["index"].get<int>());
                        
                        // Format as S01E01
                        info.seasonEpisode = "S" + (seasonNum.size() == 1 ? "0" + seasonNum : seasonNum) + 
                                           "E" + (episodeNum.size() == 1 ? "0" + episodeNum : episodeNum);
                        
                        // Get episode title
                        info.episodeName = info.title;
                        
                        // Get show title and set as main title
                        if (session.contains("grandparentTitle"))
                        {
                            info.title = session["grandparentTitle"].get<std::string>();
                        }
                        
                        // Set subtitle with season/episode info and episode name
                        info.subtitle = info.seasonEpisode + " - " + info.episodeName;
                    }

                    if (session.contains("thumb"))
                    {
                        info.thumbnailUrl = Config::getInstance().serverUrl + session["thumb"].get<std::string>() +
                                            "?X-Plex-Token=" + this->authToken;
                    }

                    // Convert viewOffset and duration from milliseconds to seconds
                    // Handle different possible numeric types
                    if (session.contains("viewOffset"))
                    {
                        int64_t offset = session["viewOffset"].is_number_integer() ? session["viewOffset"].get<int64_t>() : static_cast<int64_t>(session["viewOffset"].get<double>());
                        info.progress = offset / 1000;
                    }
                    else
                    {
                        info.progress = 0;
                    }

                    if (session.contains("duration"))
                    {
                        int64_t duration = session["duration"].is_number_integer() ? session["duration"].get<int64_t>() : static_cast<int64_t>(session["duration"].get<double>());
                        info.duration = duration / 1000;
                    }
                    else
                    {
                        info.duration = 0;
                    }

                    info.startTime = std::time(nullptr) - info.progress;

                    return true;
                }
            }
        }

        // No matching session found
        info.state = PlaybackState::Stopped;
        return false;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Plex", "Error parsing Plex response: " + std::string(e.what()));
        info.state = PlaybackState::Stopped;
        return false;
    }
}

// Polling thread function
void Plex::plexPollingThread()
{
    while (running)
    {
        std::string url = Config::getInstance().serverUrl + "/status/sessions";
        std::string response = makeRequest(url);

        if (!response.empty())
        {
            PlaybackInfo info;
            if (parseSessionsResponse(response, info))
            {
                // Update global playback info using the SharedPlaybackInfo wrapper
                sharedPlayback.update(info);
                LOG_DEBUG_STREAM("Plex", "Updated playback info: " << info.title << " - " << info.username);
            }
            else
            {
                // No active sessions - set empty info
                PlaybackInfo emptyInfo{};
                sharedPlayback.update(emptyInfo);
            }
        }

        // Wait for next poll interval
        std::this_thread::sleep_for(std::chrono::seconds(Config::getInstance().pollInterval));
    }
}

void Plex::setPlaybackInfo(const PlaybackInfo &info)
{
    // Use the new thread-safe wrapper for compatibility
    sharedPlayback.update(info);
}

void Plex::getPlaybackInfo(PlaybackInfo &info) const
{
    // Use the new thread-safe wrapper for compatibility
    info = sharedPlayback.get();
}

PlaybackInfo Plex::getCurrentPlayback() const
{
    // Use the new thread-safe wrapper to get the current playback information
    return sharedPlayback.get();
}

// Request a PIN from the Plex API
bool Plex::requestPlexPin(const std::string &clientId, std::string &pinCode, std::string &pinId)
{
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl)
    {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

        std::string postData = "strong=true";
        postData += "&X-Plex-Product=PlexRichPresence";
        postData += "&X-Plex-Client-Identifier=" + clientId;

        curl_easy_setopt(curl, CURLOPT_URL, "https://plex.tv/api/v2/pins");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK)
        {
            try
            {
                json response = json::parse(readBuffer);
                if (response.contains("id") && response.contains("code"))
                {
                    pinId = std::to_string(response["id"].get<int>());
                    pinCode = response["code"].get<std::string>();
                    return true;
                }
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Plex", "Error parsing PIN response: " + std::string(e.what()));
            }
        }
        else
        {
            LOG_ERROR("Plex", "PIN request failed: " + std::string(curl_easy_strerror(res)));
        }
    }

    return false;
}

// Poll for auth token using the PIN
bool Plex::pollForAuthToken(const std::string &pinId, std::string &clientId)
{
    CURL *curl;
    CURLcode res;

    // Poll for token with a timeout
    const int MAX_ATTEMPTS = 30; // 30 attempts with 2-second delay = 60 seconds total
    const int POLL_DELAY = 2;    // 2 seconds between poll attempts

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++)
    {
        std::string readBuffer;
        curl = curl_easy_init();

        if (curl)
        {
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Accept: application/json");

            std::string url = "https://plex.tv/api/v2/pins/" + pinId + "/?X-Plex-Client-Identifier=" + clientId;

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

            res = curl_easy_perform(curl);

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (res == CURLE_OK)
            {
                try
                {
                    json response = json::parse(readBuffer);

                    // Check if the PIN has been authorized
                    if (response.contains("authToken") && !response["authToken"].is_null())
                    {
                        // Save the auth token
                        std::string token = response["authToken"].get<std::string>();
                        authToken = token;

                        Config::getInstance().setConfigValue("auth_token", token);

                        LOG_DEBUG("Plex", "Auth token received and saved");
                        return true;
                    }
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR("Plex", "Error parsing poll response: " + std::string(e.what()));
                }
            }
        }

        // Wait before trying again
        std::this_thread::sleep_for(std::chrono::seconds(POLL_DELAY));
    }

    LOG_ERROR("Plex", "Timed out waiting for Plex authorization");
    return false;
}