#pragma once
#define NOMINMAX
#include "models.h"
#include "config.h"
#include "uuid.h"
#include <string>
#include <iostream>
#include <mutex>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <shared_mutex>
#include <filesystem>
#include <fstream>
#include <regex>

class Plex
{
public:
	Plex();
	~Plex();
	void startPolling();
	void stopPolling();
	void setPlaybackInfo(const PlaybackInfo &info);
	void getPlaybackInfo(PlaybackInfo &info) const;
	PlaybackInfo getCurrentPlayback() const;

private:
	static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s);
	void plexPollingThread();
	bool parseSessionsResponse(const std::string &response, PlaybackInfo &info);
	std::string makeRequest(const std::string &url) const;
	std::string getPlexDirectHash() const;
	bool requestPlexPin(const std::string &clientId, std::string &pinCode, std::string &pinId);
	bool pollForAuthToken(const std::string &pinId, std::string &clientId);

	std::atomic<bool> running{false};
	std::thread pollingThread;
	mutable std::shared_mutex playback_mutex;
	PlaybackInfo currentPlayback;
	std::string authToken;
};