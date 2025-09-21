#include "presence_for_plex/services/discord_ipc.hpp"
#include "presence_for_plex/utils/logger.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#endif

// Platform-specific endian conversion
#if defined(_WIN32) && !defined(htole32)
#define htole32(x) (x) // Windows is little-endian
#define le32toh(x) (x)
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32(x) OSSwapHostToLittleInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#elif defined(__linux__) || defined(__unix__)
#include <endian.h>
#endif

namespace presence_for_plex::services {

using Json = nlohmann::json;

constexpr uint32_t DISCORD_VERSION = 1;

enum class OpCode : uint32_t {
  HANDSHAKE = 0,
  FRAME = 1,
  CLOSE = 2,
  PING = 3,
  PONG = 4
};

DiscordIPC::DiscordIPC(std::string client_id)
    : m_client_id(std::move(client_id)), m_connected(false) {
#ifdef _WIN32
  m_pipe = INVALID_HANDLE_VALUE;
#else
  m_socket = -1;
#endif
}

DiscordIPC::~DiscordIPC() {
  disconnect();
}

bool DiscordIPC::connect() {
    if (m_connected) {
      return true;
    }

    PLEX_LOG_INFO("DiscordIPC", "Attempting to connect to Discord");

#ifdef _WIN32
    return connect_windows();
#else
    return connect_unix();
#endif
  }

void DiscordIPC::disconnect() {
    if (!m_connected) {
      return;
    }

    PLEX_LOG_INFO("DiscordIPC", "Disconnecting from Discord");

#ifdef _WIN32
    if (m_pipe != INVALID_HANDLE_VALUE) {
      CloseHandle(m_pipe);
      m_pipe = INVALID_HANDLE_VALUE;
    }
#else
    if (m_socket >= 0) {
      close(m_socket);
      m_socket = -1;
    }
#endif

    m_connected = false;
  }

bool DiscordIPC::is_connected() const {
  return m_connected;
}

bool DiscordIPC::send_presence(const Json& presence_data) {
    if (!m_connected) {
      PLEX_LOG_WARNING("DiscordIPC", "Not connected to Discord");
      return false;
    }

    const Json payload = {
        {"cmd", "SET_ACTIVITY"},
        {"nonce",
         std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count())},
        {"args", {{"pid", get_process_id()}, {"activity", presence_data}}}};

    return send_payload(payload);
  }

bool DiscordIPC::clear_presence() {
    if (!m_connected) {
      return false;
    }

    const Json payload = {
        {"cmd", "SET_ACTIVITY"},
        {"nonce",
         std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count())},
        {"args", {{"pid", get_process_id()}, {"activity", nullptr}}}};

    return send_payload(payload);
  }

bool DiscordIPC::send_ping() {
    if (!m_connected) {
      PLEX_LOG_WARNING("DiscordIPC", "Can't send ping: not connected");
      return false;
    }

    PLEX_LOG_DEBUG("DiscordIPC", "Sending ping");
    const Json ping = Json::object();

    if (!write_frame(static_cast<uint32_t>(OpCode::PING), ping.dump())) {
      PLEX_LOG_WARNING("DiscordIPC", "Failed to send ping frame");
      return false;
    }

    // Read and validate PONG response
    uint32_t response_opcode;
    std::string response_data;
    if (!read_frame(response_opcode, response_data)) {
      PLEX_LOG_WARNING("DiscordIPC", "Failed to read ping response");
      m_connected = false;
      return false;
    }

    if (response_opcode != static_cast<uint32_t>(OpCode::PONG)) {
      PLEX_LOG_WARNING("DiscordIPC",
        "Unexpected response to ping. Expected PONG (" +
        std::to_string(static_cast<uint32_t>(OpCode::PONG)) +
        "), got " + std::to_string(response_opcode));
      return false;
    }

    PLEX_LOG_DEBUG("DiscordIPC", "Ping successful, received PONG");
    return true;
  }

