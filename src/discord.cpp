#include "discord.h"

constexpr int MAX_PAUSED_DURATION = 9999;

// Rate limit constants chosen based on how Music Presence does it - kind of...
// I can't find anything online about Discord's rate limits, but there is definitely something
constexpr int MAX_FRAMES_PER_WINDOW = 5;      
constexpr int RATE_LIMIT_WINDOW_SECONDS = 15; 
constexpr int MIN_FRAME_INTERVAL_SECONDS = 1; 
constexpr int MAX_FRAMES_SHORT_WINDOW = 3;    
constexpr int RATE_LIMIT_SHORT_WINDOW = 5;

using json = nlohmann::json;

Discord::Discord() : running(false),
					 needs_reconnect(false),
					 reconnect_attempts(0),
					 is_playing(false),
					 nonce_counter(0),
					 onConnected(nullptr),
					 onDisconnected(nullptr),
					 has_queued_frame(false),
					 last_frame_write_time(0)
{
}

Discord::~Discord()
{
	if (running)
		stop();
	LOG_INFO("Discord", "Discord object destroyed");
}

void Discord::connectionThread()
{
	LOG_INFO("Discord", "Connection thread started");
	while (running)
	{
		// Handle connection logic
		if (!ipc.isConnected())
		{
			LOG_DEBUG("Discord", "Not connected, attempting connection");

			// Calculate exponential backoff delay
			if (reconnect_attempts > 0)
			{
				int delay = std::min(5 * reconnect_attempts, 60);
				LOG_INFO("Discord", "Reconnection attempt " + std::to_string(reconnect_attempts) +
										", waiting " + std::to_string(delay) + " seconds");

				for (int i = 0; i < delay*2 && running; ++i)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					if (i % 10 == 0) {
						processQueuedFrame(); // Process frames more frequently during wait
					}
				}
				if (!running)
				{
					break;
				}
			}

			reconnect_attempts++;

			// Attempt to connect
			if (!attemptConnection())
			{
				LOG_INFO("Discord", "Failed to connect to Discord IPC, will retry later");
				continue;
			}

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
			LOG_DEBUG("Discord", "Checking Discord connection health");

			if (!isStillAlive())
			{
				LOG_INFO("Discord", "Connection to Discord lost, will reconnect");
				if (ipc.isConnected())
				{
					ipc.closePipe();
				}
				needs_reconnect = true;

				// Call disconnected callback if set
				if (onDisconnected)
				{
					onDisconnected();
				}
				continue;
			}
			else {
				needs_reconnect = false;
			}

			// Connection health check sleep
			for (int i = 0; i < 600 && running && !needs_reconnect; ++i) // 600 * 100ms = 60s
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				if (i % 10 == 0) {
					processQueuedFrame();
				}
			}
		}
	}
}

bool Discord::attemptConnection()
{
	if (!ipc.openPipe())
	{
		return false;
	}

	LOG_DEBUG("Discord", "Connection established, sending handshake");
	LOG_DEBUG("Discord", "Using client ID: " + std::to_string(Config::getInstance().getDiscordClientId()));

	if (!ipc.sendHandshake(Config::getInstance().getDiscordClientId()))
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
		LOG_DEBUG_STREAM("Discord", "Response that caused the error: " << response);
	}
	catch (const json::type_error &e)
	{
		LOG_ERROR_STREAM("Discord", "JSON type error in READY response: " << e.what());
		LOG_DEBUG_STREAM("Discord", "Response that caused the error: " << response);
	}
	catch (const std::exception &e)
	{
		LOG_ERROR("Discord", "Failed to parse READY response: " + std::string(e.what()));
		LOG_DEBUG("Discord", "Response that caused the error: " + response);
	}

	ipc.closePipe();
	return false;
}

