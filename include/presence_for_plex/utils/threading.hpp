#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <concepts>
#include <chrono>
#include <expected>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <stop_token>

namespace presence_for_plex {
namespace utils {

// Error types for thread pool operations
enum class ThreadPoolError {
    Shutdown,
    QueueFull,
    InvalidTask
};

// Concepts for type safety
template<typename F>
concept Callable = std::is_invocable_v<F>;

template<typename F, typename... Args>
concept CallableWith = std::is_invocable_v<F, Args...>;

// Modern thread pool with C++20 features
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();

    // Non-copyable, movable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Submit work with automatic return type deduction (throws on shutdown)
    template<Callable F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<F>>;

    template<typename F, typename... Args>
    requires CallableWith<F, Args...>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    // Safe submit methods that return std::expected
    template<Callable F>
    auto try_submit(F&& f) -> std::expected<std::future<std::invoke_result_t<F>>, ThreadPoolError>;

    template<typename F, typename... Args>
    requires CallableWith<F, Args...>
    auto try_submit(F&& f, Args&&... args) -> std::expected<std::future<std::invoke_result_t<F, Args...>>, ThreadPoolError>;

    // Utility methods
    void shutdown();

private:
    std::vector<std::jthread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    mutable std::mutex m_queue_mutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_shutdown{false};
    std::atomic<size_t> m_active_threads{0};

    void worker_thread(std::stop_token stop_token);
};

// Template implementations for ThreadPool
template<Callable F>
auto ThreadPool::submit(F&& f) -> std::future<std::invoke_result_t<F>> {
    using return_type = std::invoke_result_t<F>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::forward<F>(f)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);

        if (m_shutdown.load()) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }

        m_tasks.emplace([task]() { (*task)(); });
    }
    m_condition.notify_one();
    return res;
}

template<typename F, typename... Args>
requires CallableWith<F, Args...>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    return submit([f = std::forward<F>(f), ...args = std::forward<Args>(args)]() {
        return f(args...);
    });
}

// Safe submit methods implementations
template<Callable F>
auto ThreadPool::try_submit(F&& f) -> std::expected<std::future<std::invoke_result_t<F>>, ThreadPoolError> {
    using return_type = std::invoke_result_t<F>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::forward<F>(f)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);

        if (m_shutdown.load()) {
            return std::unexpected(ThreadPoolError::Shutdown);
        }

        m_tasks.emplace([task]() { (*task)(); });
    }
    m_condition.notify_one();
    return res;
}

template<typename F, typename... Args>
requires CallableWith<F, Args...>
auto ThreadPool::try_submit(F&& f, Args&&... args) -> std::expected<std::future<std::invoke_result_t<F, Args...>>, ThreadPoolError> {
    return try_submit([f = std::forward<F>(f), ...args = std::forward<Args>(args)]() {
        return f(args...);
    });
}

} // namespace utils
} // namespace presence_for_plex