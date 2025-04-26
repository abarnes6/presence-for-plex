#pragma once

// Standard library headers
#include <atomic>
#include <chrono>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Platform-specific headers
#ifdef _WIN32
#include <WinSock2.h>
#include <shellapi.h>
#endif

// Third-party headers
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// Project headers
#include "config.h"
#include "http_client.h"
#include "logger.h"
#include "models.h"
#include "uuid.h"

// Forward declarations for cache structures
struct TMDBCacheEntry;
struct MALCacheEntry;
struct MediaCacheEntry;
struct SessionUserCacheEntry;
struct ServerUriCacheEntry;

class Plex
{
public:
	Plex();
	~Plex();

	// Initialize connection to Plex
	bool init();

	// Get current playback status
	MediaInfo getCurrentPlayback();

	// Stop all connections
	void stop();

private:
	// Helper methods
	std::map<std::string, std::string> getStandardHeaders(const std::string &token = "");

	// State variables
	std::atomic<bool> m_initialized;
	std::atomic<bool> m_shuttingDown;

	// Cache mutexes and maps
	std::mutex m_cacheMutex;
	std::map<std::string, TMDBCacheEntry> m_tmdbArtworkCache;
	std::map<std::string, MALCacheEntry> m_malIdCache;
	std::map<std::string, MediaCacheEntry> m_mediaInfoCache;
	std::map<std::string, SessionUserCacheEntry> m_sessionUserCache;
	std::map<std::string, ServerUriCacheEntry> m_serverUriCache;

	// Active sessions
	std::mutex m_sessionMutex;
	std::map<std::string, MediaInfo> m_activeSessions;

	// Authentication methods
	bool acquireAuthToken();
	bool requestPlexPin(std::string &pinId, std::string &pin, HttpClient &client,
						const std::map<std::string, std::string> &headers);
	void openAuthorizationUrl(const std::string &pin, const std::string &clientId);
	bool pollForPinAuthorization(const std::string &pinId, const std::string &pin,
								 const std::string &clientId, HttpClient &client,
								 const std::map<std::string, std::string> &headers);
	bool fetchAndSaveUsername(const std::string &authToken, const std::string &clientId);
	std::string getClientIdentifier();

	// Server methods
	bool fetchServers();
	bool parseServerJson(const std::string &jsonStr);
	void setupServerConnections();
	void setupServerSSEConnection(const std::shared_ptr<PlexServer> &server);

	// Event handling methods
	void handleSSEEvent(const std::string &serverId, const std::string &event);
	void processPlaySessionStateNotification(const std::string &serverId, const nlohmann::json &notification);
	void updateSessionInfo(const std::string &serverId, const std::string &sessionKey,
						   const std::string &state, const std::string &mediaKey,
						   int64_t viewOffset, const std::shared_ptr<PlexServer> &server);
	void updatePlaybackState(MediaInfo &info, const std::string &state, int64_t viewOffset);
	std::string urlEncode(const std::string &value);

	// Media info methods
	MediaInfo fetchMediaDetails(const std::string &serverUri, const std::string &accessToken,
								const std::string &mediaKey);
	void extractBasicMediaInfo(const nlohmann::json &metadata, MediaInfo &info);
	void extractMovieSpecificInfo(const nlohmann::json &metadata, MediaInfo &info);
	void extractTVShowSpecificInfo(const nlohmann::json &metadata, MediaInfo &info);
	void fetchGrandparentMetadata(const std::string &serverUrl, const std::string &accessToken,
								  MediaInfo &info);
	void parseGuid(const nlohmann::json &metadata, MediaInfo &info);
	void parseGenres(const nlohmann::json &metadata, MediaInfo &info);
	bool isAnimeContent(const nlohmann::json &metadata);
	void fetchAnimeMetadata(const nlohmann::json &metadata, MediaInfo &info);
	void fetchTMDBArtwork(const std::string &tmdbId, MediaInfo &info);
	std::string fetchSessionUsername(const std::string &serverUri, const std::string &accessToken,
									 const std::string &sessionKey);
	std::string getPreferredServerUri(const std::shared_ptr<PlexServer> &server);
	void extractMusicSpecificInfo(const nlohmann::json &metadata, MediaInfo &info,
								  const std::string &serverUri, const std::string &accessToken);
};