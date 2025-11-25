#pragma once

#include "presence_for_plex/core/models.hpp"
#include <string>
#include <memory>
#include <map>
#include <expected>
#include <atomic>
#include <shared_mutex>
#include <filesystem>

namespace presence_for_plex {
namespace platform {
    class BrowserLauncher;
}
namespace services {

class HttpClient;

class PlexAuth {
public:
    explicit PlexAuth(std::shared_ptr<HttpClient> http_client,
                      std::shared_ptr<platform::BrowserLauncher> browser_launcher = nullptr,
                      const std::filesystem::path& storage_path = {});
    ~PlexAuth() = default;

    // Authentication flow
    std::expected<core::PlexToken, core::PlexError> acquire_auth_token();
    std::expected<std::string, core::PlexError> fetch_username(const core::PlexToken& token);
    std::expected<std::string, core::PlexError> validate_token(const core::PlexToken& token);
    std::expected<std::pair<core::PlexToken, std::string>, core::PlexError> ensure_authenticated(bool skip_validation = false);

    // Token/credential access
    std::string get_token() const;
    void set_token(const std::string& token);
    std::string get_client_identifier() const;
    std::string get_username() const;
    void set_username(const std::string& username);

    // HTTP headers
    std::map<std::string, std::string> get_standard_headers(const core::PlexToken& token = {}) const;

    // Lifecycle
    void shutdown();
    void save();
    void load();

private:
    // OAuth flow helpers
    std::expected<std::pair<std::string, std::string>, core::PlexError> request_plex_pin();
    void open_authorization_url(const std::string& pin, const std::string& client_id);
    std::expected<core::PlexToken, core::PlexError> poll_for_pin_authorization(
        const std::string& pin_id,
        const std::string& pin
    );

    // Storage helpers
    static std::filesystem::path get_default_auth_path();
    void ensure_storage_directory();
    void generate_client_identifier();
    void save_internal();
    void load_internal();

    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<platform::BrowserLauncher> m_browser_launcher;
    std::atomic<bool> m_shutting_down{false};

    // Stored credentials
    mutable std::shared_mutex m_mutex;
    std::filesystem::path m_storage_path;
    std::string m_token;
    std::string m_client_identifier;
    std::string m_username;
};

} // namespace services
} // namespace presence_for_plex
