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

// Initialize file logging
void Logger::initFileLogging(const std::filesystem::path& logFilePath, bool clearExisting)
{
    std::lock_guard<std::mutex> lock(logMutex);

    logFile = std::ofstream(logFilePath / "logs.txt", std::ios::out | std::ios::app);
    
    // Close the file if it's already open
    if (logFile.is_open()) {
        logFile.close();
    }
    
    // Create the directory if it doesn't exist
    if (!std::filesystem::exists(logFilePath.parent_path())) {
        std::filesystem::create_directories(logFilePath.parent_path());
    }
    
    // Open the log file with appropriate mode
    auto openMode = std::ios::out;
    if (clearExisting) {
        openMode |= std::ios::trunc;  // Clear the file
    } else {
        openMode |= std::ios::app;    // Append to the file
    }
    
    logFile.open(logFilePath, openMode);
    
    if (logFile.is_open()) {
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
    } else {
        std::cerr << "Failed to open log file: " << logFilePath.string() << std::endl;
        logToFile = false;
    }
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

    // Format the log message
    std::stringstream formatted;
    formatted << "[" << std::put_time(&now_tm, "%H:%M:%S") << "] "
              << "[" << component << "] "
              << "[" << level << "] "
              << message;
    
    // Still print to console
#ifndef _WIN32
    std::cout << formatted.str() << std::endl;
#endif

    // Write to log file if enabled
    if (logToFile && logFile.is_open()) {
        logFile << formatted.str() << std::endl;
        logFile.flush(); // Ensure logs are written immediately
    }
}