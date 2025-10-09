#include "presence_for_plex/platform/system_service.hpp"
#include "presence_for_plex/utils/logger.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef USE_QT_UI
#include <QLockFile>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>
#else
#ifndef _WIN32
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#endif

#include <filesystem>
#include <fstream>

namespace presence_for_plex {
namespace platform {

class SingleInstanceManagerImpl : public SingleInstanceManager {
public:
    explicit SingleInstanceManagerImpl(const std::string& instance_name)
        : m_instance_name(instance_name)
        , m_acquired(false)
#ifndef USE_QT_UI
#ifdef _WIN32
        , m_mutex_handle(nullptr)
#else
        , m_lock_fd(-1)
#endif
#endif
    {
        LOG_DEBUG("SingleInstance", "Creating single instance manager for: " + instance_name);

#ifdef USE_QT_UI
        // Create lock file path in system temp directory
        QString temp_path = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QDir temp_dir(temp_path);
        QString lock_file_name = QString::fromStdString(instance_name) + ".lock";
        m_lock_file_path = temp_dir.absoluteFilePath(lock_file_name).toStdString();

        m_lock_file = std::make_unique<QLockFile>(QString::fromStdString(m_lock_file_path));
        m_lock_file->setStaleLockTime(0); // Don't consider the lock stale
#endif
    }

    ~SingleInstanceManagerImpl() {
        release_instance();
    }

    std::expected<bool, SystemError> try_acquire_instance(const std::string& instance_name) override {
        if (m_acquired) {
            return true; // Already acquired
        }

        LOG_INFO("SingleInstance", "Attempting to acquire instance: " + instance_name);

#ifdef USE_QT_UI
        // Qt implementation using QLockFile
        m_acquired = m_lock_file->tryLock(0);

        if (m_acquired) {
            LOG_INFO("SingleInstance", "Successfully acquired instance");
        } else {
            if (m_lock_file->error() == QLockFile::LockFailedError) {
                LOG_INFO("SingleInstance", "Another instance is already running");
            } else {
                LOG_ERROR("SingleInstance", "Failed to acquire lock: " +
                              std::to_string(static_cast<int>(m_lock_file->error())));
                return std::unexpected<SystemError>(SystemError::OperationFailed);
            }
        }
#else
#ifdef _WIN32
        // Windows implementation using named mutex
        std::string mutex_name = "Global\\" + instance_name + "_SingleInstance_Mutex";
        m_mutex_handle = CreateMutexA(nullptr, TRUE, mutex_name.c_str());

        if (m_mutex_handle != nullptr) {
            m_acquired = (GetLastError() != ERROR_ALREADY_EXISTS);
            if (!m_acquired) {
                ReleaseMutex(m_mutex_handle);
                CloseHandle(m_mutex_handle);
                m_mutex_handle = nullptr;
            }
        } else {
            LOG_ERROR("SingleInstance", "Failed to create mutex");
            return std::unexpected<SystemError>(SystemError::OperationFailed);
        }
#else
        // Unix/Linux/macOS implementation using file locking
        const char* tmp_dir = getenv("TMPDIR");
        if (tmp_dir == nullptr) {
            tmp_dir = "/tmp";
        }

        m_lock_file_path = std::string(tmp_dir) + "/" + instance_name + ".lock";
        m_lock_fd = open(m_lock_file_path.c_str(), O_RDWR | O_CREAT, 0666);

        if (m_lock_fd != -1) {
            int lock_result = flock(m_lock_fd, LOCK_EX | LOCK_NB);
            m_acquired = (lock_result != -1);

            if (!m_acquired) {
                close(m_lock_fd);
                m_lock_fd = -1;
            }
        } else {
            LOG_ERROR("SingleInstance", "Failed to create lock file");
            return std::unexpected<SystemError>(SystemError::OperationFailed);
        }
#endif
#endif

        if (m_acquired) {
            LOG_INFO("SingleInstance", "Successfully acquired instance");
        } else {
            LOG_INFO("SingleInstance", "Another instance is already running");
        }

        return m_acquired;
    }

