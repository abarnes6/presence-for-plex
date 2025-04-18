#pragma once
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>

enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error,
    None
};

class Logger
{
public:
    static Logger &getInstance();
    void setLogLevel(LogLevel level);
    LogLevel getLogLevel() const;
    void debug(const std::string &component, const std::string &message);
    void info(const std::string &component, const std::string &message);
    void warning(const std::string &component, const std::string &message);
    void error(const std::string &component, const std::string &message);

private:
    Logger() : logLevel(LogLevel::Info) {}
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    void log(const std::string &component, const std::string &level, const std::string &message);
    LogLevel logLevel;
    std::mutex logMutex;
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