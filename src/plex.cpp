#include "plex.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <sqlite3.h>

using json = nlohmann::json;

Plex::~Plex()
{
    // Clean up cURL
    curl_global_cleanup();
    running = false;
    if (pollingThread.joinable())
    {
        pollingThread.join();
    }
}

Plex::Plex()
{
    curl_global_init(CURL_GLOBAL_ALL);
    authToken = getAuthTokenFromBrowser();
}

// Start the polling thread
void Plex::startPolling()
{
    running = true;
    std::thread pollingThread(&Plex::plexPollingThread, this);
    pollingThread.detach(); // Detach the thread to run independently
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
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
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
            std::cerr << "Invalid Plex response format: Response doesn't appear to be JSON" << std::endl;
            if (!response.empty())
            {
                std::cerr << "Response begins with: " << response.substr(0, std::min(50UL, response.size())) << "..." << std::endl;
            }
            info.isPlaying = false;
            return false;
        }

        json j = json::parse(response);

        // Check if there are any active sessions
        if (j["MediaContainer"].contains("size") && j["MediaContainer"]["size"].get<int>() > 0)
        {
            auto sessions = j["MediaContainer"]["Metadata"];

            // Find session for the account owner
            for (const auto &session : sessions)
            {
                auto user = session["User"];
                if (user.contains("id"))
                {
                    // Handle different types properly with appropriate conversions
                    info.isPlaying = true;

                    // Handle user ID - could be number or string
                    if (user["id"].is_string())
                        info.userId = user["id"].get<std::string>();
                    else if (user["id"].is_number())
                        info.userId = std::to_string(user["id"].get<int>());

                    info.username = user["title"].get<std::string>();
                    info.title = session["title"].get<std::string>();
                    info.mediaType = session["type"].get<std::string>();

                    if (info.mediaType == "episode" && session.contains("grandparentTitle"))
                    {
                        std::string seasonNum = session["parentIndex"].is_string() ? session["parentIndex"].get<std::string>() : std::to_string(session["parentIndex"].get<int>());

                        std::string episodeNum = session["index"].is_string() ? session["index"].get<std::string>() : std::to_string(session["index"].get<int>());

                        info.subtitle = session["grandparentTitle"].get<std::string>() +
                                        " - S" + seasonNum +
                                        "E" + episodeNum;
                    }
                    else
                    {
                        info.subtitle = "";
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
        info.isPlaying = false;
        return false;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error parsing Plex response: " << e.what() << std::endl;
        info.isPlaying = false;
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
                // Update global playback info
                setPlaybackInfo(info);
                std::cout << "Updated playback info: " << info.title << " - " << info.username << std::endl;
            }
            else
            {
                // No active sessions
                setPlaybackInfo(PlaybackInfo{});
            }
        }

        // Wait for next poll interval
        std::this_thread::sleep_for(std::chrono::seconds(Config::getInstance().pollInterval));
    }
}

void Plex::setPlaybackInfo(const PlaybackInfo &info)
{
    std::unique_lock<std::shared_mutex> lock(playback_mutex);
    currentPlayback = info;
}
void Plex::getPlaybackInfo(PlaybackInfo &info)
{
    std::shared_lock<std::shared_mutex> lock(playback_mutex);
    info = currentPlayback;
}