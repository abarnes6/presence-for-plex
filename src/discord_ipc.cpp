#include "discord_ipc.h"

// Platform-specific localtime function
#ifdef _WIN32
inline void safe_localtime(std::tm *tm_ptr, const std::time_t *time_ptr)
{
    localtime_s(tm_ptr, time_ptr);
}
#else
inline void safe_localtime(std::tm *tm_ptr, const std::time_t *time_ptr)
{
    *tm_ptr = *localtime(time_ptr);
}
#endif

using json = nlohmann::json;

DiscordIPC::DiscordIPC() : connected(false)
{
#ifdef _WIN32
    pipe_handle = INVALID_HANDLE_VALUE;
#else
    pipe_fd = -1;
#endif
}

DiscordIPC::~DiscordIPC()
{
    if (connected)
    {
        closePipe();
    }
}

#if defined(_WIN32)
bool DiscordIPC::openPipe()
{
    // Windows implementation using named pipes
    LOG_INFO("DiscordIPC", "Attempting to connect to Discord via Windows named pipes");
    for (int i = 0; i < 10; i++)
    {
        std::string pipeName = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        LOG_DEBUG("DiscordIPC", "Trying pipe: " + pipeName);

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
                LOG_DEBUG("DiscordIPC", "Warning: Failed to set pipe mode. Using default mode. Error: " + std::to_string(error));
            }

            LOG_INFO("DiscordIPC", "Successfully connected to Discord pipe: " + pipeName);
            connected = true;
            return true;
        }

        // Log the specific error for debugging
        DWORD error = GetLastError();
        LOG_DEBUG("DiscordIPC", "Failed to connect to " + pipeName + ": error code " + std::to_string(error));
    }
    LOG_INFO("DiscordIPC", "Could not connect to any Discord pipe. Is Discord running?");
    return false;
}

