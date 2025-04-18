#include "logger.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <fstream>

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

// Log a debug message
void Logger::debug(const std::string &component, const std::string &message)
{
    if (logLevel <= LogLevel::Debug)
    {
        log(component, "DEBUG", message);
    }
}

// Log an info message
void Logger::info(const std::string &component, const std::string &message)
{
    if (logLevel <= LogLevel::Info)
    {
        log(component, "INFO", message);
    }
}

// Log a warning message
void Logger::warning(const std::string &component, const std::string &message)
{
    if (logLevel <= LogLevel::Warning)
    {
        log(component, "WARNING", message);
    }
}

// Log an error message
void Logger::error(const std::string &component, const std::string &message)
{
    if (logLevel <= LogLevel::Error)
    {
        log(component, "ERROR", message);
    }
}

// Internal logging implementation
void Logger::log(const std::string &component, const std::string &level, const std::string &message)
{
    std::lock_guard<std::mutex> lock(logMutex);

    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;

#ifdef _WIN32
    localtime_s(&now_tm, &now_c);
#else
    now_tm = *localtime(&now_c);
#endif

    std::cout << "[" << std::put_time(&now_tm, "%H:%M:%S") << "] "
              << "[" << component << "] "
              << "[" << level << "] "
              << message << std::endl;

    // If log file was opened, write to it too
    // if (logFile.is_open()) {
    //     logFile << "[" << std::put_time(&now_tm, "%H:%M:%S") << "] "
    //           << "[" << component << "] "
    //           << "[" << level << "] "
    //           << message << std::endl;
    // }
}