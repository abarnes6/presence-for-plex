#include "discord.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>  // For timestamps in debug logs
#include <iomanip> // For formatting timestamps

// Debug logging macro to include timestamps
#define DEBUG_LOG(msg)                                                                      \
	{                                                                                       \
		auto now = std::chrono::system_clock::now();                                        \
		auto now_c = std::chrono::system_clock::to_time_t(now);                             \
		std::tm now_tm;                                                                     \
		localtime_s(&now_tm, &now_c);                                                       \
		std::cout << "[" << std::put_time(&now_tm, "%H:%M:%S") << "] " << msg << std::endl; \
	}

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> // For newer socket functions
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#if defined(_WIN32) && !defined(htole32)
#define htole32(x) (x) // little‑endian host
#define le32toh(x) (x)
#endif

using json = nlohmann::json;

// Discord IPC opcodes
enum DiscordOpcodes
{
	OP_HANDSHAKE = 0,
	OP_FRAME = 1,
	OP_CLOSE = 2,
	OP_PING = 3,
	OP_PONG = 4
};

Discord::Discord() : running(false),
					 connected(false),
					 needs_reconnect(false),
					 is_playing(false),
					 start_timestamp(0),
					 reconnect_attempts(0),
					 last_successful_update(0)
{
#ifdef _WIN32
	pipe_handle = INVALID_HANDLE_VALUE;
#else
	pipe_fd = -1;
#endif
}

Discord::~Discord()
{
	stop();
}

bool Discord::init()
{
	DEBUG_LOG("Initializing Discord Rich Presence...");
	running = true;
	conn_thread = std::thread(&Discord::connectionThread, this);
	client_id = Config::getInstance().clientId;
	DEBUG_LOG("Initializing Discord with client ID: " << client_id);
	return true;
}

