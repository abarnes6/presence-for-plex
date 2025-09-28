#pragma once

#include "presence_for_plex/core/models.hpp"
#include <unordered_map>
#include <memory>
#include <atomic>

namespace presence_for_plex {
namespace core {

// Transient runtime data that should not be persisted
struct RuntimeState {
    // Active Plex servers discovered at runtime
    std::unordered_map<std::string, std::unique_ptr<PlexServer>> plex_servers;

    // Connection states
    std::atomic<bool> plex_connected{false};
    std::atomic<bool> discord_connected{false};

    // Service states
    std::atomic<bool> services_running{false};

    // Statistics
    struct Stats {
        std::chrono::system_clock::time_point startup_time;
        uint64_t media_updates_received{0};
        uint64_t presence_updates_sent{0};
    } stats;

    RuntimeState() = default;
    RuntimeState(const RuntimeState&) = delete;
    RuntimeState& operator=(const RuntimeState&) = delete;
    RuntimeState(RuntimeState&&) = delete;
    RuntimeState& operator=(RuntimeState&&) = delete;
};

} // namespace core
} // namespace presence_for_plex