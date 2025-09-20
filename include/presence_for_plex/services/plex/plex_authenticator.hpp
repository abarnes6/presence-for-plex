#pragma once

#include "presence_for_plex/core/models.hpp"
#include <string>
#include <memory>
#include <map>
#include <expected>

namespace presence_for_plex {
namespace core {
    class ConfigurationService;
}
namespace services {

// Forward declarations
class HttpClient;

// Interface for Plex authentication following SRP
class IPlexAuthenticator {
public:
    virtual ~IPlexAuthenticator() = default;

    // Core authentication methods
    virtual std::expected<core::PlexToken, core::PlexError> acquire_auth_token() = 0;
    virtual std::expected<std::string, core::PlexError> fetch_username(const core::PlexToken& token) = 0;
    virtual bool validate_token(const core::PlexToken& token) = 0;
    virtual std::expected<core::PlexToken, core::PlexError> ensure_authenticated() = 0;

    // Header generation for authenticated requests
    virtual std::map<std::string, std::string> get_standard_headers(const core::PlexToken& token = {}) const = 0;

    // Shutdown method to abort ongoing operations
    virtual void shutdown() = 0;
};

// Concrete implementation
class PlexAuthenticator : public IPlexAuthenticator {
public:
    explicit PlexAuthenticator(std::shared_ptr<HttpClient> http_client,
                              std::shared_ptr<core::ConfigurationService> config_service);
    ~PlexAuthenticator() override = default;

    std::expected<core::PlexToken, core::PlexError> acquire_auth_token() override;
    std::expected<std::string, core::PlexError> fetch_username(const core::PlexToken& token) override;
    bool validate_token(const core::PlexToken& token) override;
    std::expected<core::PlexToken, core::PlexError> ensure_authenticated() override;

    std::map<std::string, std::string> get_standard_headers(const core::PlexToken& token = {}) const override;

    void shutdown() override;

private:
    // Internal helper methods
    std::expected<std::pair<std::string, std::string>, core::PlexError> request_plex_pin();
    void open_authorization_url(const std::string& pin, const std::string& client_id);
    std::expected<core::PlexToken, core::PlexError> poll_for_pin_authorization(
        const std::string& pin_id,
        const std::string& pin
    );

    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<core::ConfigurationService> m_config_service;
    std::atomic<bool> m_shutting_down{false};
};

} // namespace services
} // namespace presence_for_plex
