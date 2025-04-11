#pragma once
#include "models.h"
#include "config.h"
#include <string>
#include <iostream>
#include <mutex>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

class Plex {
public:
	Plex();
	~Plex();
	void startPolling();
	void setPlaybackInfo(const PlaybackInfo& info);
	void getPlaybackInfo(PlaybackInfo& info);
	PlaybackInfo currentPlayback;
private:
	static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s);
	void plexPollingThread();
	bool parseSessionsResponse(const std::string& response, PlaybackInfo& info);
	std::string makeRequest(const std::string& url) const;
	bool running = false;
	std::thread pollingThread;
	int pollInterval;
	std::shared_mutex playback_mutex;
};