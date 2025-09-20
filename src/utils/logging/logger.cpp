#include "presence_for_plex/utils/logger.hpp"

#include <iomanip>
#include <iostream>
#include <mutex>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace presence_for_plex {
namespace utils {

// ANSI color codes
namespace {
    constexpr std::string_view ANSI_RESET = "\033[0m";
    constexpr std::string_view ANSI_RED = "\033[31m";
    constexpr std::string_view ANSI_YELLOW = "\033[33m";
    constexpr std::string_view ANSI_GREEN = "\033[32m";
    constexpr std::string_view ANSI_BLUE = "\033[36m";

    std::string format_timestamp(const std::chrono::system_clock::time_point& tp) {
        const auto time_t = std::chrono::system_clock::to_time_t(tp);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            tp.time_since_epoch()) % 1000;

        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << tm_buf.tm_hour << ":"
            << std::setw(2) << tm_buf.tm_min << ":"
            << std::setw(2) << tm_buf.tm_sec << "."
            << std::setw(3) << ms.count();
        return oss.str();
    }

    std::string_view level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info: return "INFO";
            case LogLevel::Warning: return "WARN";
            case LogLevel::Error: return "ERROR";
            default: return "UNKNOWN";
        }
    }

#ifdef _WIN32
    bool enable_ansi_colors() {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut == INVALID_HANDLE_VALUE) return false;

        DWORD dwMode = 0;
        if (!GetConsoleMode(hOut, &dwMode)) return false;

        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        return SetConsoleMode(hOut, dwMode) != 0;
    }
#endif

    bool is_terminal() {
#ifdef _WIN32
        return _isatty(_fileno(stdout));
#else
        return isatty(fileno(stdout));
#endif
    }
}

// Logger implementation
Logger::Logger(LogLevel min_level) : m_min_level(min_level) {}

void Logger::set_level(LogLevel level) {
    std::lock_guard lock(m_mutex);
    m_min_level = level;
}

LogLevel Logger::get_level() const {
    std::lock_guard lock(m_mutex);
    return m_min_level;
}

void Logger::add_sink(SinkPtr sink) {
    std::lock_guard lock(m_mutex);
    m_sinks.push_back(std::move(sink));
}

void Logger::clear_sinks() {
    std::lock_guard lock(m_mutex);
    m_sinks.clear();
}

void Logger::debug(std::string_view component, std::string_view message,
                   SourceLocation location) {
    log(LogLevel::Debug, component, message, location);
}

void Logger::info(std::string_view component, std::string_view message,
                  SourceLocation location) {
    log(LogLevel::Info, component, message, location);
}

void Logger::warning(std::string_view component, std::string_view message,
                     SourceLocation location) {
    log(LogLevel::Warning, component, message, location);
}

void Logger::error(std::string_view component, std::string_view message,
                   SourceLocation location) {
    log(LogLevel::Error, component, message, location);
}

void Logger::debug_f(std::string_view component, const std::string& message) {
    debug(component, message);
}

void Logger::info_f(std::string_view component, const std::string& message) {
    info(component, message);
}

void Logger::warning_f(std::string_view component, const std::string& message) {
    warning(component, message);
}

void Logger::error_f(std::string_view component, const std::string& message) {
    error(component, message);
}

// Template method implementations moved to header

void Logger::flush() {
    std::lock_guard lock(m_mutex);
    for (auto& sink : m_sinks) {
        sink->flush();
    }
}

void Logger::log(LogLevel level, std::string_view component, std::string_view message,
                 SourceLocation location) {
    if (level < m_min_level) return;

    LogMessage log_msg{
        .m_level = level,
        .m_timestamp = std::chrono::system_clock::now(),
        .m_component = std::string(component),
        .m_message = std::string(message),
        .m_location = location
    };

    std::lock_guard lock(m_mutex);
    for (auto& sink : m_sinks) {
        sink->write(log_msg);
    }
}