void Discord::connectionThread()
{
	DEBUG_LOG("Discord connection thread started");
	while (running)
	{
		if (!connected)
		{
			DEBUG_LOG("Not connected to Discord, attempting connection...");
			if (connectToDiscord())
			{
				DEBUG_LOG("Connection established, sending handshake...");
				// After connecting, send handshake
				// Create the correct format that Discord requires
				json payload = {
					{"client_id", std::to_string(client_id)},
					{"v", 1}};

				std::string handshake_str = payload.dump();
				DEBUG_LOG("Sending handshake payload: " << handshake_str);

				if (!writeFrame(OP_HANDSHAKE, handshake_str))
				{
					DEBUG_LOG("ERROR: Handshake write failed");
					disconnectFromDiscord();
					calculateBackoffTime();
					continue;
				}

				// Read handshake response - Discord should reply
				int opcode;
				std::string response;
				DEBUG_LOG("Waiting for handshake response...");
				if (!readFrame(opcode, response) || opcode != OP_FRAME)
				{
					DEBUG_LOG("ERROR: Failed to read handshake response. Opcode: " << opcode);
					if (!response.empty())
					{
						DEBUG_LOG("Response content: " << response);
					}
					disconnectFromDiscord();
					calculateBackoffTime();
					continue;
				}

				DEBUG_LOG("Handshake response received: " << response);

				// Verify READY was received
				json ready;
				try
				{
					ready = json::parse(response);
					if (ready["evt"] != "READY")
					{
						DEBUG_LOG("ERROR: Discord did not respond with READY event. Got: " << ready["evt"].get<std::string>());
						disconnectFromDiscord();
						calculateBackoffTime();
						continue;
					}
					DEBUG_LOG("Handshake READY event confirmed");
				}
				catch (const std::exception &e)
				{
					DEBUG_LOG("ERROR: Failed to parse READY response: " << e.what());
					disconnectFromDiscord();
					calculateBackoffTime();
					continue;
				}

				connected = true;
				reconnect_attempts = 0; // Reset attempts after successful connection
				DEBUG_LOG("Successfully connected to Discord IPC");

				// If we have a last activity cached, restore it immediately
				if (!last_activity_payload.empty())
				{
					DEBUG_LOG("Restoring previous activity state...");
					DEBUG_LOG("Cached payload: " << last_activity_payload);
					if (!writeFrame(OP_FRAME, last_activity_payload))
					{
						DEBUG_LOG("ERROR: Failed to restore activity state");
						needs_reconnect = true;
					}
					else
					{
						// Read response to verify
						DEBUG_LOG("Reading response after restoring activity...");
						if (readFrame(opcode, response))
						{
							DEBUG_LOG("Restore activity response: " << response);
						}
						else
						{
							DEBUG_LOG("ERROR: Failed to read response after restoring activity");
						}
					}
				}
			}
			else
			{
				// Wait before retrying with exponential backoff
				DEBUG_LOG("Failed to connect to Discord IPC");
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
				DEBUG_LOG("Sending keep-alive ping to Discord");
				keepAlive();
				last_ping = std::chrono::steady_clock::now();
			}

			if (needs_reconnect)
			{
				DEBUG_LOG("Reconnect flag set, reconnecting to Discord...");
				disconnectFromDiscord();
				connected = false;
				needs_reconnect = false;
				continue;
			}

			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}
	DEBUG_LOG("Connection thread exiting");
}

void Discord::calculateBackoffTime()
{
	// Exponential backoff: 2^n seconds with a max of 60 seconds
	int backoff_secs = std::min(1 << std::min(reconnect_attempts, 5), 60);
	reconnect_attempts++;
	DEBUG_LOG("Waiting " << backoff_secs << " seconds before reconnecting (attempt " << reconnect_attempts << ")");
	std::this_thread::sleep_for(std::chrono::seconds(backoff_secs));
}

bool Discord::connectToDiscord()
{
#ifdef _WIN32
	// Windows implementation using named pipes
	DEBUG_LOG("Attempting to connect to Discord via Windows named pipes");
	for (int i = 0; i < 10; i++)
	{
		std::string pipeName = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
		DEBUG_LOG("Trying pipe: " << pipeName);

		pipe_handle = CreateFile(
			pipeName.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			0,
			NULL);

		if (pipe_handle != INVALID_HANDLE_VALUE)
		{
			// Try setting pipe to message mode, but don't fail if this doesn't work
			// Some Discord versions may not support message mode
			DWORD mode = PIPE_READMODE_MESSAGE;
			if (!SetNamedPipeHandleState(pipe_handle, &mode, NULL, NULL))
			{
				DWORD error = GetLastError();
				DEBUG_LOG("Warning: Failed to set pipe mode. Using default mode. Error: " << error);
				// Continue anyway - don't disconnect
			}

			DEBUG_LOG("Successfully connected to Discord pipe: " << pipeName);
			return true;
		}

		// Log the specific error for debugging
		DWORD error = GetLastError();
		DEBUG_LOG("Failed to connect to " << pipeName << ": error code " << error);
	}
	DEBUG_LOG("ERROR: Could not connect to any Discord pipe. Is Discord running?");
	return false;
#else
	// Unix implementation using sockets
	for (int i = 0; i < 10; i++)
	{
		std::string socket_path;
		const char *temp = getenv("XDG_RUNTIME_DIR");
		const char *home = getenv("HOME");

		if (temp)
		{
			socket_path = std::string(temp) + "/discord-ipc-" + std::to_string(i);
		}
		else if (home)
		{
			socket_path = std::string(home) + "/.discord-ipc-" + std::to_string(i);
		}
		else
		{
			continue;
		}

		pipe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (pipe_fd == -1)
		{
			continue;
		}

		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

		if (connect(pipe_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
		{
			return true;
		}

		close(pipe_fd);
		pipe_fd = -1;
	}
	return false;
#endif
}

void Discord::disconnectFromDiscord()
{
	DEBUG_LOG("Disconnecting from Discord...");
#ifdef _WIN32
	if (pipe_handle != INVALID_HANDLE_VALUE)
	{
		DEBUG_LOG("Closing pipe handle");
		CloseHandle(pipe_handle);
		pipe_handle = INVALID_HANDLE_VALUE;
	}
#else
	if (pipe_fd != -1)
	{
		close(pipe_fd);
		pipe_fd = -1;
	}
#endif
	connected = false;
	DEBUG_LOG("Disconnected from Discord");
}

bool Discord::writeFrame(int opcode, const std::string &json)
{
	DEBUG_LOG("Writing frame - Opcode: " << opcode << ", Data length: " << json.size());
	DEBUG_LOG("Frame data: " << json);

	uint32_t len = static_cast<uint32_t>(json.size());
	std::vector<char> buf(8 + len); // Dynamic buffer sized exactly for our payload
	auto *p = reinterpret_cast<uint32_t *>(buf.data());
	p[0] = htole32(opcode); // endian-safe
	p[1] = htole32(len);
	memcpy(buf.data() + 8, json.data(), len);

#ifdef _WIN32
	DWORD written;
	if (!WriteFile(pipe_handle, buf.data(), 8 + len, &written, nullptr) ||
		written != 8 + len)
	{
		DWORD error = GetLastError();
		DEBUG_LOG("ERROR: Failed to write frame to pipe. Error code: " << error << ", Bytes written: " << written);
		needs_reconnect = true;
		return false;
	}
	DEBUG_LOG("Successfully wrote " << written << " bytes to pipe");
	FlushFileBuffers(pipe_handle);
#else
	ssize_t n = ::write(pipe_fd, buf.data(), 8 + len);
	if (n != 8 + len)
	{
		needs_reconnect = true;
		return false;
	}
#endif
	return true;
}

bool Discord::readFrame(int &opcode, std::string &data)
{
	DEBUG_LOG("Attempting to read frame from Discord");
	opcode = -1;
	// Read header (8 bytes)
	char header[8];
	int header_bytes_read = 0;

#ifdef _WIN32
	DEBUG_LOG("Reading header (8 bytes)...");
	while (header_bytes_read < 8)
	{
		DWORD bytes_read;
		if (!ReadFile(pipe_handle, header + header_bytes_read, 8 - header_bytes_read, &bytes_read, NULL))
		{
			DWORD error = GetLastError();
			DEBUG_LOG("ERROR: Failed to read header: error code " << error);
			needs_reconnect = true;
			return false;
		}

		if (bytes_read == 0)
		{
			DEBUG_LOG("ERROR: Read zero bytes from pipe - connection closed");
			needs_reconnect = true;
			return false;
		}

		header_bytes_read += bytes_read;
		DEBUG_LOG("Read " << bytes_read << " header bytes, total: " << header_bytes_read << "/8");
	}
#else
	while (header_bytes_read < 8)
	{
		ssize_t bytes_read = read(pipe_fd, header + header_bytes_read, 8 - header_bytes_read);
		if (bytes_read <= 0)
		{
			if (bytes_read < 0)
			{
				std::cerr << "Error reading from socket: " << strerror(errno) << std::endl;
			}
			else
			{
				std::cerr << "Socket closed during header read" << std::endl;
			}
			needs_reconnect = true;
			return false;
		}
		header_bytes_read += bytes_read;
	}
#endif

	// Extract opcode and length (with proper endianness handling)
	uint32_t raw0, raw1;
	memcpy(&raw0, header, 4);
	memcpy(&raw1, header + 4, 4);
	opcode = le32toh(raw0);
	uint32_t length = le32toh(raw1);

	DEBUG_LOG("Frame header parsed - Opcode: " << opcode << ", Expected data length: " << length);

	if (length == 0)
	{
		DEBUG_LOG("Frame has zero length, no data to read");
		data.clear();
		return true;
	}

	// Read data
	data.resize(length);
	uint32_t data_bytes_read = 0;

#ifdef _WIN32
	DEBUG_LOG("Reading payload (" << length << " bytes)...");
	while (data_bytes_read < length)
	{
		DWORD bytes_read;
		if (!ReadFile(pipe_handle, &data[data_bytes_read], length - data_bytes_read, &bytes_read, NULL))
		{
			DWORD error = GetLastError();
			DEBUG_LOG("ERROR: Failed to read data: error code " << error);
			needs_reconnect = true;
			return false;
		}

		if (bytes_read == 0)
		{
			DEBUG_LOG("ERROR: Read zero bytes from pipe during payload read - connection closed");
			needs_reconnect = true;
			return false;
		}

		data_bytes_read += bytes_read;
		DEBUG_LOG("Read " << bytes_read << " data bytes, total: " << data_bytes_read << "/" << length);
	}
#else
	while (data_bytes_read < length)
	{
		ssize_t bytes_read = read(pipe_fd, &data[data_bytes_read], length - data_bytes_read);
		if (bytes_read <= 0)
		{
			if (bytes_read < 0)
			{
				std::cerr << "Error reading from socket: " << strerror(errno) << std::endl;
			}
			else
			{
				std::cerr << "Socket closed during payload read" << std::endl;
			}
			needs_reconnect = true;
			return false;
		}
		data_bytes_read += bytes_read;
	}
#endif

	DEBUG_LOG("Successfully read complete frame. Data: " << data);
	return true;
}

void Discord::updatePresence(const PlaybackInfo &playbackInfo)
{
	DEBUG_LOG("updatePresence called with state: " << ", title: " << playbackInfo.title
												   << ", mediaType: " << playbackInfo.mediaType);

	if (!connected)
	{
		DEBUG_LOG("ERROR: Can't update presence: not connected to Discord");
		return;
	}

	std::lock_guard<std::mutex> lock(mutex);
	DEBUG_LOG("Acquired mutex for presence update");

	// Rate limiting - don't allow more than 5 updates per 20 seconds
	auto now_time = std::chrono::steady_clock::now();
	auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(now_time.time_since_epoch()).count();
	int64_t seconds_since_last_update = now_seconds - last_successful_update;

	DEBUG_LOG("Seconds since last update: " << seconds_since_last_update);
	if (seconds_since_last_update < 4) // 20s ÷ 5 = 4s minimum between updates
	{
		DEBUG_LOG("Rate limiting: skipping presence update (too soon)");
		return;
	}

	// Check if we're already showing the same content
	std::string new_details;
	std::string new_state;
	int64_t new_timestamp = 0;

	if (playbackInfo.state == PlaybackState::Playing || playbackInfo.state == PlaybackState::Paused)
	{
		is_playing = true;
		DEBUG_LOG("Media is playing or paused, updating presence");

		// Format details (title)
		new_details = playbackInfo.title;
		if (!playbackInfo.subtitle.empty())
		{
			new_details += " - " + playbackInfo.subtitle;
		}

		// Truncate details if too long (Discord limit is 128 chars)
		if (new_details.length() > 128)
		{
			DEBUG_LOG("Details too long (" << new_details.length() << " chars), truncating");
			new_details = new_details.substr(0, 125) + "...";
		}
		DEBUG_LOG("Formatted details: " << new_details);

		// Format state (media type & status)
		new_state = playbackInfo.mediaType;
		if (playbackInfo.state == PlaybackState::Paused)
		{
			new_state += " (Paused)";
		}

		// Truncate state if too long (Discord limit is 128 chars)
		if (new_state.length() > 128)
		{
			DEBUG_LOG("State too long (" << new_state.length() << " chars), truncating");
			new_state = new_state.substr(0, 125) + "...";
		}
		DEBUG_LOG("Formatted state: " << new_state);

		// Calculate timestamps
		auto now = std::chrono::system_clock::now();
		int64_t current_time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

		if (playbackInfo.state == PlaybackState::Playing)
		{
			// For playing state, calculate end timestamp (when content will finish)
			if (playbackInfo.duration > 0 && playbackInfo.progress >= 0)
			{
				// Calculate remaining time (in seconds)
				int64_t remaining = static_cast<int64_t>(playbackInfo.duration - playbackInfo.progress);
				new_timestamp = current_time + remaining;
				DEBUG_LOG("Calculated end timestamp: " << new_timestamp
													   << " (current time: " << current_time
													   << ", duration: " << playbackInfo.duration
													   << ", progress: " << playbackInfo.progress
													   << ", remaining: " << remaining << ")");
			}
			else
			{
				DEBUG_LOG("Cannot calculate timestamp - duration: " << playbackInfo.duration
																	<< ", progress: " << playbackInfo.progress);
			}
		}

		// If nothing changed, don't update
		DEBUG_LOG("Comparing current state: '" << current_state << "' to new state: '" << new_state << "'");
		DEBUG_LOG("Comparing current details: '" << current_details << "' to new details: '" << new_details << "'");
		DEBUG_LOG("Current timestamp: " << start_timestamp << ", new timestamp: " << new_timestamp);

		if (current_details == new_details && current_state == new_state &&
			((playbackInfo.state == PlaybackState::Paused && start_timestamp == 0) ||
			 (playbackInfo.state != PlaybackState::Paused && start_timestamp == new_timestamp)))
		{
			DEBUG_LOG("Skipping presence update - no changes detected");
			return;
		}

		DEBUG_LOG("Changes detected, updating presence");
		current_details = new_details;
		current_state = new_state;
		start_timestamp = (playbackInfo.state == PlaybackState::Paused) ? 0 : new_timestamp;

		// Create rich presence payload
		json presence = {
			{"cmd", "SET_ACTIVITY"},
			{"args", {{"pid", static_cast<int>(
#ifdef _WIN32
								  GetCurrentProcessId()
#else
								  getpid()
#endif
									  )},
					  {"activity", {{"details", current_details}, {"state", current_state}, {"assets", {{"large_image", "plex_logo"}, {"large_text", "Watching on Plex"}}}}}}},
			{"nonce", std::to_string(time(nullptr))}};

		// Add timestamps if playing (not paused)
		if (playbackInfo.state == PlaybackState::Playing && start_timestamp > 0)
		{
			// Use "end" timestamp to show countdown timer
			presence["args"]["activity"]["timestamps"] = {{"end", start_timestamp}};
			DEBUG_LOG("Added end timestamp: " << start_timestamp);
		}

		std::string presence_str = presence.dump();
		DEBUG_LOG("Sending presence update payload: " << presence_str);

		// Send presence update
		if (!writeFrame(OP_FRAME, presence_str))
		{
			DEBUG_LOG("ERROR: Failed to send presence update");
			needs_reconnect = true;
		}
		else
		{
			// Cache the last successful activity payload for quick restoration after reconnect
			last_activity_payload = presence_str;
			DEBUG_LOG("Cached activity payload for future reconnects");

			// Read response to verify it worked
			int opcode;
			std::string response;
			DEBUG_LOG("Reading response to presence update...");
			if (readFrame(opcode, response))
			{
				DEBUG_LOG("Discord response received: opcode=" << opcode << ", data=" << response);

				// Validate response - check for error
				try
				{
					json response_json = json::parse(response);
					if (response_json.contains("evt") && response_json["evt"] == "ERROR")
					{
						DEBUG_LOG("ERROR: Discord rejected presence update: " << response);
						// If we hit rate limit, don't update timestamp
						if (response_json.contains("data") &&
							response_json["data"].contains("code") &&
							response_json["data"]["code"] == 4000)
						{
							DEBUG_LOG("Rate limit hit, backing off");
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
							DEBUG_LOG("WARNING: large_image asset 'plex_logo' was not found. "
									  "Make sure it's uploaded in Discord Developer Portal.");
						}
						else
						{
							DEBUG_LOG("Asset verification passed");
						}
					}

					// Update the last update timestamp only if successful
					last_successful_update = now_seconds;
					DEBUG_LOG("Presence update was successful, updated last_successful_update to " << last_successful_update);
				}
				catch (const std::exception &e)
				{
					DEBUG_LOG("ERROR: Failed to parse response JSON: " << e.what());
				}
			}
			else
			{
				DEBUG_LOG("ERROR: Failed to read Discord response");
			}
		}
	}
	else
	{
		// Clear presence if not playing anymore
		if (is_playing)
		{
			DEBUG_LOG("Media stopped, clearing presence");
			clearPresence();
			last_successful_update = now_seconds;
		}
		else
		{
			DEBUG_LOG("Media not playing and presence already cleared, no action needed");
		}
	}
}

void Discord::clearPresence()
{
	DEBUG_LOG("clearPresence called");
	if (!connected)
	{
		DEBUG_LOG("ERROR: Can't clear presence: not connected to Discord");
		return;
	}

	std::lock_guard<std::mutex> lock(mutex);
	DEBUG_LOG("Acquired mutex for clearing presence");

	// Reset state tracking variables
	current_details.clear();
	current_state.clear();
	start_timestamp = 0;
	is_playing = false;
	last_activity_payload.clear();
	DEBUG_LOG("Reset all state tracking variables");

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

	DEBUG_LOG("Sending clear presence payload");
	std::string presence_str = presence.dump();
	DEBUG_LOG("Clear presence payload: " << presence_str);

	if (!writeFrame(OP_FRAME, presence_str))
	{
		DEBUG_LOG("ERROR: Failed to clear presence");
		needs_reconnect = true;
	}
	else
	{
		// Read and log response
		int opcode;
		std::string response;
		DEBUG_LOG("Reading response to clear presence request...");
		if (readFrame(opcode, response))
		{
			DEBUG_LOG("Clear presence response: opcode=" << opcode << ", data=" << response);
		}
		else
		{
			DEBUG_LOG("ERROR: Failed to read clear presence response");
		}
	}
}

void Discord::keepAlive()
{
	DEBUG_LOG("Sending keepAlive ping to Discord");
	static const json ping = json::object(); // empty payload
	std::string ping_str = ping.dump();

	if (!writeFrame(OP_PING, ping_str))
	{
		DEBUG_LOG("ERROR: Failed to send ping");
		needs_reconnect = true;
		return;
	}

	// Read and process PONG response
	int opcode;
	std::string response;
	DEBUG_LOG("Waiting for PONG response...");
	if (readFrame(opcode, response))
	{
		if (opcode == OP_PONG)
		{
			DEBUG_LOG("Received PONG from Discord");
		}
		else
		{
			DEBUG_LOG("ERROR: Unexpected response to PING. Opcode: " << opcode << ", Response: " << response);
		}
	}
	else
	{
		DEBUG_LOG("ERROR: Failed to read PONG response");
		needs_reconnect = true;
	}
}

// Lifecycle control
void Discord::start()
{
	DEBUG_LOG("Discord::start() called");
	init();
}

void Discord::stop()
{
	DEBUG_LOG("Discord::stop() called, shutting down...");
	running = false;
	if (conn_thread.joinable())
	{
		DEBUG_LOG("Waiting for connection thread to exit...");
		conn_thread.join();
		DEBUG_LOG("Connection thread has exited");
	}
	disconnectFromDiscord();
	DEBUG_LOG("Discord module stopped");
}

bool Discord::isConnected() const
{
	return connected;
}
