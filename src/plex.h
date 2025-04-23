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
	bool fetchAndSaveUsername(const std::string &authToken, const std::string &clientId);
	bool pollForPinAuthorization(const std::string &pinId, const std::string &pin,
								 const std::string &clientId, HttpClient &client,
								 const std::map<std::string, std::string> &headers);
	bool requestPlexPin(std::string &pinId, std::string &pin, HttpClient &client,
						const std::map<std::string, std::string> &headers);
	void openAuthorizationUrl(const std::string &pin, const std::string &clientId);

	// Server discovery
	bool fetchServers();
	bool parseServerJson(const std::string &jsonStr);
	void setupServerConnections();
	void setupServerSSEConnection(const std::shared_ptr<PlexServer> &server);

	// Event handling
	void handleSSEEvent(const std::string &serverId, const std::string &event);
	void processPlaySessionStateNotification(const std::string &serverId, const nlohmann::json &notification);
	void updateSessionInfo(const std::string &serverId, const std::string &sessionKey,
						   const std::string &state, const std::string &mediaKey,
						   int64_t viewOffset, const std::shared_ptr<PlexServer> &server);
	void updatePlaybackState(MediaInfo &info, const std::string &state, int64_t viewOffset);

	// Media metadata helpers
	void fetchGrandparentMetadata(const std::string &serverUri, const std::string &accessToken, MediaInfo &info);
	void fetchTMDBArtwork(const std::string &tmdbId, MediaInfo &info);
	void fetchSessionUserInfo(const std::string &serverUri, const std::string &accessToken,
							  const std::string &sessionKey, MediaInfo &info);
	void parseGenres(const nlohmann::json &metadata, MediaInfo &info);
	void parseGuid(const nlohmann::json &metadata, MediaInfo &info);
	bool isAnimeContent(const nlohmann::json &metadata);
	void fetchAnimeMetadata(MediaInfo &info);
	void extractBasicMediaInfo(const nlohmann::json &metadata, MediaInfo &info);
	void extractMovieSpecificInfo(const nlohmann::json &metadata, MediaInfo &info);
	void extractTVShowSpecificInfo(const nlohmann::json &metadata, MediaInfo &info);

	// Media details
	MediaInfo fetchMediaDetails(const std::string &serverUri, const std::string &accessToken,
								const std::string &mediaKey);

	// Generate X-Plex-Client-Identifier if needed
	std::string getClientIdentifier();
	std::map<std::string, std::string> getStandardHeaders(const std::string &token = "");

	// Data members
	std::map<std::string, MediaInfo> m_activeSessions;
	std::mutex m_sessionMutex;
	std::atomic<bool> m_initialized;
};