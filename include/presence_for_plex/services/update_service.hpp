#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include <memory>
#include <string>
#include <expected>

namespace presence_for_plex::services {

enum class UpdateCheckError {
    NetworkError,
    ParseError,
    InvalidResponse,
    RateLimited
};

struct UpdateInfo {
    std::string current_version;
    std::string latest_version;
    std::string download_url;
    std::string release_notes;
    bool update_available;
};

class GitHubUpdateService {
public:
    GitHubUpdateService(
        std::string repo_owner,
        std::string repo_name,
        std::string current_version,
        std::shared_ptr<HttpClient> http_client
    );

    std::expected<UpdateInfo, UpdateCheckError> check_for_updates();
    void set_event_bus(std::shared_ptr<core::EventBus> bus);
    std::string get_current_version() const;

private:
    std::string m_repo_owner;
    std::string m_repo_name;
    std::string m_current_version;
    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::EventBus> m_event_bus;
};

} // namespace presence_for_plex::services
