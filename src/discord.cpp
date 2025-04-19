#include "discord.h"

using json = nlohmann::json;

Discord::Discord() : 
	running(false),
	connected(false),
	needs_reconnect(false),
	waiting_for_discord(false),
	client_id(0),
	reconnect_attempts(0),
	last_successful_update(0),
	is_playing(false)

{
}

Discord::~Discord()
{
	stop();
}

bool Discord::init()
{
	LOG_INFO("Discord", "Initializing Discord Rich Presence");
	running = true;
	client_id = Config::getInstance().getClientId();
	LOG_DEBUG("Discord", "Using client ID: " + std::to_string(client_id));
	conn_thread = std::thread(&Discord::connectionThread, this);
	return true;
}

void Discord::connectionThread()
{
	LOG_DEBUG("Discord", "Connection thread started");
	while (running)
	{
		if (!connected)
		{
			LOG_DEBUG("Discord", "Not connected, attempting connection");
			if (ipc.connect())
			{
				// Successfully connected to the Discord pipe
				waiting_for_discord = false;
				LOG_DEBUG("Discord", "Connection established, sending handshake");

				// Send handshake to Discord
				if (!ipc.sendHandshake(client_id))
				{
					LOG_ERROR("Discord", "Handshake write failed");
					ipc.disconnect();
					calculateBackoffTime();
					continue;
				}

				// Read handshake response
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
					ipc.disconnect();
					calculateBackoffTime();
					continue;
				}

				LOG_DEBUG("Discord", "Handshake response received");

				// Verify READY was received
				json ready;
				try
				{
					LOG_DEBUG("Discord", "Parsing response: " + response);
					ready = json::parse(response);
					
					if (!ready.contains("evt"))
					{
						LOG_ERROR("Discord", "Discord response missing 'evt' field");
						LOG_DEBUG("Discord", "Complete response: " + response);
						ipc.disconnect();
						calculateBackoffTime();
						continue;
					}
					
					if (ready["evt"] != "READY")
					{
						LOG_ERROR("Discord", "Discord did not respond with READY event: " + ready["evt"].dump());
						ipc.disconnect();
						calculateBackoffTime();
						continue;
					}
					LOG_DEBUG("Discord", "Handshake READY event confirmed");
				}
				catch (const json::parse_error& e)
				{
					LOG_ERROR_STREAM("Discord", "JSON parse error in READY response: " << e.what() << " at position " << e.byte);
					LOG_ERROR_STREAM("Discord", "Response that caused the error: " << response);
					ipc.disconnect();
					calculateBackoffTime();
					continue;
				}
				catch (const json::type_error& e)
				{
					LOG_ERROR_STREAM("Discord", "JSON type error in READY response: " << e.what());
					LOG_ERROR_STREAM("Discord", "Response that caused the error: " << response);
					ipc.disconnect();
					calculateBackoffTime();
					continue;
				}
				catch (const std::exception &e)
				{
					LOG_ERROR("Discord", "Failed to parse READY response: " + std::string(e.what()));
					LOG_ERROR("Discord", "Response that caused the error: " + response);
					ipc.disconnect();
					calculateBackoffTime();
					continue;
				}

				connected = true;
				reconnect_attempts = 0; // Reset attempts after successful connection
				LOG_INFO("Discord", "Successfully connected to Discord");

				// If we have a last activity cached, restore it immediately
				if (!last_activity_payload.empty())
				{
					LOG_DEBUG("Discord", "Restoring previous activity state");
					if (!ipc.writeFrame(OP_FRAME, last_activity_payload))
					{
						LOG_ERROR("Discord", "Failed to restore activity state");
						needs_reconnect = true;
					}
					else
					{
						// Read response to verify
						if (ipc.readFrame(opcode, response))
						{
							LOG_DEBUG("Discord", "Restore activity response received");
						}
						else
						{
							LOG_ERROR("Discord", "Failed to read response after restoring activity");
						}
					}
				}
			}
			else
			{
				// Wait before retrying with exponential backoff
				LOG_WARNING("Discord", "Failed to connect to Discord IPC");
				waiting_for_discord = true; // Set the waiting state
				LOG_INFO("Discord", "Waiting for Discord to open...");
				calculateBackoffTime();
			}
		}
		else
		{
			// Keep connection alive with periodic pings
			static auto last_ping = std::chrono::steady_clock::now();
			auto now = std::chrono::steady_clock::now();
			if (now - last_ping > std::chrono::seconds(15))
			{
				LOG_DEBUG("Discord", "Sending keep-alive ping");
				keepAlive();
				last_ping = std::chrono::steady_clock::now();
			}

			if (needs_reconnect)
			{
				LOG_INFO("Discord", "Reconnecting to Discord");
				ipc.disconnect();
				connected = false;
				needs_reconnect = false;
				continue;
			}

			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}
}

