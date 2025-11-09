#include "presence_for_plex/services/network/sse_client.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/services/network/request_builder.hpp"
#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <sstream>
#include <chrono>
#include <thread>
#include <future>

namespace presence_for_plex {
namespace services {

// SSEClient implementation
SSEClient::SSEClient(std::shared_ptr<HttpClient> http_client)
    : m_http_client(std::move(http_client))
    , m_last_event_time(std::chrono::system_clock::now()) {

    LOG_DEBUG("SSEClient", "Creating SSE client");
}

SSEClient::~SSEClient() {
    disconnect();
    LOG_DEBUG("SSEClient", "SSE client destroyed");
}

std::expected<void, core::PlexError> SSEClient::connect(
    const std::string& url,
    const HttpHeaders& headers,
    SSEBasicEventCallback callback) {

    if (m_running) {
        LOG_WARNING("SSEClient", "SSE client already running");
        return {};
    }

    m_url = url;
    m_headers = headers;
    m_callback = std::move(callback);

    LOG_INFO("SSEClient", "Connecting to SSE endpoint: " + url);

    m_running = true;
    m_event_thread = std::jthread(&SSEClient::event_loop, this);

    return {};
}

void SSEClient::disconnect() {
    if (!m_running) {
        return;
    }

    LOG_INFO("SSEClient", "Disconnecting SSE client");

    m_running = false;
    m_connected = false;
}

bool SSEClient::is_connected() const {
    return m_connected;
}

std::string SSEClient::get_url() const {
    return m_url;
}

std::chrono::system_clock::time_point SSEClient::get_last_event_time() const {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_last_event_time;
}

void SSEClient::process_streaming_data(const std::string& data_chunk) {
    if (data_chunk.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_state_mutex);

    // Append the new chunk to our partial data buffer
    m_partial_data += data_chunk;

    // Process complete lines from the buffer
    size_t pos = 0;
    while ((pos = m_partial_data.find('\n', pos)) != std::string::npos) {
        std::string line = m_partial_data.substr(0, pos);

        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Process this line
        process_sse_line(line);

        // Remove processed line from buffer
        m_partial_data.erase(0, pos + 1);
        pos = 0;
    }

    // Update last event time
    m_last_event_time = std::chrono::system_clock::now();
}


void SSEClient::event_loop() {
    LOG_DEBUG("SSEClient", "Starting SSE event loop");

    while (m_running) {
        // Check if we should stop retrying initial connection
        if (!m_initial_connection_succeeded) {
            m_connection_attempts++;
            if (m_connection_attempts > MAX_INITIAL_CONNECTION_ATTEMPTS) {
                LOG_ERROR("SSEClient", "Max initial connection attempts (" +
                              std::to_string(MAX_INITIAL_CONNECTION_ATTEMPTS) +
                              ") reached for: " + m_url);
                m_running = false;
                break;
            }
            LOG_INFO("SSEClient", "Initial connection attempt " + std::to_string(m_connection_attempts) +
                         "/" + std::to_string(MAX_INITIAL_CONNECTION_ATTEMPTS) + " for: " + m_url);
        }

        try {
            // Add SSE-specific headers
            HttpHeaders sse_headers = m_headers;
            sse_headers["Accept"] = "text/event-stream";
            sse_headers["Cache-Control"] = "no-cache";

            LOG_DEBUG("SSEClient", "Attempting SSE connection to: " + m_url);

            // Use RequestBuilder for SSE streaming request
            HttpRequest sse_request = RequestBuilder(m_url)
                .method(HttpMethod::GET)
                .headers(sse_headers)
                .follow_redirects(false)
                .build();

            // Set up streaming callback to process data chunks as they arrive
            auto streaming_callback = [this](const std::string& data_chunk) {
                LOG_DEBUG("SSEClient", "Received data chunk of size: " + std::to_string(data_chunk.size()));
                if (m_running) {
                    if (!m_initial_connection_succeeded.load()) {
                        m_initial_connection_succeeded.store(true);
                        m_connected.store(true);
                        LOG_INFO("SSEClient", "SSE connection successfully established for: " + m_url);
                    }
                    this->process_streaming_data(data_chunk);
                }
            };

            // Don't mark as connected until we actually establish the stream
            LOG_DEBUG("SSEClient", "Attempting to establish SSE stream...");

            // Start streaming - this will block and call the callback for each chunk
            // Pass the m_running flag as the stop flag so CURL can abort cleanly
            auto result = m_http_client->execute_streaming(sse_request, streaming_callback, &m_running);

            if (!result) {
                m_connected.store(false);
                if (m_initial_connection_succeeded.load()) {
                    LOG_WARNING("SSEClient", "SSE streaming failed, will retry");
                } else {
                    LOG_WARNING("SSEClient", "Initial SSE connection failed for: " + m_url);
                }
            } else {
                LOG_INFO("SSEClient", "SSE streaming completed normally");
            }

        } catch (const std::exception& e) {
            m_connected.store(false);
            LOG_ERROR("SSEClient", "SSE event loop error: " + std::string(e.what()));
        }

        m_connected.store(false);

        if (!m_running || (!m_initial_connection_succeeded.load() && m_connection_attempts >= MAX_INITIAL_CONNECTION_ATTEMPTS)) {
            break;
        }

        // Wait before next attempt/poll with frequent shutdown checks
        auto sleep_duration = std::chrono::seconds(2);  // Shorter reconnect delay
        auto sleep_start = std::chrono::steady_clock::now();

        while (m_running && (std::chrono::steady_clock::now() - sleep_start) < sleep_duration) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    m_connected = false;
    LOG_DEBUG("SSEClient", "SSE event loop finished");
}

void SSEClient::parse_sse_data(const std::string& data) {
    // Simple SSE parsing - in a real implementation this would handle streaming data
    std::istringstream stream(data);
    std::string line;
    std::string event_data;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            // Empty line indicates end of event
            if (!event_data.empty()) {
                handle_event(event_data);
                event_data.clear();
            }
        } else if (line.substr(0, 5) == "data:") {
            // Extract data line
            std::string data_line = line.substr(5);
            if (!data_line.empty() && data_line[0] == ' ') {
                data_line = data_line.substr(1);
            }
            if (!event_data.empty()) {
                event_data += "\n";
            }
            event_data += data_line;
        }
        // Ignore other SSE fields (event:, id:, retry:) for now
    }