#ifdef _WIN32
bool DiscordIPC::connect_windows() {
    for (int i = 0; i < 10; ++i) {
      std::string pipe_name = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
      PLEX_LOG_DEBUG("DiscordIPC", "Trying pipe: " + pipe_name);

      m_pipe = CreateFileA(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                           nullptr, OPEN_EXISTING, 0, nullptr);

      if (m_pipe != INVALID_HANDLE_VALUE) {
        // Try setting pipe to message mode
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(m_pipe, &mode, nullptr, nullptr)) {
          DWORD error = GetLastError();
          PLEX_LOG_DEBUG("DiscordIPC", "Warning: Failed to set pipe mode. Using default mode. Error: " + std::to_string(error));
        }

        PLEX_LOG_INFO("DiscordIPC", "Connected to pipe: " + pipe_name);
        m_connected = true;  // Set connected before handshake

        if (!perform_handshake()) {
          m_connected = false;
          CloseHandle(m_pipe);
          m_pipe = INVALID_HANDLE_VALUE;
          PLEX_LOG_DEBUG("DiscordIPC", "Handshake failed, trying next pipe");
          continue;
        }

        return true;
      }

      DWORD error = GetLastError();
      PLEX_LOG_DEBUG("DiscordIPC", "Failed to connect to " + pipe_name + ": error code " + std::to_string(error));
    }

    PLEX_LOG_ERROR("DiscordIPC", "Failed to connect to any Discord pipe. Is Discord running?");
    return false;
  }

bool DiscordIPC::write_data(const void* data, size_t size) const {
    DWORD bytes_written;
    if (!WriteFile(m_pipe, data, static_cast<DWORD>(size), &bytes_written, nullptr) ||
        bytes_written != size) {
      DWORD error = GetLastError();
      PLEX_LOG_ERROR("DiscordIPC", "Failed to write data to pipe. Error code: " + std::to_string(error));
      return false;
    }
    FlushFileBuffers(m_pipe);
    return true;
  }

bool DiscordIPC::read_data(void* data, size_t size) const {
    DWORD bytes_read;
    if (!ReadFile(m_pipe, data, static_cast<DWORD>(size), &bytes_read, nullptr) ||
        bytes_read != size) {
      DWORD error = GetLastError();
      PLEX_LOG_ERROR("DiscordIPC", "Failed to read data from pipe. Error code: " + std::to_string(error));
      return false;
    }
    return true;
  }

