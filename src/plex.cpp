#include "plex.h"

using json = nlohmann::json;

Plex::~Plex() {
	// Clean up cURL
	curl_global_cleanup();
	running = false;
	if (pollingThread.joinable()) {
		pollingThread.join();
	}
}

Plex::Plex() {
    curl_global_init(CURL_GLOBAL_ALL);
}

// Start the polling thread
void Plex::startPolling() {
	running = true;
	std::thread pollingThread(&Plex::plexPollingThread, this);
	pollingThread.detach(); // Detach the thread to run independently
}

// Utility function for HTTP requests
size_t Plex::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
        return newLength;
    }
    catch (std::bad_alloc&) {
        return 0;
    }
}

// Function to perform HTTP request to Plex API
std::string Plex::makeRequest(const std::string& url) const {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, ("X-Plex-Token: " + Config::getInstance().authToken).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);

        res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            return "";
        }
    }

    return readBuffer;
}

// Parse Plex API response
bool Plex::parseSessionsResponse(const std::string& response, PlaybackInfo& info) {
    try {
        json j = json::parse(response);

        // Check if there are any active sessions
        if (j["MediaContainer"].contains("size") && j["MediaContainer"]["size"].get<int>() > 0) {
            auto sessions = j["MediaContainer"]["Metadata"];

            // Find session for the account owner
            for (const auto& session : sessions) {
                auto user = session["User"];
                if (user.contains("id")) {
                    info.userId = user["id"].get<std::string>();
                    info.username = user["title"].get<std::string>();

                    info.isPlaying = true;
                    info.title = session["title"].get<std::string>();
                    info.mediaType = session["type"].get<std::string>();

                    if (info.mediaType == "episode" && session.contains("grandparentTitle")) {
                        info.subtitle = session["grandparentTitle"].get<std::string>() +
                            " - S" + session["parentIndex"].get<std::string>() +
                            "E" + session["index"].get<std::string>();
                    }
                    else {
                        info.subtitle = "";
                    }

                    if (session.contains("thumb")) {
                        info.thumbnailUrl = Config::getInstance().serverUrl + session["thumb"].get<std::string>() +
                            "?X-Plex-Token=" + Config::getInstance().authToken;
                    }

                    info.progress = session["viewOffset"].get<int>() / 1000; // Convert from milliseconds to seconds
                    info.duration = session["duration"].get<int>() / 1000;
                    info.startTime = std::time(nullptr) - info.progress;

                    return true;
                }
            }
        }

        // No matching session found
        info.isPlaying = false;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Error parsing Plex response: " << e.what() << std::endl;
        info.isPlaying = false;
        return false;
    }
}


// Polling thread function
void Plex::plexPollingThread() {
    while (running) {
        std::string url = Config::getInstance().serverUrl + "/status/sessions";
        std::string response = makeRequest(url);

        if (!response.empty()) {
            PlaybackInfo info;
            if (parseSessionsResponse(response, info)) {
                // Update global playback info
				setPlaybackInfo(info);
			}
			else {
				// No active sessions
				setPlaybackInfo(PlaybackInfo{});
            }
        }

        // Wait for next poll interval
        std::this_thread::sleep_for(std::chrono::seconds(pollInterval));
    }
}

void Plex::setPlaybackInfo(const PlaybackInfo& info) {
	std::unique_lock<std::shared_mutex> lock(playback_mutex);
	currentPlayback = info;
}
void Plex::getPlaybackInfo(PlaybackInfo& info) {
	std::shared_lock<std::shared_mutex> lock(playback_mutex);
	info = currentPlayback;
}