#pragma once

#include "presence_for_plex/core/models.hpp"
#include <string>
#include <memory>
#include <map>
#include <expected>
#include <atomic>

namespace presence_for_plex {
namespace core {
    class AuthenticationService;
}
namespace platform {
    class BrowserLauncher;
}
namespace services {

// Forward declarations
class HttpClient;

class PlexAuthenticator {
public:
    explicit PlexAuthenticator(std::shared_ptr<HttpClient> http_client,
                              std::shared_ptr<core::AuthenticationService> auth_service,
                              std::shared_ptr<platform::BrowserLauncher> browser_launcher = nullptr);
    ~PlexAuthenticator() = default;

    std::expected<core::PlexToken, core::PlexError> acquire_auth_token();
    std::expected<std::string, core::PlexError> fetch_username(const core::PlexToken& token);
    std::expected<void, core::PlexError> validate_token(const core::PlexToken& token);
    std::expected<core::PlexToken, core::PlexError> ensure_authenticated();

    std::map<std::string, std::string> get_standard_headers(const core::PlexToken& token = {}) const;

    void shutdown();

private:
    // Internal helper methods
    std::expected<std::pair<std::string, std::string>, core::PlexError> request_plex_pin();
    void open_authorization_url(const std::string& pin, const std::string& client_id);
    std::expected<core::PlexToken, core::PlexError> poll_for_pin_authorization(
        const std::string& pin_id,
        const std::string& pin
    );

    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::AuthenticationService> m_auth_service;
    std::shared_ptr<platform::BrowserLauncher> m_browser_launcher;
    std::atomic<bool> m_shutting_down{false};
};

} // namespace services
} // namespace presence_for_plex
