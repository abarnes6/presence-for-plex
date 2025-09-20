#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/utils/logger.hpp"

namespace presence_for_plex {
namespace core {

// Minimal event bus implementation
template<typename EventType>
class EventBusImpl : public EventBus<EventType> {
public:
    using EventHandler = typename EventBus<EventType>::EventHandler;
    using SubscriptionId = typename EventBus<EventType>::SubscriptionId;

    SubscriptionId subscribe(EventHandler handler) override {
        PLEX_LOG_DEBUG("EventBus", "Subscribing to events");
        return 0; // Stub
    }

    void unsubscribe(SubscriptionId id) override {
        PLEX_LOG_DEBUG("EventBus", "Unsubscribing from events");
        // Stub
    }

    void publish(const EventType& event) override {
        PLEX_LOG_DEBUG("EventBus", "Publishing event");
        // Stub
    }

    void clear() override {
        PLEX_LOG_DEBUG("EventBus", "Clearing event handlers");
        // Stub
    }
};

// Template method implementation - provide a general template implementation
template<typename EventType>
template<typename T>
std::unique_ptr<EventBus<T>> EventBus<EventType>::create() {
    return std::make_unique<EventBusImpl<T>>();
}

// Explicit template instantiations for the event types we use
template class EventBusImpl<MediaStateChangedEvent>;
template class EventBusImpl<DiscordConnectionEvent>;
template class EventBusImpl<PlexConnectionEvent>;
template class EventBusImpl<ApplicationStartedEvent>;

// Explicit template instantiations for EventBus::create method
template std::unique_ptr<EventBus<MediaStateChangedEvent>> EventBus<MediaStateChangedEvent>::create<MediaStateChangedEvent>();
template std::unique_ptr<EventBus<DiscordConnectionEvent>> EventBus<DiscordConnectionEvent>::create<DiscordConnectionEvent>();
template std::unique_ptr<EventBus<PlexConnectionEvent>> EventBus<PlexConnectionEvent>::create<PlexConnectionEvent>();
template std::unique_ptr<EventBus<ApplicationStartedEvent>> EventBus<ApplicationStartedEvent>::create<ApplicationStartedEvent>();

} // namespace core
} // namespace presence_for_plex