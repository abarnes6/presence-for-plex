#include "single_instance.h"
#include "logger.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#endif

class SingleInstance::Impl {
public:
    explicit Impl(const std::string& appName);
    ~Impl();
    bool isFirstInstance() const;

private:
#ifdef _WIN32
    HANDLE mutexHandle;
#else
    int lockFileHandle;
    std::string lockFilePath;
#endif
    bool isFirst;
};

#ifdef _WIN32
// Windows implementation
SingleInstance::Impl::Impl(const std::string& appName) 
    : isFirst(false)
{
    // Create a named mutex
    std::string mutexName = "Global\\" + appName + "_SingleInstance_Mutex";
    
    // Try to create/open the mutex
    mutexHandle = CreateMutexA(NULL, TRUE, mutexName.c_str());
    
    // Check if we got the mutex
    if (mutexHandle != NULL) {
        // If GetLastError returns ERROR_ALREADY_EXISTS, another instance exists
        isFirst = (GetLastError() != ERROR_ALREADY_EXISTS);
        
        if (!isFirst) {
            // Release the mutex if we're not the first instance
            ReleaseMutex(mutexHandle);
        }
    }
    
    LOG_INFO("SingleInstance", isFirst ? 
        "Application instance is unique" : 
        "Another instance is already running");
}

SingleInstance::Impl::~Impl() {
    if (mutexHandle != NULL) {
        if (isFirst) {
            ReleaseMutex(mutexHandle);
        }
        CloseHandle(mutexHandle);
    }
}
#else
// Unix/Linux/MacOS implementation
SingleInstance::Impl::Impl(const std::string& appName) 
    : isFirst(false), lockFileHandle(-1)
{
    // Determine lock file location
    const char* tmpDir = getenv("TMPDIR");
    if (tmpDir == nullptr) {
        tmpDir = "/tmp";
    }
    
    lockFilePath = std::string(tmpDir) + "/" + appName + ".lock";
    
    // Open or create the lock file
    lockFileHandle = open(lockFilePath.c_str(), O_RDWR | O_CREAT, 0666);
    
    if (lockFileHandle != -1) {
        // Try to get an exclusive lock
        int lockResult = flock(lockFileHandle, LOCK_EX | LOCK_NB);
        isFirst = (lockResult != -1);
        
        if (!isFirst) {
            // Close the file if we couldn't get the lock
            close(lockFileHandle);
            lockFileHandle = -1;
        }
    }
    
    LOG_INFO("SingleInstance", isFirst ? 
        "Application instance is unique" : 
        "Another instance is already running");
}

SingleInstance::Impl::~Impl() {
    if (lockFileHandle != -1) {
        // Release the lock and close the file
        flock(lockFileHandle, LOCK_UN);
        close(lockFileHandle);
        
        // Try to remove the lock file
        unlink(lockFilePath.c_str());
    }
}
#endif

bool SingleInstance::Impl::isFirstInstance() const {
    return isFirst;
}

// Public interface implementation
SingleInstance::SingleInstance(const std::string& appName)
    : pImpl(std::make_unique<Impl>(appName))
{
}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::isFirstInstance() const {
    return pImpl->isFirstInstance();
}