    void release_instance() override {
        if (!m_acquired) {
            return;
        }

        LOG_INFO("SingleInstance", "Releasing instance");

#ifdef USE_QT_UI
        if (m_lock_file) {
            m_lock_file->unlock();
        }
#else
#ifdef _WIN32
        if (m_mutex_handle != nullptr) {
            ReleaseMutex(m_mutex_handle);
            CloseHandle(m_mutex_handle);
            m_mutex_handle = nullptr;
        }
#else
        if (m_lock_fd != -1) {
            flock(m_lock_fd, LOCK_UN);
            close(m_lock_fd);
            m_lock_fd = -1;
            unlink(m_lock_file_path.c_str());
        }
#endif
#endif

        m_acquired = false;
    }

    bool is_instance_acquired() const override {
        return m_acquired;
    }

private:
    std::string m_instance_name;
    bool m_acquired;
    std::string m_lock_file_path;

#ifdef USE_QT_UI
    std::unique_ptr<QLockFile> m_lock_file;
#else
#ifdef _WIN32
    HANDLE m_mutex_handle;
#else
    int m_lock_fd;
#endif
#endif
};

std::unique_ptr<SingleInstanceManager> SingleInstanceManager::create(const std::string& instance_name) {
    return std::make_unique<SingleInstanceManagerImpl>(instance_name);
}

class AutostartManagerImpl : public AutostartManager {
public:
    explicit AutostartManagerImpl(const std::string& app_name)
        : m_app_name(app_name) {
        LOG_DEBUG("Autostart", "Creating autostart manager for: " + app_name);
    }

    std::expected<void, SystemError> enable_autostart() override {
        LOG_INFO("Autostart", "Enabling autostart");

#ifdef _WIN32
        // Windows: Add registry key in HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run
        HKEY hkey;
        const std::string registry_path = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

        if (RegOpenKeyExA(HKEY_CURRENT_USER, registry_path.c_str(), 0, KEY_WRITE, &hkey) != ERROR_SUCCESS) {
            LOG_ERROR("Autostart", "Failed to open registry key");
            return std::unexpected(SystemError::OperationFailed);
        }

        char exe_path[MAX_PATH];
        if (GetModuleFileNameA(nullptr, exe_path, MAX_PATH) == 0) {
            RegCloseKey(hkey);
            LOG_ERROR("Autostart", "Failed to get executable path");
            return std::unexpected(SystemError::OperationFailed);
        }

        if (RegSetValueExA(hkey, m_app_name.c_str(), 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(exe_path),
                          static_cast<DWORD>(strlen(exe_path) + 1)) != ERROR_SUCCESS) {
            RegCloseKey(hkey);
            LOG_ERROR("Autostart", "Failed to set registry value");
            return std::unexpected(SystemError::OperationFailed);
        }

        RegCloseKey(hkey);
        LOG_INFO("Autostart", "Autostart enabled successfully");
        return {};

#elif defined(__linux__)
        // Linux: Create .desktop file in ~/.config/autostart/
        const char* home = std::getenv("HOME");
        const char* xdg_config = std::getenv("XDG_CONFIG_HOME");

        std::filesystem::path autostart_dir;
        if (xdg_config) {
            autostart_dir = std::filesystem::path(xdg_config) / "autostart";
        } else if (home) {
            autostart_dir = std::filesystem::path(home) / ".config" / "autostart";
        } else {
            LOG_ERROR("Autostart", "Could not determine config directory");
            return std::unexpected(SystemError::OperationFailed);
        }

        try {
            std::filesystem::create_directories(autostart_dir);
        } catch (const std::exception& e) {
            LOG_ERROR("Autostart", "Failed to create autostart directory: " + std::string(e.what()));
            return std::unexpected(SystemError::OperationFailed);
        }

        std::filesystem::path desktop_file = autostart_dir / "presence-for-plex.desktop";

        // Get executable path
        char exe_path[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len == -1) {
            LOG_ERROR("Autostart", "Failed to get executable path");
            return std::unexpected(SystemError::OperationFailed);
        }
        exe_path[len] = '\0';

        std::ofstream file(desktop_file);
        if (!file.is_open()) {
            LOG_ERROR("Autostart", "Failed to create desktop file");
            return std::unexpected(SystemError::OperationFailed);
        }

        file << "[Desktop Entry]\n"
             << "Type=Application\n"
             << "Name=" << m_app_name << "\n"
             << "Exec=" << exe_path << "\n"
             << "Hidden=false\n"
             << "NoDisplay=false\n"
             << "X-GNOME-Autostart-enabled=true\n";

        file.close();
        LOG_INFO("Autostart", "Autostart enabled successfully");
        return {};

#else
        LOG_WARNING("Autostart", "Autostart not supported on this platform");
        return std::unexpected(SystemError::NotSupported);
#endif
    }

