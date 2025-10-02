#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/services/network_service.hpp"
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

class UpdateService {
public:
    virtual ~UpdateService() = default;

    virtual std::expected<UpdateInfo, UpdateCheckError> check_for_updates() = 0;
    virtual void set_event_bus(std::shared_ptr<core::EventBus> bus) = 0;
    virtual std::string get_current_version() const = 0;
};

class UpdateServiceFactory {
public:
    static std::unique_ptr<UpdateService> create_github_service(
        const std::string& repo_owner,
        const std::string& repo_name,
        const std::string& current_version,
        std::shared_ptr<HttpClient> http_client = nullptr
    );
};

} // namespace presence_for_plex::services
