#pragma once

#include <queue>
#include <mutex>
#include <chrono>
#include <optional>
#include <functional>

namespace presence_for_plex::services {

/**
 * @brief Represents a queued frame with metadata
 */
template<typename T>
struct QueuedFrame {
    T data;
    std::chrono::system_clock::time_point timestamp;
    int priority = 0;  // Higher values = higher priority
    std::optional<std::chrono::system_clock::time_point> expires_at;

    QueuedFrame(T frame_data, int frame_priority = 0,
                std::optional<std::chrono::system_clock::time_point> expiry = std::nullopt)
        : data(std::move(frame_data))
        , timestamp(std::chrono::system_clock::now())
        , priority(frame_priority)
        , expires_at(expiry) {}

    bool is_expired() const {
        if (!expires_at) return false;
        return std::chrono::system_clock::now() > *expires_at;
    }
};

/**
 * @brief Configuration for frame queue behavior
 */
struct FrameQueueConfig {
    // Maximum number of frames to keep in queue
    size_t max_queue_size = 100;

    // Default TTL for frames
    std::chrono::seconds default_ttl{300}; // 5 minutes

    // Whether to auto-expire old frames
    bool enable_expiration = true;

    // Whether to replace duplicate frames (based on data comparison)
    bool replace_duplicates = true;

    // Maximum age for frames before they're considered stale
    std::chrono::seconds max_frame_age{600}; // 10 minutes

    bool is_valid() const {
        return max_queue_size > 0 &&
               default_ttl > std::chrono::seconds{0} &&
               max_frame_age >= default_ttl;
    }
};

/**
 * @brief Thread-safe priority queue for frames with expiration support
 *
 * Follows Single Responsibility Principle by focusing only on frame queuing.
 * Template allows reuse for different frame types.
 */
template<typename T>
class FrameQueue {
public:
    using FrameType = QueuedFrame<T>;
    using ProcessorCallback = std::function<bool(const T&)>;

    explicit FrameQueue(FrameQueueConfig config = {})
        : m_config(std::move(config)) {

        if (!m_config.is_valid()) {
            m_config = FrameQueueConfig{};
        }
    }

    /**
     * @brief Add a frame to the queue
     * @param frame_data The frame data to queue
     * @param priority Priority level (higher = more important)
     * @param custom_ttl Custom TTL for this frame (optional)
     * @return true if frame was queued, false if rejected
     */
    bool enqueue(T frame_data, int priority = 0,
                 std::optional<std::chrono::seconds> custom_ttl = std::nullopt) {

        std::lock_guard<std::mutex> lock(m_mutex);

        cleanup_expired_frames();

        // Check size limit
        if (m_frames.size() >= m_config.max_queue_size) {
            // Remove lowest priority frame if queue is full
            if (!remove_lowest_priority_frame()) {
                return false; // Queue full and couldn't make space
            }
        }

        // Calculate expiration
        std::optional<std::chrono::system_clock::time_point> expires_at;
        if (m_config.enable_expiration) {
            auto ttl = custom_ttl ? *custom_ttl : m_config.default_ttl;
            expires_at = std::chrono::system_clock::now() + ttl;
        }

        // Check for duplicates if enabled
        if (m_config.replace_duplicates) {
            remove_duplicate_frames(frame_data);
        }

        m_frames.emplace(std::move(frame_data), priority, expires_at);
        return true;
    }

    /**
     * @brief Get the next frame from the queue
     * @return The highest priority, non-expired frame, or nullopt if queue is empty
     */
    std::optional<FrameType> dequeue() {
        std::lock_guard<std::mutex> lock(m_mutex);

        cleanup_expired_frames();

        if (m_frames.empty()) {
            return std::nullopt;
        }

        auto frame = m_frames.top();
        m_frames.pop();
        return frame;
    }

    /**
     * @brief Peek at the next frame without removing it
     */
    std::optional<FrameType> peek() const {
        std::lock_guard<std::mutex> lock(m_mutex);

        const_cast<FrameQueue*>(this)->cleanup_expired_frames();

        if (m_frames.empty()) {
            return std::nullopt;
        }

        return m_frames.top();
    }

