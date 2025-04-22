#include "discord.h"

using json = nlohmann::json;

Discord::Discord() : running(false),
					 connected(false),
					 needs_reconnect(false),
					 reconnect_attempts(0),
					 is_playing(false),
					 nonce_counter(0),
					 onConnected(nullptr),
					 onDisconnected(nullptr)
{
}

Discord::~Discord()
{
	stop();
}

/* Persistent connection to Discord IPC
   This thread handles the connection to Discord and keeps it alive.
   It also handles reconnections in case of disconnection.
   The thread will run until the `running` flag is set to false.
   The thread will attempt to reconnect with exponential backoff if the connection fails.

   How it works:
   1. The thread will attempt to connect to Discord IPC using the `ipc.openPipe()` method.
   2. If the connection is successful, it will send a handshake message to the Discord client.
   3. It will then attempt to read the handshake response.
   4. If the handshake is successful, it will set the `connected` flag to true.

*/
void Discord::connectionThread()
{
	LOG_DEBUG("Discord", "Connection thread started");
	while (running)
	{
		// Handle connection logic
		if (!connected)
		{
			LOG_DEBUG("Discord", "Not connected, attempting connection");

			// Calculate exponential backoff delay
			if (reconnect_attempts > 0)
			{
				int delay = std::min(5 * reconnect_attempts, 60);
				LOG_INFO("Discord", "Reconnection attempt " + std::to_string(reconnect_attempts) +
										", waiting " + std::to_string(delay) + " seconds");

				for (int i = 0; i < delay && running; ++i)
				{
					if (!running)
						break;
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
			}

			reconnect_attempts++;

			// Attempt to connect
			if (!attemptConnection())
			{
				LOG_WARNING("Discord", "Failed to connect to Discord IPC, will retry later");
				continue;
			}

			// If we got here, connection was successful
			connected = true;
			reconnect_attempts = 0;
			LOG_INFO("Discord", "Successfully connected to Discord");

			// Call connected callback if set
			if (onConnected)
			{
				onConnected();
			}
		}
		else
		{
			LOG_DEBUG("Discord", "Checking Discord alive status");

			if (!isStillAlive())
			{
				LOG_INFO("Discord", "Connection to Discord lost, will reconnect");
				ipc.closePipe();
				connected = false;
				needs_reconnect = false;

				// Call disconnected callback if set
				if (onDisconnected)
				{
					onDisconnected();
				}
				continue;
			}

			// Connection is healthy, wait before next check
			for (int i = 0; i < 30 && running; ++i)
			{
				if (!running)
					break;
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}
	}
}

// Helper method to encapsulate connection logic
bool Discord::attemptConnection()
{
	if (!ipc.openPipe())
	{
		return false;
	}

	LOG_DEBUG("Discord", "Connection established, sending handshake");
	LOG_DEBUG("Discord", "Using client ID: " + std::to_string(Config::getInstance().getClientId()));

	if (!ipc.sendHandshake(Config::getInstance().getClientId()))
	{
		LOG_ERROR("Discord", "Handshake write failed");
		ipc.closePipe();
		return false;
	}

	int opcode;
	std::string response;
	LOG_DEBUG("Discord", "Waiting for handshake response");
	if (!ipc.readFrame(opcode, response) || opcode != OP_FRAME)
	{
		LOG_ERROR("Discord", "Failed to read handshake response. Opcode: " + std::to_string(opcode));
		if (!response.empty())
		{
			LOG_DEBUG("Discord", "Response content: " + response);
		}
		ipc.closePipe();
		return false;
	}

	LOG_DEBUG("Discord", "Handshake response received");

	try
	{
		LOG_DEBUG("Discord", "Parsing response: " + response);
		json ready = json::parse(response);

		if (!ready.contains("evt"))
		{
			LOG_ERROR("Discord", "Discord response missing 'evt' field");
			LOG_DEBUG("Discord", "Complete response: " + response);
			ipc.closePipe();
			return false;
		}

		if (ready["evt"] != "READY")
		{
			LOG_ERROR("Discord", "Discord did not respond with READY event: " + ready["evt"].dump());
			ipc.closePipe();
			return false;
		}
		LOG_DEBUG("Discord", "Handshake READY event confirmed");
		return true;
	}
	catch (const json::parse_error &e)
	{
		LOG_ERROR_STREAM("Discord", "JSON parse error in READY response: " << e.what() << " at position " << e.byte);
		LOG_ERROR_STREAM("Discord", "Response that caused the error: " << response);
	}
	catch (const json::type_error &e)
	{
		LOG_ERROR_STREAM("Discord", "JSON type error in READY response: " << e.what());
		LOG_ERROR_STREAM("Discord", "Response that caused the error: " << response);
	}
	catch (const std::exception &e)
	{
		LOG_ERROR("Discord", "Failed to parse READY response: " + std::string(e.what()));
		LOG_ERROR("Discord", "Response that caused the error: " + response);
	}

	ipc.closePipe();
	return false;
}

void Discord::updatePresence(const PlaybackInfo &playbackInfo)
{
	LOG_DEBUG_STREAM("Discord", "updatePresence called for title: " << playbackInfo.title);

	if (!connected || !ipc.isConnected())
	{
		LOG_ERROR("Discord", "Can't update presence: not connected to Discord");
		return;
	}

	std::lock_guard<std::mutex> lock(mutex);

	if (playbackInfo.state == PlaybackState::Playing ||
		playbackInfo.state == PlaybackState::Paused ||
		playbackInfo.state == PlaybackState::Buffering)
	{
		LOG_DEBUG_STREAM("Discord", "Media is " << (playbackInfo.state == PlaybackState::Playing ? "playing" : (playbackInfo.state == PlaybackState::Paused ? "paused" : "buffering"))
												<< ", updating presence");
		is_playing = true;

		std::string nonce = generateNonce();

		std::string presence = createPresence(playbackInfo, nonce);
		// std::string presenceMetadata = createPresenceMetadata(playbackInfo, nonce);

		sendPresenceMessage(presence);
		// sendActivityMessage(presenceMetadata);

		LOG_DEBUG_STREAM("Discord", "Updated presence: " << playbackInfo.title
														 << " - " << playbackInfo.username
														 << (playbackInfo.state == PlaybackState::Paused ? " (Paused)" : "")
														 << (playbackInfo.state == PlaybackState::Buffering ? " (Buffering)" : ""));
	}
	else
	{
		// Clear presence if not playing anymore
		if (is_playing)
		{
			LOG_INFO("Discord", "Media stopped, clearing presence");
			clearPresence();
		}
	}
}

std::string Discord::generateNonce()
{
	return std::to_string(++nonce_counter);
}

// Create the primary Discord presence payload
std::string Discord::createPresence(const PlaybackInfo &playbackInfo, const std::string &nonce)
{
	json activity = createActivity(playbackInfo);

#ifdef _WIN32
	auto process_id = static_cast<int>(GetCurrentProcessId());
#else
	auto process_id = static_cast<int>(getpid());
#endif

	json args = {
		{"pid", process_id},
		{"activity", activity}};

	json presence = {{"cmd", "SET_ACTIVITY"}, {"args", args}, {"nonce", nonce}};

	return presence.dump();
}

// Create the secondary Discord presence payload
std::string Discord::createPresenceMetadata(const PlaybackInfo &playbackInfo, const std::string &nonce)
{
	json activity = createActivity(playbackInfo);

	json presence = {
		{"cmd", "SET_ACTIVITY"},
		{"data", activity},
		{"evt", nullptr},
		{"nonce", nonce}};

	return presence.dump();
}

// Helper function to create the activity JSON structure
json Discord::createActivity(const PlaybackInfo &playbackInfo)
{
	std::string state = playbackInfo.seasonEpisode + " · " + playbackInfo.episodeName;

	if (playbackInfo.state == PlaybackState::Buffering)
	{
		state = "🔄 Buffering...";
	}
	else if (playbackInfo.state == PlaybackState::Paused)
	{
		state = "⏸️ " + state;
	}

	auto details = playbackInfo.title;

	json assets = {{"large_image", "plex_logo"},
				   {"large_text", playbackInfo.title},
				   {"small_image", "plex_logo"},
				   {"small_text", playbackInfo.username}};

	auto now = std::chrono::system_clock::now();
	int64_t current_time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
	int64_t start_timestamp = 0;
	int64_t end_timestamp = 0;

	if (playbackInfo.state == PlaybackState::Playing)
	{
		start_timestamp = current_time - static_cast<int64_t>(playbackInfo.progress);
		end_timestamp = current_time + static_cast<int64_t>(playbackInfo.duration - playbackInfo.progress);
	}
	else if (playbackInfo.state == PlaybackState::Paused || playbackInfo.state == PlaybackState::Buffering)
	{
		start_timestamp = current_time + static_cast<int64_t>(playbackInfo.duration);
		end_timestamp = current_time + (static_cast<int64_t>(playbackInfo.duration) * 2);
	}

	json timestamps = {
		{"start", start_timestamp},
		{"end", end_timestamp}};

	return {
		{"type", 3}, // 3 = "Watching"
		{"state", state},
		{"details", details},
		{"timestamps", timestamps},
		{"assets", assets},
		{"instance", false}};
}

void Discord::sendPresenceMessage(const std::string &message)
{
	if (!ipc.writeFrame(OP_FRAME, message))
	{
		LOG_ERROR("Discord", "Failed to send presence update");
		needs_reconnect = true;
		return;
	}

	int opcode;
	std::string response;
	if (ipc.readFrame(opcode, response))
	{
		try
		{
			json response_json = json::parse(response);
			if (response_json.contains("evt") && response_json["evt"] == "ERROR")
			{
				LOG_ERROR_STREAM("Discord", "Discord rejected presence update: " << response);
			}
		}
		catch (const std::exception &e)
		{
			LOG_ERROR_STREAM("Discord", "Failed to parse response: " << e.what());
		}
	}
	else
	{
		LOG_ERROR("Discord", "Failed to read Discord response");
	}
}

void Discord::clearPresence()
{
	LOG_DEBUG("Discord", "clearPresence called");
	if (!connected || !ipc.isConnected())
	{
		LOG_ERROR("Discord", "Can't clear presence: not connected to Discord");
		return;
	}

	is_playing = false;

#ifdef _WIN32
	auto process_id = static_cast<int>(GetCurrentProcessId());
#else
	auto process_id = static_cast<int>(getpid());
#endif
	// Create empty presence payload to clear current presence
	json presence = {
		{"cmd", "SET_ACTIVITY"},
		{"args", {{"pid", process_id}, {"activity", nullptr}}},
		{"nonce", generateNonce()}};

	std::string presence_str = presence.dump();

	sendPresenceMessage(presence_str);
}

bool Discord::isStillAlive()
{
	if (!ipc.sendPing())
	{
		LOG_ERROR("Discord", "Failed to send ping");
		return false;
	}

	// Read and process PONG response
	int opcode;
	std::string response;
	if (ipc.readFrame(opcode, response))
	{
		if (opcode != OP_PONG)
		{
			LOG_WARNING_STREAM("Discord", "Unexpected response to PING. Opcode: " << opcode);
			return false;
		}
	}
	else
	{
		LOG_ERROR("Discord", "Failed to read PONG response");
		return false;
	}

	return true;
}

// Lifecycle control
void Discord::start()
{
	LOG_INFO("Discord", "Starting Discord Rich Presence");
	running = true;
	conn_thread = std::thread(&Discord::connectionThread, this);
}

void Discord::stop()
{
	LOG_INFO("Discord", "Stopping Discord Rich Presence");
	running = false;

	// Call disconnected callback if currently connected
	if (connected && onDisconnected)
	{
		onDisconnected();
	}

	if (conn_thread.joinable())
	{
		conn_thread.join();
	}
	ipc.closePipe();
}

bool Discord::isConnected() const
{
	return connected && ipc.isConnected();
}

void Discord::setConnectedCallback(ConnectionCallback callback)
{
	onConnected = callback;
}

void Discord::setDisconnectedCallback(ConnectionCallback callback)
{
	onDisconnected = callback;
}
