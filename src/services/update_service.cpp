#include "presence_for_plex/services/update_service.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/utils/logger.hpp"
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

        try {
            auto release_info = nlohmann::json::parse(response);

            std::string latest_version = release_info["tag_name"];
            if (!latest_version.empty() && latest_version[0] == 'v') {
                latest_version = latest_version.substr(1);
            }

            LOG_INFO("UpdateService", "Latest version: " + latest_version);

            bool update_available = (latest_version != m_current_version);

            UpdateInfo info{
                .current_version = m_current_version,
                .latest_version = latest_version,
                .download_url = release_info.value("html_url", ""),
                .release_notes = release_info.value("body", ""),
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

        } catch (const nlohmann::json::exception& e) {
            LOG_ERROR("UpdateService", "Failed to parse GitHub response: " + std::string(e.what()));
            if (m_event_bus) {
                m_event_bus->publish(core::events::UpdateCheckFailed{"Invalid response from GitHub"});
            }
            return std::unexpected(UpdateCheckError::ParseError);
        }
    }

void GitHubUpdateService::set_event_bus(std::shared_ptr<core::EventBus> bus) {
    m_event_bus = std::move(bus);
}

std::string GitHubUpdateService::get_current_version() const {
    return m_current_version;
}

} // namespace presence_for_plex::services
