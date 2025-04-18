#pragma once
#define NOMINMAX
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include "config.h"
#include "models.h"
#include "discord_ipc.h"
#include "logger.h"

class Discord
{
public:
	Discord();
	~Discord();

	bool init();
	void start();
	void stop();
	bool isConnected() const;
	void updatePresence(const PlaybackInfo &playbackInfo);
	void clearPresence();
	void keepAlive();

private:
	void connectionThread();
	void calculateBackoffTime();

	// Component responsible for Discord IPC communication
	DiscordIPC ipc;

	std::thread conn_thread;
	std::atomic<bool> running;
	std::atomic<bool> connected;
	std::atomic<bool> needs_reconnect;
	std::mutex mutex;

	// Connection info
	uint64_t client_id;
	int reconnect_attempts;
	int64_t last_successful_update;
	std::string last_activity_payload;

	// State tracking
	std::atomic<bool> is_playing;
	PlaybackInfo previous_playback_info; // Store previous playback info to detect changes
};
