#include "logger.h"

// Constructor
Logger::Logger() : logLevel(LogLevel::Info), logToFile(false)
{
#ifdef _WIN32
    // Enable ANSI color codes on Windows
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    useColorOutput = true;
#else
    useColorOutput = true;
#endif
}

// Get singleton instance
Logger &Logger::getInstance()
{
    static Logger instance;
    return instance;
}

// Set the current log level
void Logger::setLogLevel(LogLevel level)
{
    logLevel = level;
}

// Get the current log level
LogLevel Logger::getLogLevel() const
{
    return logLevel;
}

// Get formatted timestamp with milliseconds
std::string Logger::getTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch())
                      .count() %
                  1000;
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;

#ifdef _WIN32
    localtime_s(&now_tm, &now_c);
#else
    now_tm = *localtime(&now_c);
#endif

    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%H:%M:%S") << "."
        << std::setfill('0') << std::setw(3) << now_ms;
    return oss.str();
}

// Get string representation of log level
std::string Logger::getLevelString(LogLevel level) const
{
    switch (level)
    {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    default:
        return "NONE";
    }
}

// Apply color to text based on log level
std::string Logger::colorize(const std::string &text, LogLevel level) const
{
    if (!useColorOutput)
        return text;

    switch (level)
    {
    case LogLevel::Debug:
        return ANSI_BLUE + text + ANSI_RESET;
    case LogLevel::Info:
        return ANSI_GREEN + text + ANSI_RESET;
    case LogLevel::Warning:
        return ANSI_YELLOW + text + ANSI_RESET;
    case LogLevel::Error:
        return ANSI_RED + text + ANSI_RESET;
    default:
        return text;
    }
}

// Initialize file logging
void Logger::initFileLogging(const std::filesystem::path &logFilePath, bool clearExisting)
{
    std::lock_guard<std::mutex> lock(logMutex);

    // Create the directory if it doesn't exist
    if (!std::filesystem::exists(logFilePath.parent_path()))
    {
        std::filesystem::create_directories(logFilePath.parent_path());
    }

    // Open the log file with appropriate mode
    auto openMode = std::ios::out;
    if (clearExisting)
    {
        openMode |= std::ios::trunc; // Clear the file
    }
    else
    {
        openMode |= std::ios::app; // Append to the file
    }

    // Close the file if it's already open
    if (logFile.is_open())
    {
        logFile.close();
    }

    logFile.open(logFilePath, openMode);

    if (logFile.is_open())
    {
        logToFile = true;

        // Log the start of the session
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm;

#ifdef _WIN32
        localtime_s(&now_tm, &now_c);
#else
        now_tm = *localtime(&now_c);
#endif

        logFile << "==================================================================" << std::endl;
        logFile << "Log session started at " << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S") << std::endl;
        logFile << "==================================================================" << std::endl;
        logFile.flush(); // Ensure the header is written immediately
    }
    else
    {
        std::cerr << "Failed to open log file: " << logFilePath.string() << std::endl;
        logToFile = false;
    }
}

// Log a debug message
void Logger::debug(const std::string &component, const std::string &message)
{
    if (logLevel <= LogLevel::Debug)
    {
        log(LogLevel::Debug, component, message);
    }
}

// Log an info message
void Logger::info(const std::string &component, const std::string &message)
{
    if (logLevel <= LogLevel::Info)
    {
        log(LogLevel::Info, component, message);
    }
}

// Log a warning message
void Logger::warning(const std::string &component, const std::string &message)
{
    if (logLevel <= LogLevel::Warning)
    {
        log(LogLevel::Warning, component, message);
    }
}

// Log an error message
void Logger::error(const std::string &component, const std::string &message)
{
    if (logLevel <= LogLevel::Error)
    {
        log(LogLevel::Error, component, message);
    }
}

// Internal logging implementation
void Logger::log(LogLevel level, const std::string &component, const std::string &message)
{
    std::lock_guard<std::mutex> lock(logMutex);

    std::string timestamp = getTimestamp();
    std::string levelStr = getLevelString(level);

    // Format the log message
    std::stringstream formatted;
    formatted << "[" << timestamp << "] "
              << "[" << levelStr << "] "
              << "[" << component << "] "
              << message;

    std::string consoleOutput = formatted.str();
    std::string fileOutput = formatted.str();

#if !defined(NDEBUG) || !defined(_WIN32)
    // Apply color for console output
    std::cout << colorize(consoleOutput, level) << std::endl;
#endif

    // Write to log file if enabled (without color codes)
    if (logToFile && logFile.is_open())
    {
        logFile << fileOutput << std::endl;
        logFile.flush(); // Ensure immediate writing
    }
}