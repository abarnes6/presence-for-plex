#pragma once

#include <string>
#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

// Discord IPC opcodes
enum DiscordOpcodes
{
    OP_HANDSHAKE = 0,
    OP_FRAME = 1,
    OP_CLOSE = 2,
    OP_PING = 3,
    OP_PONG = 4
};

// Class handling the low-level IPC communication with Discord
class DiscordIPC
{
public:
    DiscordIPC();
    ~DiscordIPC();

    // Connection management
    bool openPipe();
    void closePipe();
    bool isConnected() const;

    // IPC operations
    bool writeFrame(int opcode, const std::string &payload);
    bool readFrame(int &opcode, std::string &data);
    bool sendHandshake(uint64_t clientId);
    bool sendPing();

private:
    std::atomic<bool> connected;

#ifdef _WIN32
    HANDLE pipe_handle;
#else
    int pipe_fd;
#endif
};