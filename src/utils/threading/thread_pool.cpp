#include "presence_for_plex/utils/threading.hpp"

#include <algorithm>

namespace presence_for_plex {
namespace utils {

// ThreadSafeQueue implementation
template<typename T>
void ThreadSafeQueue<T>::push(T item) {
    std::lock_guard lock(m_mutex);
    m_queue.push(std::move(item));
    m_condition.notify_one();
}

template<typename T>
void ThreadSafeQueue<T>::push(T&& item) {
    std::lock_guard lock(m_mutex);
    m_queue.push(std::forward<T>(item));
    m_condition.notify_one();
}

template<typename T>
bool ThreadSafeQueue<T>::try_pop(T& item) {
    std::lock_guard lock(m_mutex);
    if (m_queue.empty()) {
        return false;
    }
    item = std::move(m_queue.front());
    m_queue.pop();
    return true;
}

template<typename T>
std::optional<T> ThreadSafeQueue<T>::try_pop() {
    std::lock_guard lock(m_mutex);
    if (m_queue.empty()) {
        return std::nullopt;
    }
    auto item = std::move(m_queue.front());
    m_queue.pop();
    return item;
}

template<typename T>
void ThreadSafeQueue<T>::wait_and_pop(T& item) {
    std::unique_lock lock(m_mutex);
    m_condition.wait(lock, [this] { return !m_queue.empty() || m_interrupted.load(); });

    if (m_interrupted.load()) {
        throw std::runtime_error("Queue interrupted");
    }

    item = std::move(m_queue.front());
    m_queue.pop();
}

template<typename T>
std::optional<T> ThreadSafeQueue<T>::wait_and_pop() {
    std::unique_lock lock(m_mutex);
    m_condition.wait(lock, [this] { return !m_queue.empty() || m_interrupted.load(); });

    if (m_interrupted.load()) {
        return std::nullopt;
    }

    auto item = std::move(m_queue.front());
    m_queue.pop();
    return item;
}

template<typename T>
bool ThreadSafeQueue<T>::wait_for_pop(T& item, std::chrono::milliseconds timeout) {
    std::unique_lock lock(m_mutex);
    bool result = m_condition.wait_for(lock, timeout,
        [this] { return !m_queue.empty() || m_interrupted.load(); });

    if (!result || m_interrupted.load() || m_queue.empty()) {
        return false;
    }

    item = std::move(m_queue.front());
    m_queue.pop();
    return true;
}

template<typename T>
std::optional<T> ThreadSafeQueue<T>::wait_for_pop(std::chrono::milliseconds timeout) {
    std::unique_lock lock(m_mutex);
    bool result = m_condition.wait_for(lock, timeout,
        [this] { return !m_queue.empty() || m_interrupted.load(); });

    if (!result || m_interrupted.load() || m_queue.empty()) {
        return std::nullopt;
    }

    auto item = std::move(m_queue.front());
    m_queue.pop();
    return item;
}

template<typename T>
void ThreadSafeQueue<T>::clear() {
    std::lock_guard lock(m_mutex);
    std::queue<T> empty;
    m_queue.swap(empty);
}

template<typename T>
size_t ThreadSafeQueue<T>::size() const {
    std::lock_guard lock(m_mutex);
    return m_queue.size();
}

template<typename T>
bool ThreadSafeQueue<T>::empty() const {
    std::lock_guard lock(m_mutex);
    return m_queue.empty();
}

template<typename T>
void ThreadSafeQueue<T>::interrupt() {
    std::lock_guard lock(m_mutex);
    m_interrupted = true;
    m_condition.notify_all();
}

// Explicit template instantiations for common types
template class ThreadSafeQueue<std::function<void()>>;
template class ThreadSafeQueue<std::packaged_task<void()>>;

// ThreadPool implementation
ThreadPool::ThreadPool(size_t num_threads) {
    m_workers.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        m_workers.emplace_back([this](std::stop_token stop_token) {
            worker_thread(stop_token);
        });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

template<Callable F>
auto ThreadPool::submit(F&& f) -> std::future<std::invoke_result_t<F>> {
    return submit_with_priority(Priority::Normal, std::forward<F>(f));
}

template<CallableWith<> F>
auto ThreadPool::submit(F&& f) -> std::future<std::invoke_result_t<F>> {
    return submit_with_priority(Priority::Normal, std::forward<F>(f));
}

template<typename F, typename... Args>
requires CallableWith<F, Args...>
auto ThreadPool::submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    return submit_with_priority(Priority::Normal, std::forward<F>(f), std::forward<Args>(args)...);
}

template<typename F, typename... Args>
auto ThreadPool::submit_with_priority(Priority priority, F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {

    using ReturnType = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        [f = std::forward<F>(f), args...]() mutable -> ReturnType {
            if constexpr (std::is_void_v<ReturnType>) {
                std::invoke(f, args...);
            } else {
                return std::invoke(f, args...);
            }
        }
    );

    auto future = task->get_future();

    {
        std::lock_guard lock(m_queue_mutex);

        if (m_shutdown.load()) {
            throw std::runtime_error("ThreadPool is shutting down");
        }

        m_tasks.emplace(Task{
            .function = [task]() { (*task)(); },
            .priority = priority,
            .submit_time = std::chrono::steady_clock::now()
        });
    }

    m_condition.notify_one();
    return future;
}

void ThreadPool::shutdown() {
    {
        std::lock_guard lock(m_queue_mutex);
        m_shutdown = true;
    }

    m_condition.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.request_stop();
            worker.join();
        }
    }