    std::expected<void, SystemError> disable_autostart() override {
        LOG_INFO("Autostart", "Disabling autostart");

#ifdef _WIN32
        // Windows: Remove registry key
        HKEY hkey;
        const std::string registry_path = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

        if (RegOpenKeyExA(HKEY_CURRENT_USER, registry_path.c_str(), 0, KEY_WRITE, &hkey) != ERROR_SUCCESS) {
            LOG_ERROR("Autostart", "Failed to open registry key");
            return std::unexpected(SystemError::OperationFailed);
        }

        LONG result = RegDeleteValueA(hkey, m_app_name.c_str());
        RegCloseKey(hkey);

        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
            LOG_ERROR("Autostart", "Failed to delete registry value");
            return std::unexpected(SystemError::OperationFailed);
        }

        LOG_INFO("Autostart", "Autostart disabled successfully");
        return {};

#elif defined(__linux__)
        // Linux: Remove .desktop file
        const char* home = std::getenv("HOME");
        const char* xdg_config = std::getenv("XDG_CONFIG_HOME");

        std::filesystem::path autostart_dir;
        if (xdg_config) {
            autostart_dir = std::filesystem::path(xdg_config) / "autostart";
        } else if (home) {
            autostart_dir = std::filesystem::path(home) / ".config" / "autostart";
        } else {
            LOG_ERROR("Autostart", "Could not determine config directory");
            return std::unexpected(SystemError::OperationFailed);
        }

        std::filesystem::path desktop_file = autostart_dir / "presence-for-plex.desktop";

        try {
            if (std::filesystem::exists(desktop_file)) {
                std::filesystem::remove(desktop_file);
                LOG_INFO("Autostart", "Autostart disabled successfully");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Autostart", "Failed to remove desktop file: " + std::string(e.what()));
            return std::unexpected(SystemError::OperationFailed);
        }

        return {};

#else
        LOG_WARNING("Autostart", "Autostart not supported on this platform");
        return std::unexpected(SystemError::NotSupported);
#endif
    }

    std::expected<bool, SystemError> is_autostart_enabled() override {
#ifdef _WIN32
        // Windows: Check registry key
        HKEY hkey;
        const std::string registry_path = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

        if (RegOpenKeyExA(HKEY_CURRENT_USER, registry_path.c_str(), 0, KEY_READ, &hkey) != ERROR_SUCCESS) {
            return false; // Key doesn't exist, so autostart is not enabled
        }

        char value[MAX_PATH];
        DWORD value_size = sizeof(value);
        LONG result = RegQueryValueExA(hkey, m_app_name.c_str(), nullptr, nullptr,
                                       reinterpret_cast<LPBYTE>(value), &value_size);
        RegCloseKey(hkey);

        return result == ERROR_SUCCESS;

#elif defined(__linux__)
        // Linux: Check for .desktop file
        const char* home = std::getenv("HOME");
        const char* xdg_config = std::getenv("XDG_CONFIG_HOME");

        std::filesystem::path autostart_dir;
        if (xdg_config) {
            autostart_dir = std::filesystem::path(xdg_config) / "autostart";
        } else if (home) {
            autostart_dir = std::filesystem::path(home) / ".config" / "autostart";
        } else {
            return false;
        }

        std::filesystem::path desktop_file = autostart_dir / "presence-for-plex.desktop";
        return std::filesystem::exists(desktop_file);

#else
        return std::unexpected(SystemError::NotSupported);
#endif
    }

private:
    std::string m_app_name;
};

std::unique_ptr<AutostartManager> AutostartManager::create(const std::string& app_name) {
    return std::make_unique<AutostartManagerImpl>(app_name);
}

} // namespace platform
} // namespace presence_for_plex
