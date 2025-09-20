#pragma once

#include <atomic>
#include <string>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace presence_for_plex::services {

class DiscordIPC {
 public:
  explicit DiscordIPC(std::string client_id);
  ~DiscordIPC();

  // Delete copy constructor and assignment operator
  DiscordIPC(const DiscordIPC&) = delete;
  DiscordIPC& operator=(const DiscordIPC&) = delete;

  // Connection management
  bool connect();
  void disconnect();
  [[nodiscard]] bool is_connected() const;

  // Discord Rich Presence operations
  bool send_presence(const nlohmann::json& presence_data);
  bool clear_presence();
  bool send_ping();

 private:
  std::string m_client_id;
  std::atomic<bool> m_connected;

#ifdef _WIN32
  HANDLE m_pipe = INVALID_HANDLE_VALUE;
  bool connect_windows();
  bool write_data(const void* data, size_t size) const;
  bool read_data(void* data, size_t size) const;
#else
  int m_socket = -1;
  bool connect_unix();
  bool write_data(const void* data, size_t size) const;
  bool read_data(void* data, size_t size) const;
#endif

  // IPC protocol methods
  bool write_frame(uint32_t opcode, const std::string& payload);
  bool read_frame(uint32_t& opcode, std::string& data);
  bool perform_handshake();
  bool send_payload(const nlohmann::json& payload);

  static uint32_t get_process_id();
};

} // namespace presence_for_plex::services