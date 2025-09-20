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

namespace presence_for_plex {
namespace utils {

// Concepts for type safety
template<typename F>
concept Callable = std::is_invocable_v<F>;

template<typename F, typename... Args>
concept CallableWith = std::is_invocable_v<F, Args...>;

// Thread-safe queue
template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;
    ~ThreadSafeQueue() = default;

    // Non-copyable, movable
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue(ThreadSafeQueue&&) = delete;
    ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;

    void push(T item);
    void push(T&& item);

    bool try_pop(T& item);
    std::optional<T> try_pop();

    void wait_and_pop(T& item);
    std::optional<T> wait_and_pop();

    bool wait_for_pop(T& item, std::chrono::milliseconds timeout);
    std::optional<T> wait_for_pop(std::chrono::milliseconds timeout);

    void clear();
    size_t size() const;
    bool empty() const;

    // Wake up all waiting threads (for shutdown)
    void interrupt();

private:
    mutable std::mutex m_mutex;
    std::queue<T> m_queue;
    std::condition_variable m_condition;
    std::atomic<bool> m_interrupted{false};
};

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

    // Submit work with automatic return type deduction
    template<Callable F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<F>>;

    template<CallableWith<> F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<F>>;

    template<typename F, typename... Args>
    requires CallableWith<F, Args...>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    // Submit work with priority
    enum class Priority { Low, Normal, High };

    template<typename F, typename... Args>
    auto submit_with_priority(Priority priority, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    // Utility methods
    void shutdown();
    void wait_for_completion();
    size_t active_threads() const;
    size_t queued_tasks() const;
    bool is_shutdown() const;

private:
    struct Task {
        std::function<void()> function;
        Priority priority = Priority::Normal;
        std::chrono::steady_clock::time_point submit_time;

        bool operator<(const Task& other) const {
            if (priority != other.priority) {
                return priority < other.priority; // Higher priority first
            }
            return submit_time > other.submit_time; // Earlier submission first
        }
    };

    std::vector<std::jthread> m_workers;
    std::priority_queue<Task> m_tasks;
    mutable std::mutex m_queue_mutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_shutdown{false};
    std::atomic<size_t> m_active_threads{0};

    void worker_thread(std::stop_token stop_token);
};

// Task scheduler for delayed/periodic execution
class TaskScheduler {
public:
    TaskScheduler();
    ~TaskScheduler();

    // Non-copyable, movable
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;
    TaskScheduler(TaskScheduler&&) = delete;
    TaskScheduler& operator=(TaskScheduler&&) = delete;

    using TaskId = std::uint64_t;

    // Schedule one-time task
    template<typename F, typename... Args>
    TaskId schedule_once(std::chrono::milliseconds delay, F&& f, Args&&... args);

    // Schedule recurring task
    template<typename F, typename... Args>
    TaskId schedule_recurring(std::chrono::milliseconds interval, F&& f, Args&&... args);

    // Schedule at specific time
    template<typename F, typename... Args>
    TaskId schedule_at(std::chrono::system_clock::time_point when, F&& f, Args&&... args);

    // Cancel task
    bool cancel_task(TaskId id);

    // Utility methods
    void shutdown();
    size_t pending_tasks() const;

private:
    struct ScheduledTask {
        TaskId id;
        std::function<void()> function;
        std::chrono::steady_clock::time_point next_run;
        std::chrono::milliseconds interval{0}; // 0 means one-time
        bool cancelled = false;

        bool operator>(const ScheduledTask& other) const {
            return next_run > other.next_run; // Earlier time has higher priority
        }
    };

    std::jthread m_scheduler_thread;
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, std::greater<>> m_tasks;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::atomic<TaskId> m_next_id{1};
    std::atomic<bool> m_shutdown{false};

    void scheduler_loop(std::stop_token stop_token);
};

// RAII scope guard
template<typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F&& f) : m_function(std::forward<F>(f)), m_active(true) {}

    ~ScopeGuard() {
        if (m_active) {
            m_function();
        }
    }

    // Non-copyable, movable
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    ScopeGuard(ScopeGuard&& other) noexcept
        : m_function(std::move(other.m_function)), m_active(other.m_active) {
        other.m_active = false;
    }

    ScopeGuard& operator=(ScopeGuard&& other) noexcept {
        if (this != &other) {
            if (m_active) {
                m_function();
            }
            m_function = std::move(other.m_function);
            m_active = other.m_active;
            other.m_active = false;
        }
        return *this;
    }

    void dismiss() {
        m_active = false;
    }

private:
    F m_function;
    bool m_active;
};

// Helper function to create scope guards
template<typename F>
auto make_scope_guard(F&& f) {
    return ScopeGuard<std::decay_t<F>>(std::forward<F>(f));
}

// Utility macros
#define SCOPE_EXIT(code) \
    auto CONCAT(_scope_guard_, __LINE__) = make_scope_guard([&]() { code; })

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)

} // namespace utils
} // namespace presence_for_plex