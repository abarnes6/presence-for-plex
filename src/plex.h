#pragma once

#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>
#include "models.h"
#include "http_client.h"

// Server information class

class Plex
{
public:
	Plex();
	~Plex();

	// Initialize connection to Plex
	bool init();

	// Get the current playback information (across all servers)
	MediaInfo getCurrentPlayback();

	// Explicitly stop all server connections
	void stopConnections();

private:
	// Authentication
	bool acquireAuthToken();

	// Server discovery
	bool fetchServers();
	bool parseServerJson(const std::string &jsonStr);

	// Event handling
	void setupServerConnections();
	void handleSSEEvent(const std::string &serverId, const std::string &event);
	void processPlaySessionStateNotification(const std::string &serverId, const nlohmann::json &notification);
	void fetchGrandparentArtwork(const std::string &serverUri, const std::string &accessToken,
								 const std::string &grandparentKey, MediaInfo &info);
	void fetchTMDBMovieArtwork(const std::string &tmdbId, MediaInfo &info);
	void fetchTMDBTVShowArtwork(const std::string &tmdbId, MediaInfo &info);

	// Media details
	MediaInfo fetchMediaDetails(const std::string &serverUri, const std::string &accessToken,
								const std::string &mediaKey);

	// Generate X-Plex-Client-Identifier if needed
	std::string getClientIdentifier();

	// Data members
	std::map<std::string, std::shared_ptr<PlexServer>> m_servers;
	std::map<std::string, MediaInfo> m_activeSessions;
	std::mutex m_sessionMutex;
	std::atomic<bool> m_initialized;
};