    /**
     * @brief Process all frames in the queue with a callback
     * @param processor Function to process each frame
     * @param stop_on_failure If true, stop processing on first failure
     * @return Number of frames successfully processed
     */
    size_t process_all(ProcessorCallback processor, bool stop_on_failure = false) {
        if (!processor) {
            return 0;
        }

        size_t processed = 0;

        while (auto frame = dequeue()) {
            try {
                if (processor(frame->data)) {
                    ++processed;
                } else if (stop_on_failure) {
                    // Re-queue the failed frame if we're stopping on failure
                    enqueue(std::move(frame->data), frame->priority);
                    break;
                }
            } catch (const std::exception& e) {
                if (stop_on_failure) {
                    // Re-queue the failed frame
                    enqueue(std::move(frame->data), frame->priority);
                    break;
                }
            }
        }

        return processed;
    }

    /**
     * @brief Get current queue size
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const_cast<FrameQueue*>(this)->cleanup_expired_frames();
        return m_frames.size();
    }

    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        return size() == 0;
    }

    /**
     * @brief Clear all frames from the queue
     */
    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!m_frames.empty()) {
            m_frames.pop();
        }
    }

    /**
     * @brief Get queue statistics
     */
    struct Stats {
        size_t current_size = 0;
        size_t expired_frames_removed = 0;
        size_t total_frames_enqueued = 0;
        size_t total_frames_processed = 0;
    };

    Stats get_stats() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const_cast<FrameQueue*>(this)->cleanup_expired_frames();

        Stats stats;
        stats.current_size = m_frames.size();
        stats.expired_frames_removed = m_expired_count;
        stats.total_frames_enqueued = m_enqueue_count;
        stats.total_frames_processed = m_process_count;
        return stats;
    }

private:
    mutable std::mutex m_mutex;
    FrameQueueConfig m_config;

    // Use priority queue for efficient ordering
    struct FrameComparator {
        bool operator()(const FrameType& a, const FrameType& b) const {
            // Higher priority first, then older timestamp first
            if (a.priority != b.priority) {
                return a.priority < b.priority;
            }
            return a.timestamp > b.timestamp;
        }
    };

    std::priority_queue<FrameType, std::vector<FrameType>, FrameComparator> m_frames;

    // Statistics
    size_t m_expired_count = 0;
    size_t m_enqueue_count = 0;
    size_t m_process_count = 0;

    void cleanup_expired_frames() {
        if (!m_config.enable_expiration) {
            return;
        }

        auto now = std::chrono::system_clock::now();
        std::vector<FrameType> valid_frames;

        // Extract all valid frames
        while (!m_frames.empty()) {
            auto frame = m_frames.top();
            m_frames.pop();

            if (frame.is_expired() ||
                (now - frame.timestamp) > m_config.max_frame_age) {
                ++m_expired_count;
            } else {
                valid_frames.push_back(std::move(frame));
            }
        }

        // Put valid frames back
        for (auto& frame : valid_frames) {
            m_frames.push(std::move(frame));
        }
    }

    bool remove_lowest_priority_frame() {
        if (m_frames.empty()) {
            return false;
        }

        // Since we use a max-heap, we need to find the minimum priority frame
        std::vector<FrameType> temp_frames;
        FrameType* lowest_frame = nullptr;
        int lowest_priority = std::numeric_limits<int>::max();

        // Find the frame with lowest priority
        while (!m_frames.empty()) {
            auto frame = m_frames.top();
            m_frames.pop();

            if (frame.priority < lowest_priority) {
                if (lowest_frame) {
                    temp_frames.push_back(std::move(*lowest_frame));
                }
                lowest_priority = frame.priority;
                lowest_frame = &frame;
            } else {
                temp_frames.push_back(std::move(frame));
            }
        }

        // Put all frames back except the lowest priority one
        for (auto& frame : temp_frames) {
            m_frames.push(std::move(frame));
        }

        return lowest_frame != nullptr;
    }

    void remove_duplicate_frames(const T& new_frame_data) {
        std::vector<FrameType> unique_frames;

        while (!m_frames.empty()) {
            auto frame = m_frames.top();
            m_frames.pop();

            // This requires T to be comparable
            if (!(frame.data == new_frame_data)) {
                unique_frames.push_back(std::move(frame));
            }
        }

        // Put unique frames back
        for (auto& frame : unique_frames) {
            m_frames.push(std::move(frame));
        }
    }
};

} // namespace presence_for_plex::services