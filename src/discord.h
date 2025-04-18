#pragma once
#define NOMINMAX
#include <string>
#include <mutex>
#include <thread>
#include <atomic>
#include "config.h"
#include "models.h"

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

private:
	void connectionThread();
	bool connectToDiscord();
	void disconnectFromDiscord();
	bool writeFrame(int opcode, const std::string &data);
	bool readFrame(int &opcode, std::string &data);
	void keepAlive();
	void calculateBackoffTime();

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

#ifdef _WIN32
	void *pipe_handle;
#else
	int pipe_fd;
#endif

	// State tracking
	std::string current_details;
	std::string current_state;
	int64_t start_timestamp;
	std::atomic<bool> is_playing;
};
