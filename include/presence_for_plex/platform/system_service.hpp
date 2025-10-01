#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <string>

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

} // namespace platform
} // namespace presence_for_plex