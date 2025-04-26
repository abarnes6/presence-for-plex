#pragma once

#include <string>
#include <memory>

/**
 * @class SingleInstance
 * @brief Ensures only one instance of the application is running at a time
 * 
 * Uses platform-specific mechanisms to create a mutex that prevents multiple
 * instances of the application from running simultaneously.
 */
class SingleInstance {
public:
    /**
     * @brief Constructor that attempts to create a single instance lock
     * @param appName The name of the application, used for the lock name
     */
    explicit SingleInstance(const std::string& appName);

    /**
     * @brief Destructor that releases the lock
     */
    ~SingleInstance();

    /**
     * @brief Check if this is the only instance
     * @return true if this is the only instance, false otherwise
     */
    bool isFirstInstance() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};
