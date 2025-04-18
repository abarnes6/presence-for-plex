#pragma once
#include "models.h"
#include "config.h"
#include <string>
#include <iostream>
#include <mutex>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <shared_mutex>
#include <atomic>

class Plex
{
public:
	Plex();
	~Plex();
	void startPolling();
	void setPlaybackInfo(const PlaybackInfo &info);
	void getPlaybackInfo(PlaybackInfo &info);
	PlaybackInfo currentPlayback;

private:
	static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s);
	void plexPollingThread();
	bool parseSessionsResponse(const std::string &response, PlaybackInfo &info);
	std::string makeRequest(const std::string &url) const;
	std::string getPlexDirectHash() const;
	bool running = false;
	std::thread pollingThread;
	std::shared_mutex playback_mutex;
	std::string authToken;
};