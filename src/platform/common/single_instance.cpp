#include "presence_for_plex/platform/system_service.hpp"
#include "presence_for_plex/utils/logger.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace presence_for_plex {
namespace platform {

class SingleInstanceManagerImpl : public SingleInstanceManager {
public:
    explicit SingleInstanceManagerImpl(const std::string& instance_name)
        : m_instance_name(instance_name)
        , m_acquired(false)
#ifdef _WIN32
        , m_mutex_handle(nullptr)
#else
        , m_lock_fd(-1)
#endif
    {
        PLEX_LOG_DEBUG("SingleInstance", "Creating single instance manager for: " + instance_name);
    }

    ~SingleInstanceManagerImpl() {
        release_instance();
    }

    std::expected<bool, SystemError> try_acquire_instance(const std::string& instance_name) override {
        if (m_acquired) {
            return true; // Already acquired
        }

        PLEX_LOG_INFO("SingleInstance", "Attempting to acquire instance: " + instance_name);

#ifdef _WIN32
        // Windows implementation using named mutex
        std::string mutex_name = "Global\\" + instance_name + "_SingleInstance_Mutex";

        m_mutex_handle = CreateMutexA(nullptr, TRUE, mutex_name.c_str());

        if (m_mutex_handle != nullptr) {
            // Check if we got the mutex (if GetLastError returns ERROR_ALREADY_EXISTS, another instance exists)
            m_acquired = (GetLastError() != ERROR_ALREADY_EXISTS);

            if (!m_acquired) {
                // Release the mutex if we're not the first instance
                ReleaseMutex(m_mutex_handle);
                CloseHandle(m_mutex_handle);
                m_mutex_handle = nullptr;
            }
        } else {
            PLEX_LOG_ERROR("SingleInstance", "Failed to create mutex");
            return std::unexpected<SystemError>(SystemError::OperationFailed);
        }
#else
        // Unix/Linux/macOS implementation using file locking
        const char* tmp_dir = getenv("TMPDIR");
        if (tmp_dir == nullptr) {
            tmp_dir = "/tmp";
        }

        m_lock_file_path = std::string(tmp_dir) + "/" + instance_name + ".lock";

        // Open or create the lock file
        m_lock_fd = open(m_lock_file_path.c_str(), O_RDWR | O_CREAT, 0666);

        if (m_lock_fd != -1) {
            // Try to get an exclusive lock
            int lock_result = flock(m_lock_fd, LOCK_EX | LOCK_NB);
            m_acquired = (lock_result != -1);

            if (!m_acquired) {
                // Close the file if we couldn't get the lock
                close(m_lock_fd);
                m_lock_fd = -1;
            }
        } else {
            PLEX_LOG_ERROR("SingleInstance", "Failed to create lock file");
            return std::unexpected<SystemError>(SystemError::OperationFailed);
        }
#endif

        if (m_acquired) {
            PLEX_LOG_INFO("SingleInstance", "Successfully acquired instance");
        } else {
            PLEX_LOG_INFO("SingleInstance", "Another instance is already running");
        }

        return m_acquired;
    }

    void release_instance() override {
        if (!m_acquired) {
            return;
        }

        PLEX_LOG_INFO("SingleInstance", "Releasing instance");

#ifdef _WIN32
        if (m_mutex_handle != nullptr) {
            ReleaseMutex(m_mutex_handle);
            CloseHandle(m_mutex_handle);
            m_mutex_handle = nullptr;
        }
#else
        if (m_lock_fd != -1) {
            // Release the lock and close the file
            flock(m_lock_fd, LOCK_UN);
            close(m_lock_fd);
            m_lock_fd = -1;

            // Try to remove the lock file
            unlink(m_lock_file_path.c_str());
        }
#endif

        m_acquired = false;
    }

    bool is_instance_acquired() const override {
        return m_acquired;
    }

private:
    std::string m_instance_name;
    bool m_acquired;

#ifdef _WIN32
    HANDLE m_mutex_handle;
#else
    int m_lock_fd;
    std::string m_lock_file_path;
#endif
};

std::unique_ptr<SingleInstanceManager> SingleInstanceManager::create(const std::string& instance_name) {
    return std::make_unique<SingleInstanceManagerImpl>(instance_name);
}

} // namespace platform
} // namespace presence_for_plex
