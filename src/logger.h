#pragma once

// Standard library headers
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

// Platform-specific headers
#ifdef _WIN32
#include <WinSock2.h>
#endif

// ANSI color codes
#define ANSI_RESET "\033[0m"
#define ANSI_RED "\033[31m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_GREEN "\033[32m"
#define ANSI_BLUE "\033[36m"

/**
 * Log severity levels in ascending order of importance
 */
enum class LogLevel
{
    Debug,   // Detailed information for debugging
    Info,    // General information about program execution
    Warning, // Potential issues that don't prevent execution
    Error,   // Critical issues that may prevent proper execution
    None     // Disable logging completely
};

/**
 * Singleton logger class supporting console output with colors and file output
 */
class Logger
{
public:
    static Logger &getInstance();

    // Configuration methods
    void setLogLevel(LogLevel level);
    LogLevel getLogLevel() const;
    void initFileLogging(const std::filesystem::path &logFilePath, bool clearExisting = true);

    // Logging methods
    void debug(const std::string &component, const std::string &message);
    void info(const std::string &component, const std::string &message);
    void warning(const std::string &component, const std::string &message);
    void error(const std::string &component, const std::string &message);

private:
    Logger();
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    void log(LogLevel level, const std::string &component, const std::string &message);
    std::string getTimestamp() const;
    std::string getLevelString(LogLevel level) const;
    std::string colorize(const std::string &text, LogLevel level) const;

    LogLevel logLevel;
    std::mutex logMutex;

    // File logging
    bool logToFile;
    std::ofstream logFile;
    bool useColorOutput;
};

// Convenience macros for logging
#define LOG_DEBUG(component, message) Logger::getInstance().debug(component, message)
#define LOG_INFO(component, message) Logger::getInstance().info(component, message)
#define LOG_WARNING(component, message) Logger::getInstance().warning(component, message)
#define LOG_ERROR(component, message) Logger::getInstance().error(component, message)

// Stream-style logging
#define LOG_DEBUG_STREAM(component, message)               \
    {                                                      \
        std::ostringstream oss;                            \
        oss << message;                                    \
        Logger::getInstance().debug(component, oss.str()); \
    }
#define LOG_INFO_STREAM(component, message)               \
    {                                                     \
        std::ostringstream oss;                           \
        oss << message;                                   \
        Logger::getInstance().info(component, oss.str()); \
    }
#define LOG_WARNING_STREAM(component, message)               \
    {                                                        \
        std::ostringstream oss;                              \
        oss << message;                                      \
        Logger::getInstance().warning(component, oss.str()); \
    }
#define LOG_ERROR_STREAM(component, message)               \
    {                                                      \
        std::ostringstream oss;                            \
        oss << message;                                    \
        Logger::getInstance().error(component, oss.str()); \
    }