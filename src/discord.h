#pragma once
#define NOMINMAX

// Standard library headers
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

// Platform-specific headers
#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

// Third-party headers
#include <nlohmann/json.hpp>

// Project headers
#include "config.h"
#include "discord_ipc.h"
#include "logger.h"
#include "models.h"

/**
 * Main interface for Discord Rich Presence integration
 *
 * This class handles all high-level communication with Discord, including:
 * - Connection and reconnection management
 * - Presence updates for media playback
 * - Status monitoring and error handling
 */
class Discord
{
public:
	/**
	 * Constructor initializes Discord Rich Presence state
	 */
	Discord();

	/**
	 * Destructor ensures proper shutdown of Discord connections
	 */
	~Discord();

	/**
	 * Starts the Discord Rich Presence background thread
	 * This initiates connection to Discord and begins monitoring
	 */
	void start();

	/**
	 * Stops the Discord Rich Presence service
	 * Closes connections and terminates the background thread
	 */
	void stop();

	/**
	 * Checks if currently connected to Discord
	 *
	 * @return true if connected, false otherwise
	 */
	bool isConnected() const;

	/**
	 * Updates Discord Rich Presence with current media information
	 *
	 * @param info The media information to display in Discord
	 */
	void updatePresence(const MediaInfo &info);

	/**
	 * Clears the current Discord Rich Presence
	 * Called when media playback stops
	 */
	void clearPresence();

	// Add callback typedefs and setters
	/**
	 * Callback type for connection state changes
	 */
	typedef std::function<void()> ConnectionCallback;

	/**
	 * Sets the callback to invoke when connection to Discord is established
	 *
	 * @param callback Function to call when connected
	 */
	void setConnectedCallback(ConnectionCallback callback);

	/**
	 * Sets the callback to invoke when connection to Discord is lost
	 *
	 * @param callback Function to call when disconnected
	 */
	void setDisconnectedCallback(ConnectionCallback callback);

private:
	/**
	 * Persistent connection thread to Discord IPC
	 *
	 * This thread handles the connection to Discord and keeps it alive.
	 * It also handles reconnections in case of disconnection with exponential backoff.
	 *
	 * Connection flow:
	 * 1. Attempt to open pipe connection to Discord client
	 * 2. Send handshake message with configured client ID
	 * 3. Wait for and validate handshake response
	 * 4. If successful, connection is established
	 * 5. Periodically check connection health with pings
	 */
	void connectionThread();

	/**
	 * Checks if Discord connection is still alive by sending a ping
	 *
	 * @return true if connection is healthy, false otherwise
	 */
	bool isStillAlive();

	/**
	 * Sends a presence update message to Discord
	 *
	 * @param message The JSON payload to send
	 */
	void sendPresenceMessage(const std::string &message);

	/**
	 * Attempts to establish a connection to Discord
	 *
	 * @return true if connection was successful, false otherwise
	 */
	bool attemptConnection();

	/**
	 * Creates the activity portion of the Discord Rich Presence payload
	 *
	 * @param info Media information to display
	 * @return JSON object representing the activity
	 */
	nlohmann::json createActivity(const MediaInfo &info);

	/**
	 * Creates the primary Discord presence payload
	 *
	 * @param info Media information to display
	 * @param nonce Unique identifier for this update
	 * @return JSON string representation of the presence payload
	 */
	std::string createPresence(const MediaInfo &info, const std::string &nonce);

	/**
	 * Creates the metadata portion of the Discord presence payload
	 *
	 * @param info Media information to display
	 * @param nonce Unique identifier for this update
	 * @return JSON string representation of the metadata payload
	 */
	std::string createPresenceMetadata(const MediaInfo &info, const std::string &nonce);

	/**
	 * Generates a unique nonce string for Discord messages
	 *
	 * @return A unique string identifier
	 */
	std::string generateNonce();

	/** Component responsible for Discord IPC communication */
	DiscordIPC ipc;

	/** Background thread for connection management */
	std::thread conn_thread;

	/** Flag indicating if the connection thread should continue running */
	std::atomic<bool> running;

	/** Flag indicating if reconnection is needed */
	std::atomic<bool> needs_reconnect;

	/** Flag indicating if we're waiting for Discord to start */
	std::atomic<bool> waiting_for_discord;

	/** Mutex for thread synchronization */
	std::mutex mutex;

	/** Number of consecutive connection attempts */
	int reconnect_attempts;

	/** Flag indicating if media is currently playing */
	std::atomic<bool> is_playing;

	/** Counter for generating unique message identifiers */
	std::atomic<uint64_t> nonce_counter;

	/** Callback for successful connection */
	ConnectionCallback onConnected;

	/** Callback for disconnection */
	ConnectionCallback onDisconnected;
};
