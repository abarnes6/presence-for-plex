#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
// #include <source_location> // C++20 feature, commented out for compatibility

namespace presence_for_plex::utils {

enum class LogLevel : std::uint8_t {
  Debug = 0,
  Info = 1,
  Warning = 2,
  Error = 3,
  None = 4
};

// Simplified location info for compatibility
struct SourceLocation {
  [[nodiscard]] const char* file_name() const { return m_filename; }
  [[nodiscard]] int line() const { return m_line_number; }

  const char* m_filename = "unknown";
  int m_line_number = 0;
};

struct LogMessage {
  LogLevel m_level;
  std::chrono::system_clock::time_point m_timestamp;
  std::string m_component;
  std::string m_message;
  SourceLocation m_location;
};

class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual void write(const LogMessage& message) = 0;
  virtual void flush() = 0;
};

class Logger {
 public:
  using SinkPtr = std::unique_ptr<LogSink>;

  explicit Logger(LogLevel min_level = LogLevel::Info);
  ~Logger() = default;

  // Move-only semantics
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;
  Logger(Logger&&) = delete;
  Logger& operator=(Logger&&) = delete;

  // Configuration
  void set_level(LogLevel level);
  [[nodiscard]] LogLevel get_level() const;
  void add_sink(SinkPtr sink);
  void clear_sinks();

  // Logging methods
  void debug(std::string_view component, std::string_view message,
             SourceLocation location = {});
  void info(std::string_view component, std::string_view message,
            SourceLocation location = {});
  void warning(std::string_view component, std::string_view message,
               SourceLocation location = {});
  void error(std::string_view component, std::string_view message,
             SourceLocation location = {});


  // Flush all sinks
  void flush();

 private:
  void log(LogLevel level, std::string_view component, std::string_view message,
           SourceLocation location);

  LogLevel m_min_level;
  std::vector<SinkPtr> m_sinks;
  mutable std::mutex m_mutex;
};

// Sink implementations
class ConsoleSink : public LogSink {
 public:
  explicit ConsoleSink(bool use_colors = true);
  void write(const LogMessage& message) override;
  void flush() override;

 private:
  bool m_use_colors;
  [[nodiscard]] std::string colorize(std::string_view text,
                                     LogLevel level) const;
  [[nodiscard]] std::string format_message(const LogMessage& message) const;
};

class FileSink : public LogSink {
 public:
  explicit FileSink(const std::filesystem::path& path, bool truncate = false);
  ~FileSink() override;

  void write(const LogMessage& message) override;
  void flush() override;
  [[nodiscard]] bool is_open() const;

 private:
  std::ofstream m_file;
  [[nodiscard]] std::string format_message(const LogMessage& message) const;
};

// Global logger instance (dependency injection friendly)
class LoggerManager {
 public:
  static Logger& get_instance();
  static void set_instance(std::unique_ptr<Logger> logger);
  static std::unique_ptr<Logger> create_default_logger();

 private:
  static std::unique_ptr<Logger> s_logger;
  static std::mutex s_init_mutex;
};

// Utility functions
inline std::string to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error: return "error";
        case LogLevel::None: return "none";
        default: return "info";
    }
}

inline LogLevel log_level_from_string(const std::string& str) {
    if (str == "debug") return LogLevel::Debug;
    if (str == "info") return LogLevel::Info;
    if (str == "warning" || str == "warn") return LogLevel::Warning;
    if (str == "error") return LogLevel::Error;
    if (str == "none") return LogLevel::None;
    return LogLevel::Info;
}

// Convenience macros
#define LOG_DEBUG(component, message)                                      \
  presence_for_plex::utils::LoggerManager::get_instance().debug(component, \
                                                                message)

#define LOG_INFO(component, message)                                       \
  presence_for_plex::utils::LoggerManager::get_instance().info(component,  \
                                                               message)

#define LOG_WARNING(component, message)                                    \
  presence_for_plex::utils::LoggerManager::get_instance().warning(component, \
                                                                  message)

#define LOG_ERROR(component, message)                                      \
  presence_for_plex::utils::LoggerManager::get_instance().error(component, \
                                                                message)

}  // namespace presence_for_plex::utils
