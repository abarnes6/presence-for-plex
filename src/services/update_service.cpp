#include "presence_for_plex/services/update_service.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/json_helper.hpp"
#include <nlohmann/json.hpp>
#include <map>

namespace presence_for_plex::services {

GitHubUpdateService::GitHubUpdateService(
    std::string repo_owner,
    std::string repo_name,
    std::string current_version,
    std::shared_ptr<HttpClient> http_client
)
    : m_repo_owner(std::move(repo_owner)),
      m_repo_name(std::move(repo_name)),
      m_current_version(std::move(current_version)),
      m_http_client(std::move(http_client))
{
    LOG_DEBUG("UpdateService", "GitHub update service created for " + m_repo_owner + "/" + m_repo_name);
}

std::expected<UpdateInfo, UpdateCheckError> GitHubUpdateService::check_for_updates() {
        LOG_INFO("UpdateService", "Checking for updates...");

        if (m_event_bus) {
            m_event_bus->publish(core::events::UpdateCheckStarted{m_current_version});
        }

        auto api_url = "https://api.github.com/repos/" + m_repo_owner + "/" + m_repo_name + "/releases/latest";

        HttpHeaders headers = {
            {"User-Agent", "Presence-For-Plex-Update-Checker"},
            {"Accept", "application/json"}
        };

        auto response_result = m_http_client->get(api_url, headers);
        if (!response_result) {
            LOG_ERROR("UpdateService", "Failed to connect to GitHub API");
            if (m_event_bus) {
                m_event_bus->publish(core::events::UpdateCheckFailed{"Failed to connect to GitHub"});
            }
            return std::unexpected(UpdateCheckError::NetworkError);
        }

        const auto& response = response_result->body;

        auto json_result = utils::JsonHelper::safe_parse(response);
        if (!json_result) {
            LOG_ERROR("UpdateService", "Failed to parse GitHub response: " + json_result.error());
            if (m_event_bus) {
                m_event_bus->publish(core::events::UpdateCheckFailed{"Invalid response from GitHub"});
            }
            return std::unexpected(UpdateCheckError::ParseError);
        }

        auto release_info = json_result.value();

        auto version_result = utils::JsonHelper::get_required<std::string>(release_info, "tag_name");
        if (!version_result) {
            LOG_ERROR("UpdateService", "Missing tag_name in GitHub response: " + version_result.error());
            if (m_event_bus) {
                m_event_bus->publish(core::events::UpdateCheckFailed{"Invalid response from GitHub"});
            }
            return std::unexpected(UpdateCheckError::ParseError);
        }

        std::string latest_version = version_result.value();
        if (!latest_version.empty() && latest_version[0] == 'v') {
            latest_version = latest_version.substr(1);
        }

        LOG_INFO("UpdateService", "Latest version: " + latest_version);

        bool update_available = (latest_version != m_current_version);

        UpdateInfo info{
            .current_version = m_current_version,
            .latest_version = latest_version,
            .download_url = utils::JsonHelper::get_optional<std::string>(release_info, "html_url", ""),
            .release_notes = utils::JsonHelper::get_optional<std::string>(release_info, "body", ""),
            .update_available = update_available
        };

        if (m_event_bus) {
            if (update_available) {
                m_event_bus->publish(core::events::UpdateAvailable{
                    m_current_version,
                    latest_version,
                    info.download_url,
                    info.release_notes
                });
            } else {
                m_event_bus->publish(core::events::NoUpdateAvailable{m_current_version});
            }
        }

        return info;
    }

void GitHubUpdateService::set_event_bus(std::shared_ptr<core::EventBus> bus) {
    m_event_bus = std::move(bus);
}

std::string GitHubUpdateService::get_current_version() const {
    return m_current_version;
}

} // namespace presence_for_plex::services
