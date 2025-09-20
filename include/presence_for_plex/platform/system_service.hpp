#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <expected>

namespace presence_for_plex {
namespace platform {

// System error types
enum class SystemError {
    NotSupported,
    PermissionDenied,
    ResourceNotFound,
    OperationFailed,
    AlreadyExists
};

// Process information
struct ProcessInfo {
    std::uint32_t pid;
    std::string name;
    std::string path;
    std::string command_line;
    std::chrono::system_clock::time_point start_time;
    std::uint64_t memory_usage; // in bytes
    double cpu_usage_percent;
    std::uint32_t parent_pid;

    bool is_valid() const { return pid != 0; }
};

// File system utilities
class FileSystem {
public:
    virtual ~FileSystem() = default;

    // Path utilities
    virtual std::filesystem::path get_executable_path() const = 0;
    virtual std::filesystem::path get_executable_directory() const = 0;
    virtual std::filesystem::path get_user_data_directory() const = 0;
    virtual std::filesystem::path get_user_config_directory() const = 0;
    virtual std::filesystem::path get_user_cache_directory() const = 0;
    virtual std::filesystem::path get_temp_directory() const = 0;

    // Application-specific paths
    virtual std::filesystem::path get_app_data_directory(const std::string& app_name) const = 0;
    virtual std::filesystem::path get_app_config_directory(const std::string& app_name) const = 0;
    virtual std::filesystem::path get_app_cache_directory(const std::string& app_name) const = 0;
    virtual std::filesystem::path get_app_log_directory(const std::string& app_name) const = 0;

