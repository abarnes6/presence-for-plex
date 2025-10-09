#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/utils/logger.hpp"

namespace presence_for_plex::core {

void EventBus::handle_exception(const std::string& event_type, const std::exception& e) {
    LOG_ERROR("EventBus", "Exception in event handler for " + event_type + ": " + e.what());
}

}