#elif defined(__APPLE__)
bool DiscordIPC::openPipe()
{
    const char *temp = getenv("TMPDIR");
    LOG_INFO("DiscordIPC", "Attempting to connect to Discord via Unix sockets on macOS");
    for (int i = 0; i < 10; i++)
    {
        std::string socket_path;
        if (temp)
        {
            socket_path = std::string(temp) + "discord-ipc-" + std::to_string(i);
        }
        else
        {
            LOG_WARNING("DiscordIPC", "Could not determine temporary directory, skipping socket " + std::to_string(i));
            continue;
        }

        LOG_DEBUG("DiscordIPC", "Trying socket: " + socket_path);

        pipe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (pipe_fd == -1)
        {
            LOG_DEBUG("DiscordIPC", "Failed to create socket: " + std::string(strerror(errno)));
            continue;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;

        // Make sure we don't overflow the sun_path buffer
        if (socket_path.length() >= sizeof(addr.sun_path))
        {
            LOG_WARNING("DiscordIPC", "Socket path too long: " + socket_path);
            close(pipe_fd);
            pipe_fd = -1;
            continue;
        }

        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(pipe_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
        {
            LOG_INFO("DiscordIPC", "Successfully connected to Discord socket: " + socket_path);

            connected = true;
            return true;
        }

        LOG_DEBUG("DiscordIPC", "Failed to connect to socket: " + socket_path + ": " + std::string(strerror(errno)));
        close(pipe_fd);
        pipe_fd = -1;
    }

    LOG_INFO("DiscordIPC", "Could not connect to any Discord socket. Is Discord running?");
    return false;
}
#elif defined(__linux__) || defined(__unix__)
bool DiscordIPC::openPipe()
{
    // Unix implementation using sockets
    LOG_INFO("DiscordIPC", "Attempting to connect to Discord via Unix sockets");

    // First try conventional socket paths
    for (int i = 0; i < 10; i++)
    {
        std::string socket_path;
        // Check environment variables first (XDG standard)
        const char *xdgRuntime = getenv("XDG_RUNTIME_DIR");
        const char *home = getenv("HOME");

        if (xdgRuntime)
        {
            socket_path = std::string(xdgRuntime) + "/discord-ipc-" + std::to_string(i);
        }
        else if (home)
        {
            socket_path = std::string(home) + "/.discord-ipc-" + std::to_string(i);
        }
        else
        {
            LOG_WARNING("DiscordIPC", "Could not determine user home directory, skipping socket " + std::to_string(i));
            continue;
        }

        LOG_DEBUG("DiscordIPC", "Trying socket: " + socket_path);

        pipe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (pipe_fd == -1)
        {
            LOG_DEBUG("DiscordIPC", "Failed to create socket: " + std::string(strerror(errno)));
            continue;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;

        // Make sure we don't overflow the sun_path buffer
        if (socket_path.length() >= sizeof(addr.sun_path))
        {
            LOG_WARNING("DiscordIPC", "Socket path too long: " + socket_path);
            close(pipe_fd);
            pipe_fd = -1;
            continue;
        }

        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(pipe_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
        {
            LOG_INFO("DiscordIPC", "Successfully connected to Discord socket: " + socket_path);

            connected = true;
            return true;
        }

        LOG_DEBUG("DiscordIPC", "Failed to connect to socket: " + socket_path + ": " + std::string(strerror(errno)));
        close(pipe_fd);
        pipe_fd = -1;
    }

    // Try Linux-specific paths
    // Try snap-specific Discord socket paths
    std::string snap_path = "/run/user/" + std::to_string(getuid()) + "/snap.discord/discord-ipc-0";
    LOG_DEBUG("DiscordIPC", "Trying Snap socket: " + snap_path);

    pipe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (pipe_fd != -1)
    {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, snap_path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(pipe_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
        {
            LOG_INFO("DiscordIPC", "Successfully connected to Discord Snap socket: " + snap_path);
            connected = true;
            return true;
        }

        LOG_DEBUG("DiscordIPC", "Failed to connect to Snap socket: " + snap_path + ": " + std::string(strerror(errno)));
        close(pipe_fd);
        pipe_fd = -1;
    }

    // Try flatpak-specific Discord socket paths
    std::string flatpak_path = "/run/user/" + std::to_string(getuid()) + "/app/com.discordapp.Discord/discord-ipc-0";
    LOG_DEBUG("DiscordIPC", "Trying Flatpak socket: " + flatpak_path);

    pipe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (pipe_fd != -1)
    {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, flatpak_path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(pipe_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
        {
            LOG_INFO("DiscordIPC", "Successfully connected to Discord Flatpak socket: " + flatpak_path);
            connected = true;
            return true;
        }

        LOG_DEBUG("DiscordIPC", "Failed to connect to Flatpak socket: " + flatpak_path + ": " + std::string(strerror(errno)));
        close(pipe_fd);
        pipe_fd = -1;
    }

    LOG_INFO("DiscordIPC", "Could not connect to any Discord socket. Is Discord running?");
    return false;
}
#endif

void DiscordIPC::closePipe()
{
    connected = false;
    LOG_INFO("DiscordIPC", "Disconnecting from Discord...");
#ifdef _WIN32
    if (pipe_handle != INVALID_HANDLE_VALUE)
    {
        LOG_DEBUG("DiscordIPC", "Closing pipe handle");
        CloseHandle(pipe_handle);
        pipe_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (pipe_fd != -1)
    {
        LOG_DEBUG("DiscordIPC", "Closing socket");
        close(pipe_fd);
        pipe_fd = -1;
    }
#endif
    LOG_INFO("DiscordIPC", "Disconnected from Discord");
}

bool DiscordIPC::isConnected() const
{
    return connected;
}

bool DiscordIPC::writeFrame(int opcode, const std::string &payload)
{
    if (!connected)
    {
        LOG_DEBUG("DiscordIPC", "Can't write frame: not connected");
        return false;
    }

    LOG_DEBUG("DiscordIPC", "Writing frame - Opcode: " + std::to_string(opcode) + ", Data length: " + std::to_string(payload.size()));
    LOG_DEBUG("DiscordIPC", "Writing frame data: " + payload);

    // Create a properly formatted Discord IPC message
    uint32_t len = static_cast<uint32_t>(payload.size());
    std::vector<char> buf(8 + len); // Header (8 bytes) + payload
    auto *p = reinterpret_cast<uint32_t *>(buf.data());
    p[0] = htole32(opcode); // First 4 bytes: opcode with proper endianness
    p[1] = htole32(len);    // Next 4 bytes: payload length with proper endianness
    memcpy(buf.data() + 8, payload.data(), len);

#ifdef _WIN32
    DWORD written;
    if (!WriteFile(pipe_handle, buf.data(), 8 + len, &written, nullptr) ||
        written != 8 + len)
    {
        DWORD error = GetLastError();
        LOG_ERROR("DiscordIPC", "Failed to write frame to pipe. Error code: " + std::to_string(error) + ", Bytes written: " + std::to_string(written));
        connected = false;
        return false;
    }
    LOG_DEBUG("DiscordIPC", "Successfully wrote " + std::to_string(written) + " bytes to pipe");
    FlushFileBuffers(pipe_handle);
#else
    ssize_t n = ::write(pipe_fd, buf.data(), 8 + len);
    if (n != 8 + len)
    {
        LOG_ERROR("DiscordIPC", "Failed to write frame to socket. Expected: " + std::to_string(8 + len) + ", Actual: " + std::to_string(n));
        if (n < 0)
        {
            LOG_ERROR("DiscordIPC", "Write error: " + std::string(strerror(errno)));
        }
        connected = false;
        return false;
    }
    LOG_DEBUG("DiscordIPC", "Successfully wrote " + std::to_string(n) + " bytes to socket");
#endif
    return true;
}

bool DiscordIPC::readFrame(int &opcode, std::string &data)
{
    if (!connected)
    {
        LOG_DEBUG("DiscordIPC", "Can't read frame: not connected");
        return false;
    }

    LOG_DEBUG("DiscordIPC", "Attempting to read frame from Discord");
    opcode = -1;

    // First read the 8-byte header (opcode + length)
    char header[8];
    int header_bytes_read = 0;

#ifdef _WIN32
    LOG_DEBUG("DiscordIPC", "Reading header (8 bytes)...");
    while (header_bytes_read < 8)
    {
        DWORD bytes_read;
        if (!ReadFile(pipe_handle, header + header_bytes_read, 8 - header_bytes_read, &bytes_read, NULL))
        {
            DWORD error = GetLastError();
            LOG_ERROR("DiscordIPC", "Failed to read header: error code " + std::to_string(error));
            connected = false;
            return false;
        }

        if (bytes_read == 0)
        {
            LOG_ERROR("DiscordIPC", "Read zero bytes from pipe - connection closed");
            connected = false;
            return false;
        }

        header_bytes_read += bytes_read;
        LOG_DEBUG("DiscordIPC", "Read " + std::to_string(bytes_read) + " header bytes, total: " + std::to_string(header_bytes_read) + "/8");
    }
#else
    LOG_DEBUG("DiscordIPC", "Reading header (8 bytes)...");
    while (header_bytes_read < 8)
    {
        ssize_t bytes_read = read(pipe_fd, header + header_bytes_read, 8 - header_bytes_read);
        if (bytes_read <= 0)
        {
            if (bytes_read < 0)
            {
                LOG_ERROR("DiscordIPC", "Error reading from socket: " + std::string(strerror(errno)));
            }
            else
            {
                LOG_ERROR("DiscordIPC", "Socket closed during header read");
            }
            connected = false;
            return false;
        }
        header_bytes_read += bytes_read;
        LOG_DEBUG("DiscordIPC", "Read " + std::to_string(bytes_read) + " header bytes, total: " + std::to_string(header_bytes_read) + "/8");
    }
#endif

    // Parse the header with proper endianness handling
    uint32_t raw0, raw1;
    memcpy(&raw0, header, 4);
    memcpy(&raw1, header + 4, 4);
    opcode = le32toh(raw0);
    uint32_t length = le32toh(raw1);

    LOG_DEBUG("DiscordIPC", "Frame header parsed - Opcode: " + std::to_string(opcode) + ", Expected data length: " + std::to_string(length));

    if (length == 0)
    {
        LOG_DEBUG("DiscordIPC", "Frame has zero length, no data to read");
        data.clear();
        return true;
    }

    // Now read the actual payload data
    data.resize(length);
    uint32_t data_bytes_read = 0;

#ifdef _WIN32
    LOG_DEBUG("DiscordIPC", "Reading payload (" + std::to_string(length) + " bytes)...");
    while (data_bytes_read < length)
    {
        DWORD bytes_read;
        if (!ReadFile(pipe_handle, &data[data_bytes_read], length - data_bytes_read, &bytes_read, NULL))
        {
            DWORD error = GetLastError();
            LOG_ERROR("DiscordIPC", "Failed to read data: error code " + std::to_string(error));
            connected = false;
            return false;
        }

        if (bytes_read == 0)
        {
            LOG_ERROR("DiscordIPC", "Read zero bytes from pipe during payload read - connection closed");
            connected = false;
            return false;
        }

        data_bytes_read += bytes_read;
        LOG_DEBUG("DiscordIPC", "Read " + std::to_string(bytes_read) + " data bytes, total: " + std::to_string(data_bytes_read) + "/" + std::to_string(length));
    }
#else
    LOG_DEBUG("DiscordIPC", "Reading payload (" + std::to_string(length) + " bytes)...");
    while (data_bytes_read < length)
    {
        ssize_t bytes_read = read(pipe_fd, &data[data_bytes_read], length - data_bytes_read);
        if (bytes_read <= 0)
        {
            if (bytes_read < 0)
            {
                LOG_ERROR("DiscordIPC", "Error reading from socket: " + std::string(strerror(errno)));
            }
            else
            {
                LOG_ERROR("DiscordIPC", "Socket closed during payload read");
            }
            connected = false;
            return false;
        }
        data_bytes_read += bytes_read;
        LOG_DEBUG("DiscordIPC", "Read " + std::to_string(bytes_read) + " data bytes, total: " + std::to_string(data_bytes_read) + "/" + std::to_string(length));
    }
#endif
    LOG_DEBUG("DiscordIPC", "Reading frame data: " + data);
    return true;
}

bool DiscordIPC::sendHandshake(uint64_t clientId)
{
    if (!connected)
    {
        LOG_DEBUG("DiscordIPC", "Can't send handshake: not connected");
        return false;
    }

    LOG_INFO("DiscordIPC", "Sending handshake with client ID: " + std::to_string(clientId));

    // Create handshake payload
    json payload = {
        {"client_id", std::to_string(clientId)},
        {"v", 1}};

    std::string handshake_str = payload.dump();
    LOG_DEBUG("DiscordIPC", "Handshake payload: " + handshake_str);

    return writeFrame(OP_HANDSHAKE, handshake_str);
}

bool DiscordIPC::sendPing()
{
    if (!connected)
    {
        LOG_DEBUG("DiscordIPC", "Can't send ping: not connected");
        return false;
    }

    LOG_DEBUG("DiscordIPC", "Sending ping");
    static const json ping = json::object(); // empty payload
    std::string ping_str = ping.dump();

    return writeFrame(OP_PING, ping_str);
}