void Discord::updatePresence(const MediaInfo &info)
{
	LOG_DEBUG_STREAM("Discord", "updatePresence called for title: " << info.title);

	if (!ipc.isConnected())
	{
		LOG_WARNING("Discord", "Can't update presence: not connected to Discord");
		return;
	}

	std::lock_guard<std::mutex> lock(mutex);

	if (info.state == PlaybackState::Playing ||
		info.state == PlaybackState::Paused ||
		info.state == PlaybackState::Buffering)
	{
		std::string stateStr = (info.state == PlaybackState::Playing) ? "playing" : (info.state == PlaybackState::Paused) ? "paused"
																														  : "buffering";
		LOG_DEBUG_STREAM("Discord", "Media is " << stateStr << ", updating presence");

		is_playing = true;

		std::string nonce = generateNonce();
		std::string presence = createPresence(info, nonce);
		
		LOG_INFO_STREAM("Discord", "Queuing presence update: " << info.title << " - " << info.username
			<< (info.state == PlaybackState::Paused ? " (Paused)" : "")
			<< (info.state == PlaybackState::Buffering ? " (Buffering)" : ""));

		// Queue the presence update
		queuePresenceMessage(presence);

		// Attempt to send it immediately
		processQueuedFrame();
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

bool Discord::isConnected() const
{
	return ipc.isConnected();
}

std::string Discord::generateNonce()
{
	return std::to_string(++nonce_counter);
}

std::string Discord::createPresence(const MediaInfo &info, const std::string &nonce)
{
	json activity = createActivity(info);

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

std::string Discord::createPresenceMetadata(const MediaInfo &info, const std::string &nonce)
{
	json activity = createActivity(info);

	json presence = {
		{"cmd", "SET_ACTIVITY"},
		{"data", activity},
		{"evt", nullptr},
		{"nonce", nonce}};

	return presence.dump();
}

json Discord::createActivity(const MediaInfo &info)
{
	std::string state;
	std::string details;
	json assets = {{"large_text", info.title}};

	if (info.artPath != "")
	{
		assets["large_image"] = info.artPath;
	}
	else
	{
		assets["large_image"] = "plex_logo";
	}

	if (info.type == MediaType::TVShow)
	{
		std::stringstream ss;
		ss << "S" << info.season;
		ss << " • ";
		ss << "E" << info.episode;

		details = info.grandparentTitle;
		state = ss.str() + " - " + info.title;
	}
	else
	{
		details = info.title + " (" + std::to_string(info.year) + ")";
		if (!info.genres.empty())
		{
			state = std::accumulate(std::next(info.genres.begin()), info.genres.end(),
									info.genres[0],
									[](std::string a, const std::string &b)
									{
										return a + ", " + b;
									});
		}
		else
		{
			state = info.grandparentTitle;
		}
	}

	if (info.state == PlaybackState::Buffering)
	{
		state = "🔄 Buffering...";
	}
	else if (info.state == PlaybackState::Paused)
	{
		assets["small_image"] = "paused";
		assets["small_text"] = "Paused";
	}
	else if (info.state == PlaybackState::Stopped)
	{
		state = "Stopped";
	}

	if (state.empty()) {
		state = "Watching";
	}

	// Calculate timestamps for progress bar
	auto now = std::chrono::system_clock::now();
	int64_t current_time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
	int64_t start_timestamp = 0;
	int64_t end_timestamp = 0;

	if (info.state == PlaybackState::Playing)
	{
		start_timestamp = current_time - static_cast<int64_t>(info.progress);
		end_timestamp = current_time + static_cast<int64_t>(info.duration - info.progress);
	}
	else if (info.state == PlaybackState::Paused || info.state == PlaybackState::Buffering)
	{
		auto max_duration = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::hours(MAX_PAUSED_DURATION))
								.count();
		start_timestamp = current_time + max_duration;
		end_timestamp = start_timestamp + static_cast<int64_t>(info.duration);
	}

	json timestamps = {
		{"start", start_timestamp},
		{"end", end_timestamp}};

	json buttons = {};

	// Add relevant buttons based on available IDs
	if (!info.malId.empty())
	{
		buttons.push_back({{"label", "View on MyAnimeList"},
						   {"url", "https://myanimelist.net/anime/" + info.malId}});
	}
	else if (!info.imdbId.empty())
	{
		buttons.push_back({{"label", "View on IMDb"},
						   {"url", "https://www.imdb.com/title/" + info.imdbId}});
	}

	json ret = {
		{"type", 3}, // 3 = "Watching"
		{"state", state},
		{"details", details},
		{"timestamps", timestamps},
		{"assets", assets},
		{"instance", true}};

	if (!buttons.empty())
	{
		ret["buttons"] = buttons;
	}

	return ret;
}

void Discord::sendPresenceMessage(const std::string &message)
{
	if (!ipc.writeFrame(OP_FRAME, message))
	{
		LOG_WARNING("Discord", "Failed to send presence update");
		needs_reconnect = true;
		// Call disconnected callback if set
		if (onDisconnected)
		{
			onDisconnected();
		}
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
				LOG_WARNING_STREAM("Discord", "Discord rejected presence update: " << response);
			}
		}
		catch (const std::exception &e)
		{
			LOG_WARNING_STREAM("Discord", "Failed to parse response: " << e.what());
		}
	}
	else
	{
		LOG_WARNING("Discord", "Failed to read Discord response");
	}
}

