#pragma once

// Standard library headers
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Platform-specific headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#if defined(_WIN32) && !defined(htole32)
#define htole32(x) (x) // little-endian host
#define le32toh(x) (x)
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32(x) OSSwapHostToLittleInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#elif defined(__linux__) || defined(__unix__)
#include <endian.h>
#endif

// Third-party headers
#include <nlohmann/json.hpp>

// Project headers
#include "logger.h"

// Discord IPC opcodes
enum DiscordOpcodes
{
    OP_HANDSHAKE = 0,
    OP_FRAME = 1,
    OP_CLOSE = 2,
    OP_PING = 3,
    OP_PONG = 4
};

/**
 * Class handling the low-level IPC communication with Discord
 *
 * This class manages the connection to Discord's local IPC socket/pipe,
 * allowing sending and receiving of Discord Rich Presence messages.
 */
class DiscordIPC
{
public:
    /**
     * Constructor initializes the Discord IPC connection state
     */
    DiscordIPC();

    /**
     * Destructor ensures the connection is properly closed
     */
    ~DiscordIPC();

    // Connection management
    /**
     * Attempts to establish a connection to Discord via IPC
     *
     * On Windows, tries to connect to Discord's named pipes
     * On macOS/Linux, tries to connect to Discord's Unix domain sockets
     *
     * @return true if connection was successful, false otherwise
     */
    bool openPipe();

    /**
     * Closes the current connection to Discord
     */
    void closePipe();

    /**
     * Checks if the connection to Discord is active
     *
     * @return true if connected, false otherwise
     */
    bool isConnected() const;

    // IPC operations
    /**
     * Writes a framed message to Discord
     *
     * @param opcode The Discord IPC opcode for this message
     * @param payload The message content as a JSON string
     * @return true if write was successful, false if it failed
     */
    bool writeFrame(int opcode, const std::string &payload);

    /**
     * Reads a framed message from Discord
     *
     * @param opcode Output parameter that will contain the received opcode
     * @param data Output parameter that will contain the received data
     * @return true if read was successful, false if it failed
     */
    bool readFrame(int &opcode, std::string &data);

    /**
     * Sends the initial handshake message to Discord
     *
     * @param clientId The Discord application client ID to use for Rich Presence
     * @return true if handshake was sent successfully, false otherwise
     */
    bool sendHandshake(uint64_t clientId);

    /**
     * Sends a ping message to check if Discord is still responsive
     *
     * @return true if ping was sent successfully, false otherwise
     */
    bool sendPing();

private:
    /** Flag indicating whether there's an active connection to Discord */
    std::atomic<bool> connected;

#ifdef _WIN32
    /** Windows-specific handle to the Discord IPC pipe */
    HANDLE pipe_handle;
#else
    /** Unix-specific file descriptor for the Discord IPC socket */
    int pipe_fd;
#endif
};