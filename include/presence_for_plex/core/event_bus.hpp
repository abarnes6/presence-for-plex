#pragma once

#include <any>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <atomic>

namespace presence_for_plex::core {

class EventBus : public std::enable_shared_from_this<EventBus> {
public:
    EventBus() = default;
    ~EventBus() {
        shutdown();
    }

    void shutdown() {
        m_shutting_down = true;
    }
    using EventHandler = std::function<void(const std::any&)>;
    using HandlerId = std::size_t;

    template<typename EventType>
    HandlerId subscribe(std::function<void(const EventType&)> handler) {
        std::lock_guard lock(m_mutex);

        auto type_index = std::type_index(typeid(EventType));
        auto wrapped_handler = [handler](const std::any& event) {
            handler(std::any_cast<const EventType&>(event));
        };

        HandlerId id = m_next_handler_id++;
        m_handlers[type_index].emplace_back(id, wrapped_handler);
        m_handler_types.emplace(id, type_index);

        return id;
    }

    template<typename EventType>
    void publish(const EventType& event) {
        std::vector<EventHandler> handlers_copy;

        {
            std::lock_guard lock(m_mutex);
            auto type_index = std::type_index(typeid(EventType));
            auto it = m_handlers.find(type_index);

            if (it != m_handlers.end()) {
                handlers_copy.reserve(it->second.size());
                for (const auto& [id, handler] : it->second) {
                    handlers_copy.push_back(handler);
                }
            }
        }

        for (const auto& handler : handlers_copy) {
            try {
                handler(event);
            } catch (const std::exception& e) {
                handle_exception(typeid(EventType).name(), e);
            }
        }
    }

    template<typename EventType>
    void publish_async(const EventType& event) {
        if (m_shutting_down) {
            return;
        }

        auto event_copy = std::make_shared<EventType>(event);
        auto self = shared_from_this();
        std::thread([self, event_copy]() {
            if (!self->m_shutting_down) {
                self->publish(*event_copy);
            }
        }).detach();
    }

    void unsubscribe(HandlerId id) {
        std::lock_guard lock(m_mutex);

        auto type_it = m_handler_types.find(id);
        if (type_it == m_handler_types.end()) {
            return;
        }

        auto& handlers = m_handlers[type_it->second];
        handlers.erase(
            std::remove_if(handlers.begin(), handlers.end(),
                          [id](const auto& pair) { return pair.first == id; }),
            handlers.end()
        );

        m_handler_types.erase(id);
    }

    void clear() {
        std::lock_guard lock(m_mutex);
        m_handlers.clear();
        m_handler_types.clear();
    }

    template<typename EventType>
    std::size_t subscriber_count() const {
        std::lock_guard lock(m_mutex);
        auto type_index = std::type_index(typeid(EventType));
        auto it = m_handlers.find(type_index);
        return it != m_handlers.end() ? it->second.size() : 0;
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::type_index, std::vector<std::pair<HandlerId, EventHandler>>> m_handlers;
    std::unordered_map<HandlerId, std::type_index> m_handler_types;
    HandlerId m_next_handler_id{1};
    std::atomic<bool> m_shutting_down{false};

    void handle_exception(const std::string& event_type, const std::exception& e);
};

}