void Discord::queuePresenceMessage(const std::string &message)
{
	std::lock_guard<std::mutex> lock(frame_queue_mutex);
	queued_frame = message;
	has_queued_frame = true;
	LOG_DEBUG("Discord", "Frame queued for sending");
}

void Discord::processQueuedFrame()
{
	std::string frame_to_send;
	
	{
		std::lock_guard<std::mutex> lock(frame_queue_mutex);
		if (!has_queued_frame) {
			return;
		}
		
		auto now = std::chrono::steady_clock::now();
		auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
		
		// Check rate limits
		if (!canSendFrame(now_seconds)) {
			return;
		}
		
		frame_to_send = queued_frame;
		has_queued_frame = false;
		
		// Record this frame write time
		frame_write_times.push_back(now_seconds);
		last_frame_write_time = now_seconds;
	}
	
	LOG_DEBUG("Discord", "Processing queued frame");
	sendPresenceMessage(frame_to_send);
}

bool Discord::canSendFrame(int64_t current_time)
{
	// Enforce minimum interval between frames
	if (current_time - last_frame_write_time < MIN_FRAME_INTERVAL_SECONDS) {
		LOG_DEBUG("Discord", "Rate limit: Too soon since last frame");
		return false;
	}
	
	while (!frame_write_times.empty() && 
		   frame_write_times.front() < current_time - RATE_LIMIT_WINDOW_SECONDS) {
		frame_write_times.pop_front();
	}
	
	// Check if we've reached the maximum frames per long window (15 seconds)
	if (frame_write_times.size() >= MAX_FRAMES_PER_WINDOW - 1) {
		LOG_DEBUG("Discord", "Rate limit: Maximum frames per 15-second window reached");
		return false;
	}
	
	int frames_in_short_window = 0;
	for (const auto& timestamp : frame_write_times) {
		if (timestamp >= current_time - RATE_LIMIT_SHORT_WINDOW) {
			frames_in_short_window++;
		}
	}
	
	if (frames_in_short_window >= MAX_FRAMES_SHORT_WINDOW - 1) {
		LOG_DEBUG("Discord", "Rate limit: Maximum frames per 5-second window reached");
		return false;
	}
	
	return true;
}

void Discord::clearPresence()
{
	LOG_DEBUG("Discord", "clearPresence called");
	if (!ipc.isConnected())
	{
		LOG_WARNING("Discord", "Can't clear presence: not connected to Discord");
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

	// Queue the clear presence message instead of sending immediately
	queuePresenceMessage(presence_str);
}

bool Discord::isStillAlive()
{

	// Get current time
	auto now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	
	// Skip ping if there was a recent write
	if (now - last_frame_write_time < 60) {
		LOG_DEBUG("Discord", "Skipping ping due to recent write activity");
		return true;
	}

	if (!ipc.sendPing())
	{
		LOG_WARNING("Discord", "Failed to send ping");
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
		LOG_WARNING("Discord", "Failed to read PONG response");
		return false;
	}

	return true;
}

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

    if (conn_thread.joinable())
    {
        ThreadUtils::joinWithTimeout(conn_thread, std::chrono::seconds(3), "Discord connection thread");
    }
    
    if (ipc.isConnected())
    {
        ipc.closePipe();
    }
}

void Discord::setConnectedCallback(ConnectionCallback callback)
{
	onConnected = callback;
}

void Discord::setDisconnectedCallback(ConnectionCallback callback)
{
	onDisconnected = callback;
}
