#include "presence_for_plex/services/plex/plex_auth.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/platform/browser_launcher.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/json_helper.hpp"
#include "presence_for_plex/utils/plex_headers_builder.hpp"
#include "presence_for_plex/utils/uuid.hpp"
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <fstream>

namespace presence_for_plex {
namespace services {

using json = nlohmann::json;

PlexAuth::PlexAuth(std::shared_ptr<HttpClient> http_client,
                   std::shared_ptr<platform::BrowserLauncher> browser_launcher,
                   const std::filesystem::path& storage_path)
    : m_http_client(std::move(http_client))
    , m_browser_launcher(std::move(browser_launcher))
    , m_storage_path(storage_path.empty() ? get_default_auth_path() : storage_path) {

    if (!m_browser_launcher) {
        m_browser_launcher = platform::create_browser_launcher();
    }

    ensure_storage_directory();
    load();
}

// Authentication flow

std::expected<core::PlexToken, core::PlexError> PlexAuth::acquire_auth_token() {
    LOG_DEBUG("PlexAuth", "Starting PIN-based authentication");

    auto pin_result = request_plex_pin();
    if (!pin_result) {
        return std::unexpected<core::PlexError>(pin_result.error());
    }

    auto [pin_id, pin] = pin_result.value();
    std::string client_id = get_client_identifier();

    open_authorization_url(pin, client_id);

    auto token_result = poll_for_pin_authorization(pin_id, pin);
    if (!token_result) {
        return std::unexpected<core::PlexError>(token_result.error());
    }

    return token_result.value();
}

std::expected<std::string, core::PlexError> PlexAuth::fetch_username(const core::PlexToken& token) {
    auto headers_map = get_standard_headers(token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get("https://plex.tv/api/v2/user", headers);
    if (!response || !response->is_success()) {
        LOG_ERROR("PlexAuth", "Failed to fetch user information");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response->body);
    if (!json_result) {
        LOG_ERROR("PlexAuth", "Failed to parse user info: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto username_result = utils::JsonHelper::get_required<std::string>(json_result.value(), "username");
    if (!username_result) {
        LOG_ERROR("PlexAuth", username_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    LOG_INFO("PlexAuth", "Fetched username: " + username_result.value());
    return username_result.value();
}

std::expected<std::string, core::PlexError> PlexAuth::validate_token(const core::PlexToken& token) {
    auto result = fetch_username(token);
    if (!result) {
        LOG_DEBUG("PlexAuth", "Token validation failed");
        return std::unexpected(result.error());
    }
    LOG_DEBUG("PlexAuth", "Token validated for user: " + result.value());
    return result.value();
}

std::expected<std::pair<core::PlexToken, std::string>, core::PlexError> PlexAuth::ensure_authenticated(bool skip_validation) {
    std::string stored_token_value = get_token();
    core::PlexToken stored_token(stored_token_value);

    if (!stored_token.empty()) {
        if (skip_validation) {
            LOG_INFO("PlexAuth", "Using stored token optimistically");
            return std::make_pair(stored_token, std::string{});
        }

        auto validation_result = validate_token(stored_token);
        if (validation_result) {
            LOG_INFO("PlexAuth", "Using stored valid token for user: " + validation_result.value());
            return std::make_pair(stored_token, validation_result.value());
        }
    }

    LOG_INFO("PlexAuth", "No valid stored token, starting authentication flow");
    auto new_token = acquire_auth_token();

    if (new_token.has_value()) {
        set_token(new_token.value());

        auto username_result = fetch_username(new_token.value());
        if (username_result) {
            return std::make_pair(new_token.value(), username_result.value());
        }
        LOG_WARNING("PlexAuth", "Token acquired but couldn't fetch username");
        return std::make_pair(new_token.value(), std::string{});
    }

    return std::unexpected(new_token.error());
}

// Token/credential access

std::string PlexAuth::get_token() const {
    std::shared_lock lock(m_mutex);
    return m_token;
}

void PlexAuth::set_token(const std::string& token) {
    {
        std::unique_lock lock(m_mutex);
        m_token = token;
    }
    save();
}

std::string PlexAuth::get_client_identifier() const {
    std::shared_lock lock(m_mutex);
    if (m_client_identifier.empty()) {
        lock.unlock();
        const_cast<PlexAuth*>(this)->generate_client_identifier();
        lock.lock();
    }
    return m_client_identifier;
}

std::string PlexAuth::get_username() const {
    std::shared_lock lock(m_mutex);
    return m_username;
}

void PlexAuth::set_username(const std::string& username) {
    {
        std::unique_lock lock(m_mutex);
        m_username = username;
    }
    save();
}

// HTTP headers

std::map<std::string, std::string> PlexAuth::get_standard_headers(const core::PlexToken& token) const {
    auto headers = utils::PlexHeadersBuilder::create_authenticated_headers(
        get_client_identifier(),
        token
    );
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    return headers;
}

// Lifecycle

void PlexAuth::shutdown() {
    LOG_INFO("PlexAuth", "Shutdown requested");
    m_shutting_down.store(true);
}

void PlexAuth::save() {
    std::shared_lock lock(m_mutex);
    save_internal();
}

void PlexAuth::load() {
    std::unique_lock lock(m_mutex);
    load_internal();
}

// OAuth flow helpers

std::expected<std::pair<std::string, std::string>, core::PlexError> PlexAuth::request_plex_pin() {
    auto headers_map = get_standard_headers();
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    std::string data = "strong=true";
    auto response = m_http_client->post("https://plex.tv/api/v2/pins", data, headers);

    if (!response || !response->is_success()) {
        LOG_ERROR("PlexAuth", "Failed to request PIN from Plex");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response->body);
    if (!json_result) {
        LOG_ERROR("PlexAuth", "Failed to parse PIN response: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    auto code_result = utils::JsonHelper::get_required<std::string>(json_response, "code");
    if (!code_result) {
        LOG_ERROR("PlexAuth", "PIN response missing code field");
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto id_result = utils::JsonHelper::get_required<int>(json_response, "id");
    if (!id_result) {
        LOG_ERROR("PlexAuth", "PIN response missing id field");
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    std::string pin = code_result.value();
    std::string pin_id = std::to_string(id_result.value());

    LOG_INFO("PlexAuth", "Got PIN: " + pin + " (ID: " + pin_id + ")");
    return std::make_pair(pin_id, pin);
}

void PlexAuth::open_authorization_url(const std::string& pin, const std::string& client_id) {
    std::string auth_url = "https://app.plex.tv/auth#" +
                          std::string("?clientID=") + client_id +
                          "&code=" + pin +
                          "&context%5Bdevice%5D%5Bproduct%5D=Presence%20For%20Plex";

    LOG_INFO("PlexAuth", "Opening browser for authentication");

    if (m_browser_launcher) {
        m_browser_launcher->show_message(
            "Plex Authentication Required",
            "A browser window will open for Plex authentication.\n\n"
            "Please log in to your Plex account and authorize Presence For Plex.\n\n"
            "The application will continue setup after successful authentication."
        );

        auto result = m_browser_launcher->open_url(auth_url);
        if (!result) {
            LOG_ERROR("PlexAuth", "Failed to open browser for authentication");
        }
    } else {
        LOG_ERROR("PlexAuth", "No browser launcher available");
    }
}

std::expected<core::PlexToken, core::PlexError> PlexAuth::poll_for_pin_authorization(
        const std::string& pin_id,
        const std::string& pin) {
    (void)pin;
    const int max_attempts = 30;
    const int poll_interval = 10;

    LOG_INFO("PlexAuth", "Waiting for user to authorize PIN...");

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        if (m_shutting_down) {
            LOG_INFO("PlexAuth", "Shutdown requested, aborting PIN authorization");
            return std::unexpected<core::PlexError>(core::PlexError::Timeout);
        }

        for (int i = 0; i < poll_interval * 10; ++i) {
            if (m_shutting_down) {
                LOG_INFO("PlexAuth", "Shutdown requested, aborting PIN authorization");
                return std::unexpected<core::PlexError>(core::PlexError::Timeout);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::string status_url = "https://plex.tv/api/v2/pins/" + pin_id;

        auto headers_map = get_standard_headers();
        HttpHeaders headers(headers_map.begin(), headers_map.end());
        auto response = m_http_client->get(status_url, headers);

        if (!response || !response->is_success()) {
            continue;
        }

        auto json_result = utils::JsonHelper::safe_parse(response->body);
        if (!json_result) {
            continue;
        }

        auto json_response = json_result.value();
        std::string auth_token = utils::JsonHelper::get_optional<std::string>(json_response, "authToken", "");

        if (!auth_token.empty()) {
            LOG_INFO("PlexAuth", "PIN authorized successfully!");
            return core::PlexToken{auth_token};
        }
    }

    LOG_ERROR("PlexAuth", "PIN authorization timed out");
    return std::unexpected<core::PlexError>(core::PlexError::AuthenticationError);
}

// Storage helpers

std::filesystem::path PlexAuth::get_default_auth_path() {
    std::filesystem::path auth_dir;

#ifdef _WIN32
    if (const char* app_data = std::getenv("APPDATA")) {
        auth_dir = std::filesystem::path(app_data) / "Presence For Plex";
    }
#else
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
        auth_dir = std::filesystem::path(xdg_config) / "presence-for-plex";
    } else if (const char* home = std::getenv("HOME")) {
        auth_dir = std::filesystem::path(home) / ".config" / "presence-for-plex";
    }
#endif

    return auth_dir / "auth.yaml";
}

void PlexAuth::ensure_storage_directory() {
    auto dir = m_storage_path.parent_path();
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
        LOG_DEBUG("PlexAuth", "Created storage directory: " + dir.string());
    }
}

void PlexAuth::generate_client_identifier() {
    LOG_INFO("PlexAuth", "Generating new client identifier");
    std::string id = utils::generate_uuid_v4();

    {
        std::unique_lock lock(m_mutex);
        m_client_identifier = id;
    }

    save();
}

void PlexAuth::save_internal() {
    try {
        YAML::Node node;

        if (!m_token.empty()) {
            node["plex"]["auth_token"] = m_token;
        }
        if (!m_client_identifier.empty()) {
            node["plex"]["client_identifier"] = m_client_identifier;
        }
        if (!m_username.empty()) {
            node["plex"]["username"] = m_username;
        }

        std::ofstream file(m_storage_path);
        if (!file) {
            LOG_ERROR("PlexAuth", "Failed to open auth file for writing");
            return;
        }

        file << node;
        LOG_DEBUG("PlexAuth", "Saved authentication data");
    } catch (const std::exception& e) {
        LOG_ERROR("PlexAuth", "Error saving auth data: " + std::string(e.what()));
    }
}

void PlexAuth::load_internal() {
    if (!std::filesystem::exists(m_storage_path)) {
        LOG_DEBUG("PlexAuth", "Auth file does not exist, using defaults");
        return;
    }

    try {
        YAML::Node node = YAML::LoadFile(m_storage_path.string());

        if (node["plex"]) {
            auto plex = node["plex"];
            if (plex["auth_token"]) {
                m_token = plex["auth_token"].as<std::string>();
            }
            if (plex["client_identifier"]) {
                m_client_identifier = plex["client_identifier"].as<std::string>();
            }
            if (plex["username"]) {
                m_username = plex["username"].as<std::string>();
            }
        }

        LOG_DEBUG("PlexAuth", "Loaded authentication data");
    } catch (const std::exception& e) {
        LOG_ERROR("PlexAuth", "Error loading auth data: " + std::string(e.what()));
    }
}

} // namespace services
} // namespace presence_for_plex