// ConsoleSink implementation
ConsoleSink::ConsoleSink(bool use_colors) : m_use_colors(use_colors && is_terminal()) {
#ifdef _WIN32
    if (m_use_colors) {
        m_use_colors = enable_ansi_colors();
    }
#endif
}

void ConsoleSink::write(const LogMessage& message) {
    auto formatted = format_message(message);
    std::cout << colorize(formatted, message.m_level) << '\n';
}

void ConsoleSink::flush() {
    std::cout.flush();
}

std::string ConsoleSink::colorize(std::string_view text, LogLevel level) const {
    if (!m_use_colors) return std::string(text);

    std::string_view color;
    switch (level) {
        case LogLevel::Debug: color = ANSI_BLUE; break;
        case LogLevel::Info: color = ANSI_GREEN; break;
        case LogLevel::Warning: color = ANSI_YELLOW; break;
        case LogLevel::Error: color = ANSI_RED; break;
        default: return std::string(text);
    }

    std::ostringstream oss;
    oss << color << text << ANSI_RESET;
    return oss.str();
}

std::string ConsoleSink::format_message(const LogMessage& message) const {
    std::ostringstream oss;
    oss << "[" << format_timestamp(message.m_timestamp) << "] "
        << "[" << level_to_string(message.m_level) << "] "
        << "[" << message.m_component << "] "
        << message.m_message;
    return oss.str();
}

// FileSink implementation
FileSink::FileSink(const std::filesystem::path& path, bool truncate) {
    if (!std::filesystem::exists(path.parent_path())) {
        std::filesystem::create_directories(path.parent_path());
    }

    auto mode = std::ios::out;
    if (truncate) {
        mode |= std::ios::trunc;
    } else {
        mode |= std::ios::app;
    }

    m_file.open(path, mode);
    if (m_file.is_open() && !truncate) {
        // Add session separator for append mode
        const auto now = std::chrono::system_clock::now();
        const auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        m_file << "\n=== Log session started at "
               << std::setfill('0')
               << std::setw(4) << (tm_buf.tm_year + 1900) << "-"
               << std::setw(2) << (tm_buf.tm_mon + 1) << "-"
               << std::setw(2) << tm_buf.tm_mday << " "
               << std::setw(2) << tm_buf.tm_hour << ":"
               << std::setw(2) << tm_buf.tm_min << ":"
               << std::setw(2) << tm_buf.tm_sec << " ===\n";
        m_file.flush();
    }
}

FileSink::~FileSink() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

void FileSink::write(const LogMessage& message) {
    if (m_file.is_open()) {
        m_file << format_message(message) << '\n';
    }
}

void FileSink::flush() {
    if (m_file.is_open()) {
        m_file.flush();
    }
}

bool FileSink::is_open() const {
    return m_file.is_open();
}

std::string FileSink::format_message(const LogMessage& message) const {
    std::ostringstream oss;
    oss << "[" << format_timestamp(message.m_timestamp) << "] "
        << "[" << level_to_string(message.m_level) << "] "
        << "[" << message.m_component << "] "
        << message.m_message << " ("
        << message.m_location.file_name() << ":"
        << message.m_location.line() << ")";
    return oss.str();
}

// LoggerManager implementation
std::unique_ptr<Logger> LoggerManager::s_logger;
std::mutex LoggerManager::s_init_mutex;

Logger& LoggerManager::get_instance() {
    std::lock_guard<std::mutex> lock(s_init_mutex);
    if (!s_logger) {
        s_logger = create_default_logger();
    }
    return *s_logger;
}

void LoggerManager::set_instance(std::unique_ptr<Logger> logger) {
    s_logger = std::move(logger);
}

std::unique_ptr<Logger> LoggerManager::create_default_logger() {
    auto logger = std::make_unique<Logger>(LogLevel::Info);
    logger->add_sink(std::make_unique<ConsoleSink>(true));
    return logger;
}

} // namespace utils
} // namespace presence_for_plex