void Discord::calculateBackoffTime()
{
	// Exponential backoff: 2^n seconds with a max of 60 seconds
	int backoff_secs = std::min(1 << std::min(reconnect_attempts, 5), 60);
	reconnect_attempts++;
	LOG_DEBUG("Discord", "Waiting " + std::to_string(backoff_secs) + " seconds before reconnecting (attempt " + std::to_string(reconnect_attempts) + ")");
	std::this_thread::sleep_for(std::chrono::seconds(backoff_secs));
}

void Discord::updatePresence(const PlaybackInfo &playbackInfo)
{
	LOG_DEBUG_STREAM("Discord", "updatePresence called for title: " << playbackInfo.title);

    if (!connected || !ipc.isConnected())
    {
        LOG_ERROR("Discord", "Can't update presence: not connected to Discord");
        return;
    }
    
    // Rate limiting - don't allow more than 5 updates per 20 seconds
    auto now_time = std::chrono::steady_clock::now();
    auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(now_time.time_since_epoch()).count();
    int64_t seconds_since_last_update = now_seconds - last_successful_update;

    if (seconds_since_last_update < 4) // 20s ÷ 5 = 4s minimum between updates
    {
        LOG_DEBUG("Discord", "Rate limiting: skipping presence update (too soon)");
        return;
    }

    // Acquire lock only for accessing shared resources
    std::lock_guard<std::mutex> lock(mutex);

	int64_t start_timestamp = 0;
	int64_t end_timestamp = 0;

	if (playbackInfo.state == PlaybackState::Playing || playbackInfo.state == PlaybackState::Paused)
	{
		is_playing = true;
		LOG_DEBUG_STREAM("Discord", "Media is " << 
            (playbackInfo.state == PlaybackState::Playing ? "playing" : "paused") << 
            ", updating presence");

		// Calculate timestamps
		auto now = std::chrono::system_clock::now();
		int64_t current_time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
		
		// Calculate timestamps differently based on playback state
        if (playbackInfo.state == PlaybackState::Playing) {
            // For "Playing" state, show elapsed and remaining time with counting timestamps
            start_timestamp = current_time - static_cast<int64_t>(playbackInfo.progress);
            end_timestamp = current_time + static_cast<int64_t>(playbackInfo.duration - playbackInfo.progress);
        } else if (playbackInfo.state == PlaybackState::Paused) {
            // For "Paused" state, use a fixed timestamp showing current progress
            // By setting a timestamp in the past without an end timestamp, Discord shows a fixed elapsed time
            start_timestamp = 1;
			end_timestamp = 1;
            // No end_timestamp for paused state - this makes Discord show only elapsed time that doesn't count up
        }

		LOG_DEBUG("Discord", "Updating presence");
		
		// Create timestamps object 
		json timestamps = {
			{"start", start_timestamp},
			{"end", end_timestamp}
		};

        // Create state text with paused indicator if needed
        std::string state_text = playbackInfo.seasonEpisode + " · " + playbackInfo.episodeName;
        if (playbackInfo.state == PlaybackState::Paused) {
            state_text = "⏸️ " + state_text;  // Add pause emoji
        }
        
		json presence = {
			{"cmd", "SET_ACTIVITY"},
			{"args", {
				{"pid", static_cast<int>(
#ifdef _WIN32
								  GetCurrentProcessId()
#else
								  getpid()
#endif
									  )},
				{"activity", {
					{"type",  3},
					{"timestamps", timestamps},
					{"details", playbackInfo.title},
					{"state", state_text},
					{"assets", {
						{"large_image", "plex_logo"},
						{"large_text", playbackInfo.title}
					}}
				}}
			}},
			{"nonce", std::to_string(time(nullptr))}
		};

		std::string presence_str = presence.dump();

		// Send presence update
		if (!ipc.writeFrame(OP_FRAME, presence_str))
		{
			LOG_ERROR("Discord", "Failed to send presence update");
			needs_reconnect = true;
		}
		else
		{
			// Cache the last successful activity payload for quick restoration after reconnect
			last_activity_payload = presence_str;

			// Read response to verify it worked
			int opcode;
			std::string response;
			if (ipc.readFrame(opcode, response))
			{
				// Validate response - check for error
				try
				{
					json response_json = json::parse(response);
					if (response_json.contains("evt") && response_json["evt"] == "ERROR")
					{
						LOG_ERROR_STREAM("Discord", "Discord rejected presence update: " << response);
						// If we hit rate limit, don't update timestamp
						if (response_json.contains("data") &&
							response_json["data"].contains("code") &&
							response_json["data"]["code"] == 4000)
						{
							LOG_WARNING("Discord", "Rate limit hit, backing off");
							return;
						}
					}
					// Check if assets were invalid
					else if (response_json.contains("data") &&
							 response_json["data"].contains("activity") &&
							 response_json["data"]["activity"].contains("assets"))
					{
						auto assets = response_json["data"]["activity"]["assets"];
						if (assets.is_null() || !assets.contains("large_image"))
						{
							LOG_WARNING("Discord", "Asset 'plex_logo' not found in Discord Developer Portal");
						}
					}

					// Update the last update timestamp only if successful
					last_successful_update = now_seconds;
					LOG_DEBUG_STREAM("Discord", "Updated presence: " << playbackInfo.title
																 << " - " << playbackInfo.username
                                                                 << (playbackInfo.state == PlaybackState::Paused ? " (Paused)" : ""));
					
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
	}
	else
	{
		// Clear presence if not playing anymore
		if (is_playing)
		{
			LOG_INFO("Discord", "Media stopped, clearing presence");
			clearPresence();
			last_successful_update = now_seconds;
		}
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
	last_activity_payload.clear();

	// Create empty presence payload to clear current presence
	json presence = {
		{"cmd", "SET_ACTIVITY"},
		{"args", {{"pid", static_cast<int>(
#ifdef _WIN32
							  GetCurrentProcessId()
#else
							  getpid()
#endif
								  )},
				  {"activity", nullptr}}},
		{"nonce", std::to_string(time(nullptr))}};

	std::string presence_str = presence.dump();

	if (!ipc.writeFrame(OP_FRAME, presence_str))
	{
		LOG_ERROR("Discord", "Failed to clear presence");
		needs_reconnect = true;
	}
	else
	{
		// Read and log response
		int opcode;
		std::string response;
		if (ipc.readFrame(opcode, response))
		{
			LOG_DEBUG("Discord", "Presence cleared successfully");
		}
		else
		{
			LOG_ERROR("Discord", "Failed to read clear presence response");
		}
	}
}

void Discord::keepAlive()
{
	if (!ipc.sendPing())
	{
		LOG_ERROR("Discord", "Failed to send ping");
		needs_reconnect = true;
		return;
	}

	// Read and process PONG response
	int opcode;
	std::string response;
	if (ipc.readFrame(opcode, response))
	{
		if (opcode != OP_PONG)
		{
			LOG_WARNING_STREAM("Discord", "Unexpected response to PING. Opcode: " << opcode);
		}
	}
	else
	{
		LOG_ERROR("Discord", "Failed to read PONG response");
		needs_reconnect = true;
	}
}

// Lifecycle control
void Discord::start()
{
	LOG_INFO("Discord", "Starting Discord Rich Presence");
	init();
}

void Discord::stop()
{
	LOG_INFO("Discord", "Stopping Discord Rich Presence");
	running = false;
	if (conn_thread.joinable())
	{
		conn_thread.join();
	}
	ipc.disconnect();
}

bool Discord::isConnected() const
{
	return connected && ipc.isConnected();
}

bool Discord::isWaitingForDiscord() const
{
	return waiting_for_discord;
}