    // Handle any remaining event data
    if (!event_data.empty()) {
        handle_event(event_data);
    }
}

void SSEClient::process_sse_line(const std::string& line) {
    if (line.empty()) {
        // Empty line indicates end of event - dispatch it
        if (!m_current_event_data.empty()) {
            handle_event(m_current_event_data);

            // Reset event state for next event
            m_current_event_data.clear();
            m_current_event_type.clear();
            m_current_event_id.clear();
        }
        return;
    }

    // Handle comment lines
    if (line[0] == ':') {
        if (line.find(": connection established") == 0) {
            if (!m_initial_connection_succeeded.load()) {
                m_initial_connection_succeeded.store(true);
                m_connected.store(true);
                LOG_INFO("SSEClient", "SSE connection successfully established for: " + m_url);
            }
        }
        return;
    }

    // Parse field and value
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        // Treat as field with empty value
        std::string field = line;
        process_sse_field(field, "");
    } else {
        std::string field = line.substr(0, colon_pos);
        std::string value = line.substr(colon_pos + 1);

        // Remove leading space from value if present
        if (!value.empty() && value[0] == ' ') {
            value = value.substr(1);
        }

        process_sse_field(field, value);
    }
}

void SSEClient::process_sse_field(const std::string& field, const std::string& value) {
    if (field == "data") {
        if (!m_current_event_data.empty()) {
            m_current_event_data += "\n";
        }
        m_current_event_data += value;
    } else if (field == "event") {
        m_current_event_type = value;
    } else if (field == "id") {
        m_current_event_id = value;
    } else if (field == "retry") {
        // Could implement retry delay handling here if needed
    }
    // Other fields are ignored per SSE spec
}

void SSEClient::handle_event(const std::string& event_data) {
    if (event_data.empty()) {
        return;
    }

    LOG_DEBUG("SSEClient", "Received SSE event: " + event_data.substr(0, 100) +
                   (event_data.length() > 100 ? "..." : ""));

    if (m_callback) {
        try {
            m_callback(event_data);
        } catch (const std::exception& e) {
            LOG_ERROR("SSEClient", "Error in SSE callback: " + std::string(e.what()));
        }
    }
}

} // namespace services
} // namespace presence_for_plex