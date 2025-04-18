#pragma once
#include <string>
#include <functional>

// Discord IPC opcodes
enum DiscordOpcodes
{
    OP_HANDSHAKE = 0,
    OP_FRAME = 1,
    OP_CLOSE = 2,
    OP_PING = 3,
    OP_PONG = 4
};

// Callback type for Discord IPC responses
using ResponseCallback = std::function<void(int opcode, const std::string& message)>;

// Handles low-level IPC communication with Discord
class DiscordIPC
{
public:
    DiscordIPC();
    ~DiscordIPC();

    // Connection management
    bool connect();
    void disconnect();
    bool isConnected() const;
    
    // IPC frame operations
    bool writeFrame(int opcode, const std::string &payload);
    bool readFrame(int &opcode, std::string &data);
    
    // Discord specific operations
    bool sendHandshake(uint64_t clientId);
    bool sendPing();
    bool sendActivity(const std::string &payload);
    
private:
    // Platform-specific implementations
    bool connectToDiscordWindows();
    bool connectToDiscordUnix();
    void disconnectFromDiscord();

#ifdef _WIN32
    void* pipe_handle;
#else
    int pipe_fd;
#endif
};