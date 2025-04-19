#pragma once
#define NOMINMAX
#include "models.h"
#include "config.h"
#include "uuid.h"
#include "logger.h"
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

#ifdef _WIN32
#include <winsock2.h>
#endif

class Plex
{
public:
	Plex();
	~Plex();
	void startPolling();
	void stopPolling();
	
	// Updated methods to use the shared wrapper
	PlaybackInfo getCurrentPlayback() const;
	
	// We'll keep these methods for backward compatibility
	void setPlaybackInfo(const PlaybackInfo &info);
	void getPlaybackInfo(PlaybackInfo &info) const;

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
	
	// Replace raw mutex and PlaybackInfo with SharedPlaybackInfo
	SharedPlaybackInfo sharedPlayback;
	
	std::string authToken;
	std::string url;
};