    m_workers.clear();
}

void ThreadPool::wait_for_completion() {
    while (true) {
        {
            std::lock_guard lock(m_queue_mutex);
            if (m_tasks.empty() && m_active_threads.load() == 0) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

size_t ThreadPool::active_threads() const {
    return m_active_threads.load();
}

size_t ThreadPool::queued_tasks() const {
    std::lock_guard lock(m_queue_mutex);
    return m_tasks.size();
}

bool ThreadPool::is_shutdown() const {
    return m_shutdown.load();
}

void ThreadPool::worker_thread(std::stop_token stop_token) {
    while (!stop_token.stop_requested() && !m_shutdown.load()) {
        Task task;

        {
            std::unique_lock lock(m_queue_mutex);
            m_condition.wait(lock, [this, &stop_token] {
                return !m_tasks.empty() || m_shutdown.load() || stop_token.stop_requested();
            });

            if (m_shutdown.load() || stop_token.stop_requested()) {
                break;
            }

            if (m_tasks.empty()) {
                continue;
            }

            task = std::move(const_cast<Task&>(m_tasks.top()));
            m_tasks.pop();
        }

        m_active_threads.fetch_add(1);

        try {
            task.function();
        } catch (...) {
            // Log the exception or handle it appropriately
            // For now, we just swallow it to prevent thread termination
        }

        m_active_threads.fetch_sub(1);
    }
}

// TaskScheduler implementation
TaskScheduler::TaskScheduler() {
    m_scheduler_thread = std::jthread([this](std::stop_token stop_token) {
        scheduler_loop(stop_token);
    });
}

TaskScheduler::~TaskScheduler() {
    shutdown();
}

template<typename F, typename... Args>
TaskScheduler::TaskId TaskScheduler::schedule_once(std::chrono::milliseconds delay, F&& f, Args&&... args) {
    auto now = std::chrono::steady_clock::now();
    auto run_time = now + delay;

    TaskId id = m_next_id.fetch_add(1);

    {
        std::lock_guard lock(m_mutex);
        m_tasks.emplace(ScheduledTask{
            .id = id,
            .function = [f = std::forward<F>(f), args...]() mutable {
                std::invoke(f, args...);
            },
            .next_run = run_time,
            .interval = std::chrono::milliseconds{0}
        });
    }

    m_condition.notify_one();
    return id;
}

template<typename F, typename... Args>
TaskScheduler::TaskId TaskScheduler::schedule_recurring(std::chrono::milliseconds interval, F&& f, Args&&... args) {
    auto now = std::chrono::steady_clock::now();
    auto run_time = now + interval;

    TaskId id = m_next_id.fetch_add(1);

    {
        std::lock_guard lock(m_mutex);
        m_tasks.emplace(ScheduledTask{
            .id = id,
            .function = [f = std::forward<F>(f), args...]() mutable {
                std::invoke(f, args...);
            },
            .next_run = run_time,
            .interval = interval
        });
    }

    m_condition.notify_one();
    return id;
}

template<typename F, typename... Args>
TaskScheduler::TaskId TaskScheduler::schedule_at(std::chrono::system_clock::time_point when, F&& f, Args&&... args) {
    // Convert system_clock to steady_clock (approximate)
    auto system_now = std::chrono::system_clock::now();
    auto steady_now = std::chrono::steady_clock::now();
    auto offset = when - system_now;
    auto run_time = steady_now + offset;

    TaskId id = m_next_id.fetch_add(1);

    {
        std::lock_guard lock(m_mutex);
        m_tasks.emplace(ScheduledTask{
            .id = id,
            .function = [f = std::forward<F>(f), args...]() mutable {
                std::invoke(f, args...);
            },
            .next_run = run_time,
            .interval = std::chrono::milliseconds{0}
        });
    }

    m_condition.notify_one();
    return id;
}

bool TaskScheduler::cancel_task(TaskId id) {
    std::lock_guard lock(m_mutex);

    // Find and mark the task as cancelled
    // Note: This is a simplified implementation. A more efficient approach
    // would use a separate data structure to track active tasks.
    std::vector<ScheduledTask> tasks_copy;
    bool found = false;

    while (!m_tasks.empty()) {
        auto task = m_tasks.top();
        m_tasks.pop();

        if (task.id == id) {
            task.cancelled = true;
            found = true;
        }

        tasks_copy.push_back(std::move(task));
    }

    // Rebuild the priority queue
    for (auto& task : tasks_copy) {
        m_tasks.push(std::move(task));
    }

    return found;
}

void TaskScheduler::shutdown() {
    {
        std::lock_guard lock(m_mutex);
        m_shutdown = true;
    }

    m_condition.notify_one();

    if (m_scheduler_thread.joinable()) {
        m_scheduler_thread.request_stop();
        m_scheduler_thread.join();
    }
}

size_t TaskScheduler::pending_tasks() const {
    std::lock_guard lock(m_mutex);
    return m_tasks.size();
}

void TaskScheduler::scheduler_loop(std::stop_token stop_token) {
    while (!stop_token.stop_requested() && !m_shutdown.load()) {
        std::optional<ScheduledTask> task_to_run;

        {
            std::unique_lock lock(m_mutex);

            if (m_tasks.empty()) {
                m_condition.wait(lock, [this, &stop_token] {
                    return !m_tasks.empty() || m_shutdown.load() || stop_token.stop_requested();
                });
                continue;
            }

            auto now = std::chrono::steady_clock::now();
            auto& next_task = m_tasks.top();

            if (next_task.next_run <= now) {
                task_to_run = next_task;
                m_tasks.pop();
            } else {
                auto wait_duration = next_task.next_run - now;
                m_condition.wait_for(lock, wait_duration, [this, &stop_token] {
                    return m_shutdown.load() || stop_token.stop_requested();
                });
                continue;
            }
        }

        if (task_to_run && !task_to_run->cancelled) {
            try {
                task_to_run->function();
            } catch (...) {
                // Log the exception or handle it appropriately
            }

            // If it's a recurring task, reschedule it
            if (task_to_run->interval.count() > 0) {
                task_to_run->next_run = std::chrono::steady_clock::now() + task_to_run->interval;

                std::lock_guard lock(m_mutex);
                m_tasks.push(std::move(*task_to_run));
            }
        }
    }
}

} // namespace utils
} // namespace presence_for_plex