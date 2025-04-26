#pragma once

#include <thread>
#include <future>
#include <chrono>
#include <functional>
#include "logger.h"

namespace ThreadUtils {

/**
 * Join a thread with timeout
 * 
 * @param thread The thread to join
 * @param timeout Timeout duration
 * @param threadName Name for logging
 * @return true if the thread was joined, false if timeout occurred
 */
inline bool joinWithTimeout(std::thread& thread, std::chrono::milliseconds timeout, const std::string& threadName) {
    if (!thread.joinable()) {
        return true;
    }
    
    // Create a future to track the join operation
    auto joinFuture = std::async(std::launch::async, [&thread]() {
        thread.join();
    });
    
    // Wait for the join operation with timeout
    if (joinFuture.wait_for(timeout) == std::future_status::timeout) {
        LOG_WARNING("ThreadUtils", "Thread '" + threadName + "' join timed out after " + 
                   std::to_string(timeout.count()) + "ms");
        return false;
    }
    
    return true;
}

/**
 * Execute a function with timeout
 * 
 * @param func Function to execute
 * @param timeout Timeout duration
 * @param operationName Name for logging
 * @return true if function completed within timeout, false otherwise
 */
template<typename Func>
inline bool executeWithTimeout(Func func, std::chrono::milliseconds timeout, const std::string& operationName) {
    auto future = std::async(std::launch::async, func);
    
    if (future.wait_for(timeout) == std::future_status::timeout) {
        LOG_WARNING("ThreadUtils", "Operation '" + operationName + "' timed out after " + 
                   std::to_string(timeout.count()) + "ms");
        return false;
    }
    
    return true;
}

} // namespace ThreadUtils