    // File operations
    virtual std::expected<bool, SystemError> ensure_directory_exists(const std::filesystem::path& path) = 0;
    virtual std::expected<void, SystemError> copy_file(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
    virtual std::expected<void, SystemError> move_file(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
    virtual std::expected<void, SystemError> delete_file(const std::filesystem::path& path) = 0;

    // Permissions
    virtual std::expected<bool, SystemError> is_readable(const std::filesystem::path& path) const = 0;
    virtual std::expected<bool, SystemError> is_writable(const std::filesystem::path& path) const = 0;
    virtual std::expected<bool, SystemError> is_executable(const std::filesystem::path& path) const = 0;

    // File watching
    using FileChangeCallback = std::function<void(const std::filesystem::path&, bool /*is_directory*/)>;

    virtual std::expected<int, SystemError> watch_file(const std::filesystem::path& path, FileChangeCallback callback) = 0;
    virtual std::expected<int, SystemError> watch_directory(const std::filesystem::path& path, FileChangeCallback callback, bool recursive = false) = 0;
    virtual std::expected<void, SystemError> unwatch(int watch_id) = 0;
};

// Process management
class ProcessManager {
public:
    virtual ~ProcessManager() = default;

    // Process discovery
    virtual std::expected<std::vector<ProcessInfo>, SystemError> get_all_processes() const = 0;
    virtual std::expected<ProcessInfo, SystemError> get_process_info(std::uint32_t pid) const = 0;
    virtual std::expected<std::vector<ProcessInfo>, SystemError> find_processes_by_name(const std::string& name) const = 0;
    virtual std::expected<ProcessInfo, SystemError> get_current_process_info() const = 0;

    // Process operations
    virtual std::expected<std::uint32_t, SystemError> start_process(
        const std::string& executable,
        const std::vector<std::string>& arguments = {},
        const std::filesystem::path& working_directory = {},
        bool detached = false
    ) = 0;

    virtual std::expected<void, SystemError> terminate_process(std::uint32_t pid, bool force = false) = 0;
    virtual std::expected<bool, SystemError> is_process_running(std::uint32_t pid) const = 0;
    virtual std::expected<int, SystemError> wait_for_process(std::uint32_t pid, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) = 0;

    // Process monitoring
    using ProcessCallback = std::function<void(const ProcessInfo&, bool /*started*/)>;

    virtual std::expected<int, SystemError> monitor_process(const std::string& name, ProcessCallback callback) = 0;
    virtual std::expected<void, SystemError> stop_monitoring(int monitor_id) = 0;
};

// System information
class SystemInfo {
public:
    virtual ~SystemInfo() = default;

    enum class Platform {
        Windows,
        macOS,
        Linux,
        Unknown
    };

    enum class Architecture {
        x86,
        x64,
        ARM,
        ARM64,
        Unknown
    };

    // Platform information
    virtual Platform get_platform() const = 0;
    virtual Architecture get_architecture() const = 0;
    virtual std::string get_platform_name() const = 0;
    virtual std::string get_platform_version() const = 0;
    virtual std::string get_kernel_version() const = 0;

    // Hardware information
    virtual std::uint32_t get_cpu_count() const = 0;
    virtual std::string get_cpu_name() const = 0;
    virtual std::uint64_t get_total_memory() const = 0; // in bytes
    virtual std::uint64_t get_available_memory() const = 0; // in bytes

    // Runtime information
    virtual std::chrono::system_clock::time_point get_boot_time() const = 0;
    virtual std::chrono::milliseconds get_uptime() const = 0;
    virtual double get_cpu_usage_percent() const = 0;
    virtual double get_memory_usage_percent() const = 0;

    // User information
    virtual std::string get_username() const = 0;
    virtual std::string get_hostname() const = 0;
    virtual bool is_admin() const = 0;
};

// Power management
class PowerManager {
public:
    virtual ~PowerManager() = default;

    enum class PowerState {
        Active,
        Idle,
        Sleep,
        Hibernate,
        Shutdown,
        Unknown
    };

    // Power state
    virtual PowerState get_power_state() const = 0;
    virtual std::chrono::seconds get_idle_time() const = 0;

    // Power operations
    virtual std::expected<void, SystemError> prevent_sleep(bool prevent = true) = 0;
    virtual std::expected<void, SystemError> request_sleep() = 0;
    virtual std::expected<void, SystemError> request_hibernate() = 0;
    virtual std::expected<void, SystemError> request_shutdown(bool force = false) = 0;
    virtual std::expected<void, SystemError> request_restart(bool force = false) = 0;

    // Power event callbacks
    using PowerCallback = std::function<void(PowerState)>;

    virtual std::expected<int, SystemError> register_power_callback(PowerCallback callback) = 0;
    virtual std::expected<void, SystemError> unregister_power_callback(int callback_id) = 0;
};

// Registry/preferences management (Windows registry, macOS preferences, Linux config files)
class RegistryManager {
public:
    virtual ~RegistryManager() = default;

    enum class ValueType {
        String,
        Integer,
        Boolean,
        Binary
    };

    // Value operations
    virtual std::expected<std::string, SystemError> read_string(const std::string& key, const std::string& value_name) const = 0;
    virtual std::expected<std::int64_t, SystemError> read_integer(const std::string& key, const std::string& value_name) const = 0;
    virtual std::expected<bool, SystemError> read_boolean(const std::string& key, const std::string& value_name) const = 0;
    virtual std::expected<std::vector<std::uint8_t>, SystemError> read_binary(const std::string& key, const std::string& value_name) const = 0;

    virtual std::expected<void, SystemError> write_string(const std::string& key, const std::string& value_name, const std::string& value) = 0;
    virtual std::expected<void, SystemError> write_integer(const std::string& key, const std::string& value_name, std::int64_t value) = 0;
    virtual std::expected<void, SystemError> write_boolean(const std::string& key, const std::string& value_name, bool value) = 0;
    virtual std::expected<void, SystemError> write_binary(const std::string& key, const std::string& value_name, const std::vector<std::uint8_t>& value) = 0;

    // Key operations
    virtual std::expected<bool, SystemError> key_exists(const std::string& key) const = 0;
    virtual std::expected<bool, SystemError> value_exists(const std::string& key, const std::string& value_name) const = 0;
    virtual std::expected<void, SystemError> create_key(const std::string& key) = 0;
    virtual std::expected<void, SystemError> delete_key(const std::string& key, bool recursive = false) = 0;
    virtual std::expected<void, SystemError> delete_value(const std::string& key, const std::string& value_name) = 0;

    // Enumeration
    virtual std::expected<std::vector<std::string>, SystemError> enumerate_keys(const std::string& key) const = 0;
    virtual std::expected<std::vector<std::string>, SystemError> enumerate_values(const std::string& key) const = 0;
};

// Single instance management
class SingleInstanceManager {
public:
    virtual ~SingleInstanceManager() = default;

    // Instance management
    virtual std::expected<bool, SystemError> try_acquire_instance(const std::string& instance_name) = 0;
    virtual void release_instance() = 0;
    virtual bool is_instance_acquired() const = 0;

    // Communication with other instances
    using MessageCallback = std::function<void(const std::string&)>;

    // Factory method
    static std::unique_ptr<SingleInstanceManager> create(const std::string& instance_name);
};

// Main system service interface
class SystemService {
public:
    virtual ~SystemService() = default;

    // Lifecycle
    virtual std::expected<void, SystemError> initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool is_initialized() const = 0;

    // Component access
    virtual std::unique_ptr<FileSystem> create_file_system() = 0;
    virtual std::unique_ptr<ProcessManager> create_process_manager() = 0;
    virtual std::unique_ptr<SystemInfo> create_system_info() = 0;
    virtual std::unique_ptr<PowerManager> create_power_manager() = 0;
    virtual std::unique_ptr<RegistryManager> create_registry_manager() = 0;
    virtual std::unique_ptr<SingleInstanceManager> create_single_instance_manager(const std::string& name) = 0;

    // Platform capabilities
    virtual bool supports_file_watching() const = 0;
    virtual bool supports_process_monitoring() const = 0;
    virtual bool supports_power_management() const = 0;
    virtual bool supports_registry() const = 0;
};

// Factory for creating platform-specific system services
class SystemServiceFactory {
public:
    virtual ~SystemServiceFactory() = default;

    enum class PlatformType {
        Windows,
        macOS,
        Linux,
        Auto // Detect automatically
    };

    virtual std::unique_ptr<SystemService> create_service(PlatformType type = PlatformType::Auto) = 0;

    static std::unique_ptr<SystemServiceFactory> create_default_factory();
};

} // namespace platform
} // namespace presence_for_plex