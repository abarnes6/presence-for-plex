#include "presence_for_plex/utils/threading.hpp"

#include <algorithm>

namespace presence_for_plex {
namespace utils {

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

// Template implementations are in the header file

void ThreadPool::shutdown() {
    {
        std::lock_guard lock(m_queue_mutex);
        m_shutdown = true;
    }

    m_condition.notify_all();

    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.request_stop();
        }
    }

    // Workers are std::jthread and will auto-join in their destructor
    m_workers.clear();
}

void ThreadPool::worker_thread(std::stop_token stop_token) {
    while (!stop_token.stop_requested() && !m_shutdown.load()) {
        std::function<void()> task;

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

            task = std::move(m_tasks.front());
            m_tasks.pop();
        }

        m_active_threads.fetch_add(1);

        try {
            task();
        } catch (...) {
            // Log the exception or handle it appropriately
            // For now, we just swallow it to prevent thread termination
        }

        m_active_threads.fetch_sub(1);
    }
}

// Explicit template instantiations are not needed since templates are in header

} // namespace utils
} // namespace presence_for_plex