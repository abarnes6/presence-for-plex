#pragma once
#define NOMINMAX
#include "logger.h"
#include "config.h"
#include "models.h"
#include "discord_ipc.h"
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>
#include <cstring>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

class Discord
{
public:
	Discord();
	~Discord();

	void start();
	void stop();
	bool isConnected() const;
	void updatePresence(const PlaybackInfo &playbackInfo);
	void clearPresence();

	// Add callback typedefs and setters
	typedef std::function<void()> ConnectionCallback;
	void setConnectedCallback(ConnectionCallback callback);
	void setDisconnectedCallback(ConnectionCallback callback);

private:
	void connectionThread();
	bool isStillAlive();
	void sendPresenceMessage(const std::string &message);
	bool attemptConnection();

	// Helper methods for creating presence payloads
	nlohmann::json createActivity(const PlaybackInfo &playbackInfo);
	std::string createPresence(const PlaybackInfo &playbackInfo, const std::string &nonce);
	std::string createPresenceMetadata(const PlaybackInfo &playbackInfo, const std::string &nonce);

	// Component responsible for Discord IPC communication
	DiscordIPC ipc;

	std::thread conn_thread;
	std::atomic<bool> running;
	std::atomic<bool> connected;
	std::atomic<bool> needs_reconnect;
	std::atomic<bool> waiting_for_discord; // New state variable
	std::mutex mutex;

	// Connection info
	int reconnect_attempts;

	// State tracking
	std::atomic<bool> is_playing;

	std::atomic<uint64_t> nonce_counter;

	std::string generateNonce();

	// Connection callbacks
	ConnectionCallback onConnected;
	ConnectionCallback onDisconnected;
};