#else
bool DiscordIPC::connect_unix() {
    // Try standard XDG and temp directories
    for (int i = 0; i < 10; ++i) {
      std::string socket_path;

      // Check XDG_RUNTIME_DIR first
      const char* xdg_runtime = getenv("XDG_RUNTIME_DIR");
      const char* home = getenv("HOME");

      if (xdg_runtime) {
        socket_path = std::string(xdg_runtime) + "/discord-ipc-" + std::to_string(i);
      } else if (home) {
        socket_path = std::string(home) + "/.discord-ipc-" + std::to_string(i);
      } else {
        PLEX_LOG_WARNING("DiscordIPC", "Could not determine user directory, skipping socket " + std::to_string(i));
        continue;
      }

      PLEX_LOG_DEBUG("DiscordIPC", "Trying socket: " + socket_path);

      m_socket = socket(AF_UNIX, SOCK_STREAM, 0);
      if (m_socket < 0) {
        PLEX_LOG_DEBUG("DiscordIPC", "Failed to create socket: " + std::string(strerror(errno)));
        continue;
      }

      struct sockaddr_un addr {};
      addr.sun_family = AF_UNIX;

      if (socket_path.length() >= sizeof(addr.sun_path)) {
        PLEX_LOG_WARNING("DiscordIPC", "Socket path too long: " + socket_path);
        close(m_socket);
        m_socket = -1;
        continue;
      }

      strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

      if (::connect(m_socket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        PLEX_LOG_INFO("DiscordIPC", "Connected to socket: " + socket_path);
        m_connected = true;  // Set connected before handshake

        if (!perform_handshake()) {
          m_connected = false;
          close(m_socket);
          m_socket = -1;
          PLEX_LOG_DEBUG("DiscordIPC", "Handshake failed, trying next socket");
          continue;
        }

        return true;
      }

      PLEX_LOG_DEBUG("DiscordIPC", "Failed to connect to socket: " + socket_path + ": " + std::string(strerror(errno)));
      close(m_socket);
      m_socket = -1;
    }

    // Try Linux-specific paths for Snap and Flatpak
    std::vector<std::string> special_paths = {
      "/run/user/" + std::to_string(getuid()) + "/snap.discord/discord-ipc-0",
      "/run/user/" + std::to_string(getuid()) + "/app/com.discordapp.Discord/discord-ipc-0"
    };

    for (const auto& path : special_paths) {
      PLEX_LOG_DEBUG("DiscordIPC", "Trying special path: " + path);

      m_socket = socket(AF_UNIX, SOCK_STREAM, 0);
      if (m_socket < 0) {
        continue;
      }

      struct sockaddr_un addr {};
      addr.sun_family = AF_UNIX;
      strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

      if (::connect(m_socket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        PLEX_LOG_INFO("DiscordIPC", "Connected to special socket: " + path);
        m_connected = true;  // Set connected before handshake

        if (!perform_handshake()) {
          m_connected = false;
          close(m_socket);
          m_socket = -1;
          PLEX_LOG_DEBUG("DiscordIPC", "Handshake failed for special socket");
          continue;
        }

        return true;
      }

      PLEX_LOG_DEBUG("DiscordIPC", "Failed to connect to special socket: " + path + ": " + std::string(strerror(errno)));
      close(m_socket);
      m_socket = -1;
    }

    PLEX_LOG_ERROR("DiscordIPC", "Failed to connect to any Discord socket. Is Discord running?");
    return false;
  }

bool DiscordIPC::write_data(const void* data, size_t size) const {
    const ssize_t sent = send(m_socket, data, size, 0);
    if (sent != static_cast<ssize_t>(size)) {
      PLEX_LOG_ERROR("DiscordIPC", "Failed to write data to socket: " + std::string(strerror(errno)));
      return false;
    }
    return true;
  }

bool DiscordIPC::read_data(void* data, size_t size) const {
    size_t total_read = 0;
    while (total_read < size) {
      const ssize_t received = recv(m_socket, static_cast<char*>(data) + total_read, size - total_read, 0);
      if (received <= 0) {
        if (received < 0) {
          PLEX_LOG_ERROR("DiscordIPC", "Error reading from socket: " + std::string(strerror(errno)));
        } else {
          PLEX_LOG_ERROR("DiscordIPC", "Socket closed unexpectedly");
        }
        return false;
      }
      total_read += static_cast<size_t>(received);
    }
    return true;
  }
#endif

bool DiscordIPC::write_frame(uint32_t opcode, const std::string& payload) {
    if (!m_connected) {
      PLEX_LOG_DEBUG("DiscordIPC", "Can't write frame: not connected");
      return false;
    }

    PLEX_LOG_DEBUG("DiscordIPC", "Writing frame - Opcode: " + std::to_string(opcode) + ", Data length: " + std::to_string(payload.size()));

    // Create properly formatted Discord IPC message with endianness handling
    const uint32_t len = static_cast<uint32_t>(payload.size());
    std::vector<char> buf(8 + len);
    auto* p = reinterpret_cast<uint32_t*>(buf.data());
    p[0] = htole32(opcode);
    p[1] = htole32(len);
    std::memcpy(buf.data() + 8, payload.data(), len);

    return write_data(buf.data(), 8 + len);
  }

bool DiscordIPC::read_frame(uint32_t& opcode, std::string& data) {
    if (!m_connected) {
      PLEX_LOG_DEBUG("DiscordIPC", "Can't read frame: not connected");
      return false;
    }

    // Read the 8-byte header
    char header[8];
    if (!read_data(header, 8)) {
      PLEX_LOG_ERROR("DiscordIPC", "Failed to read frame header");
      m_connected = false;
      return false;
    }

    // Parse header with endianness handling
    uint32_t raw_opcode, raw_length;
    std::memcpy(&raw_opcode, header, 4);
    std::memcpy(&raw_length, header + 4, 4);
    opcode = le32toh(raw_opcode);
    const uint32_t length = le32toh(raw_length);

    PLEX_LOG_DEBUG("DiscordIPC", "Frame header - Opcode: " + std::to_string(opcode) + ", Length: " + std::to_string(length));

    if (length == 0) {
      data.clear();
      return true;
    }

    // Read payload
    data.resize(length);
    if (!read_data(data.data(), length)) {
      PLEX_LOG_ERROR("DiscordIPC", "Failed to read frame payload");
      m_connected = false;
      return false;
    }

    return true;
  }

bool DiscordIPC::perform_handshake() {
    const Json handshake = {{"v", DISCORD_VERSION}, {"client_id", m_client_id}};
    const std::string handshake_str = handshake.dump();

    PLEX_LOG_INFO("DiscordIPC", "Sending handshake with client ID: " + m_client_id);
    PLEX_LOG_DEBUG("DiscordIPC", "Handshake payload: " + handshake_str);

    if (!write_frame(static_cast<uint32_t>(OpCode::HANDSHAKE), handshake_str)) {
      PLEX_LOG_ERROR("DiscordIPC", "Failed to send handshake");
      return false;
    }

    // Read response
    uint32_t response_opcode;
    std::string response_data;
    if (!read_frame(response_opcode, response_data)) {
      PLEX_LOG_ERROR("DiscordIPC", "Failed to read handshake response");
      return false;
    }

    if (response_opcode != static_cast<uint32_t>(OpCode::FRAME)) {
      PLEX_LOG_ERROR("DiscordIPC", "Invalid handshake response opcode: " + std::to_string(response_opcode));
      return false;
    }

    try {
      const auto response_json = Json::parse(response_data);
      PLEX_LOG_DEBUG("DiscordIPC", "Handshake response: " + response_data);

      if (response_json.contains("evt") && response_json["evt"] == "READY") {
        PLEX_LOG_INFO("DiscordIPC", "Handshake successful");
        return true;
      } else {
        PLEX_LOG_ERROR("DiscordIPC", "Handshake failed - not ready");
        return false;
      }
    } catch (const std::exception& e) {
      PLEX_LOG_ERROR("DiscordIPC", "Failed to parse handshake response: " + std::string(e.what()));
      return false;
    }
  }

bool DiscordIPC::send_payload(const Json& payload) {
    const std::string payload_str = payload.dump();
    PLEX_LOG_DEBUG("DiscordIPC", "Sending payload: " + payload_str);

    if (!write_frame(static_cast<uint32_t>(OpCode::FRAME), payload_str)) {
      PLEX_LOG_ERROR("DiscordIPC", "Failed to write frame");
      return false;
    }

    // Read and process the response
    uint32_t response_opcode;
    std::string response_data;
    if (!read_frame(response_opcode, response_data)) {
      PLEX_LOG_WARNING("DiscordIPC", "Failed to read response after sending payload");
      return false;
    }

    // Log the response for debugging
    PLEX_LOG_DEBUG("DiscordIPC", "Response received - Opcode: " + std::to_string(response_opcode) + ", Data: " + response_data);

    // Check for errors in the response
    if (!response_data.empty()) {
      try {
        const auto response_json = Json::parse(response_data);
        if (response_json.contains("evt") && response_json["evt"] == "ERROR") {
          PLEX_LOG_ERROR("DiscordIPC", "Discord returned error: " + response_data);
          return false;
        }
      } catch (const std::exception& e) {
        PLEX_LOG_WARNING("DiscordIPC", "Failed to parse response: " + std::string(e.what()));
      }
    }

    return true;
  }

uint32_t DiscordIPC::get_process_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<uint32_t>(getpid());
#endif
  }

}  // namespace presence_for_plex::services
