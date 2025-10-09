#include "presence_for_plex/services/plex/plex_authenticator.hpp"
#include "presence_for_plex/services/network_service.hpp"
#include "presence_for_plex/core/authentication_service.hpp"
#include "presence_for_plex/platform/browser_launcher.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <nlohmann/json.hpp>

namespace presence_for_plex {
namespace services {

using json = nlohmann::json;

PlexAuthenticator::PlexAuthenticator(std::shared_ptr<HttpClient> http_client,
                                   std::shared_ptr<core::AuthenticationService> auth_service,
                                   std::shared_ptr<platform::BrowserLauncher> browser_launcher)
    : m_http_client(std::move(http_client))
    , m_auth_service(std::move(auth_service))
    , m_browser_launcher(std::move(browser_launcher)) {

    // Create default browser launcher if none provided
    if (!m_browser_launcher) {
        m_browser_launcher = platform::create_browser_launcher();
    }
}

std::expected<core::PlexToken, core::PlexError> PlexAuthenticator::acquire_auth_token() {
    LOG_DEBUG("PlexAuthenticator", "acquire_auth_token() called");
    LOG_INFO("PlexAuthenticator", "Starting PIN-based authentication");

    // Request a PIN from Plex
    auto pin_result = request_plex_pin();
    if (!pin_result) {
        return std::unexpected<core::PlexError>(pin_result.error());
    }

    auto [pin_id, pin] = pin_result.value();
    std::string client_id = m_auth_service->get_plex_client_identifier();

    // Open browser for user authentication
    open_authorization_url(pin, client_id);

    // Poll for authorization completion
    auto token_result = poll_for_pin_authorization(pin_id, pin);
    if (!token_result) {
        return std::unexpected<core::PlexError>(token_result.error());
    }

    return token_result.value();
}

std::expected<std::string, core::PlexError> PlexAuthenticator::fetch_username(const core::PlexToken& token) {
    LOG_DEBUG("PlexAuthenticator", "fetch_username() called");
    auto headers_map = get_standard_headers(token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get("https://plex.tv/api/v2/user", headers);
    if (!response || !response->is_success()) {
        LOG_ERROR("PlexAuthenticator", "Failed to fetch user information. Status: " +
                      (response ? std::to_string(static_cast<int>(response->status_code)) : "No response"));
        if (response) {
            LOG_DEBUG("PlexAuthenticator", "Response body: " + response->body);
        }
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    // Check if response looks like JSON before parsing
    if (response->body.empty() || response->body[0] == '<') {
        LOG_ERROR("PlexAuthenticator", "Received XML/HTML instead of JSON for user info. Response: " +
                      response->body.substr(0, std::min(response->body.length(), size_t(200))));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    try {
        auto json_response = json::parse(response->body);

        if (!json_response.contains("username")) {
            LOG_ERROR("PlexAuthenticator", "User response missing username field");
            return std::unexpected<core::PlexError>(core::PlexError::ParseError);
        }

        std::string username = json_response["username"].get<std::string>();
        LOG_INFO("PlexAuthenticator", "Fetched username: " + username);
        return username;
    } catch (const std::exception& e) {
        LOG_ERROR("PlexAuthenticator", "Error parsing user response: " + std::string(e.what()));
        LOG_DEBUG("PlexAuthenticator", "Raw user response: " + response->body);
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }
}

std::expected<void, core::PlexError> PlexAuthenticator::validate_token(const core::PlexToken& token) {
    LOG_DEBUG("PlexAuthenticator", "validate_token() called");
    auto result = fetch_username(token);
    if (!result) {
        LOG_DEBUG("PlexAuthenticator", "Token validation failed: " + std::to_string(static_cast<int>(result.error())));
        return std::unexpected(result.error());
    }
    LOG_DEBUG("PlexAuthenticator", "Token validation succeeded");
    return {};
}

std::expected<core::PlexToken, core::PlexError> PlexAuthenticator::ensure_authenticated() {
    LOG_DEBUG("PlexAuthenticator", "ensure_authenticated() called");
    // Load stored token from configuration
    std::string stored_token_value = m_auth_service->get_plex_token();
    core::PlexToken stored_token(stored_token_value);
    LOG_DEBUG("PlexAuthenticator", "Loaded stored token from config (length: " + std::to_string(stored_token_value.length()) + ")");

    // Check if we have a stored token and if it's valid
    if (!stored_token.value.empty()) {
        auto validation_result = validate_token(stored_token);
        if (validation_result) {
            LOG_INFO("PlexAuthenticator", "Using stored valid token");
            return stored_token;
        } else {
            LOG_DEBUG("PlexAuthenticator", "Stored token validation failed: " + std::to_string(static_cast<int>(validation_result.error())));
        }
    }

    // No valid stored token, need to authenticate
    LOG_INFO("PlexAuthenticator", "No valid stored token, starting authentication flow");
    auto new_token = acquire_auth_token();

    // Save the new token if authentication was successful
    if (new_token.has_value()) {
        m_auth_service->set_plex_token(new_token.value().value);
        m_auth_service->save();
    }

    return new_token;
}

std::map<std::string, std::string> PlexAuthenticator::get_standard_headers(const core::PlexToken& token) const {
    LOG_DEBUG("PlexAuthenticator", "get_standard_headers() called with token length: " + std::to_string(token.value.length()));
    std::map<std::string, std::string> headers;
    headers["X-Plex-Product"] = "Presence For Plex";
    headers["X-Plex-Version"] = "1.0.0";
    headers["X-Plex-Client-Identifier"] = m_auth_service->get_plex_client_identifier();
    headers["X-Plex-Platform"] = "Linux";
    headers["X-Plex-Platform-Version"] = "1.0";
    headers["X-Plex-Device"] = "PC";
    headers["X-Plex-Device-Name"] = "Presence For Plex";
    headers["Accept"] = "application/json";
    headers["Content-Type"] = "application/x-www-form-urlencoded";

    if (!token.value.empty()) {
        headers["X-Plex-Token"] = token.value;
    }

    return headers;
}

void PlexAuthenticator::shutdown() {
    LOG_INFO("PlexAuthenticator", "Shutdown requested, aborting ongoing operations");
    m_shutting_down.store(true);
}

std::expected<std::pair<std::string, std::string>, core::PlexError> PlexAuthenticator::request_plex_pin() {
    LOG_DEBUG("PlexAuthenticator", "request_plex_pin() called");
    auto headers_map = get_standard_headers();
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    std::string data = "strong=true";
    auto response = m_http_client->post("https://plex.tv/api/v2/pins", data, headers);

    if (!response || !response->is_success()) {
        LOG_ERROR("PlexAuthenticator", "Failed to request PIN from Plex. Status: " +
                      (response ? std::to_string(static_cast<int>(response->status_code)) : "No response"));
        if (response) {
            LOG_DEBUG("PlexAuthenticator", "Response body: " + response->body);
        }
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    LOG_DEBUG("PlexAuthenticator", "PIN response: " + response->body);

    // Check if response looks like JSON before parsing
    if (response->body.empty() || response->body[0] == '<') {
        LOG_ERROR("PlexAuthenticator", "Received XML/HTML instead of JSON. Response: " +
                      response->body.substr(0, std::min(response->body.length(), size_t(200))));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    try {
        auto json_response = json::parse(response->body);

        // Check if the required fields exist
        if (!json_response.contains("code") || !json_response.contains("id")) {
            LOG_ERROR("PlexAuthenticator", "PIN response missing required fields");
            return std::unexpected<core::PlexError>(core::PlexError::ParseError);
        }

        std::string pin = json_response["code"].get<std::string>();
        std::string pin_id = std::to_string(json_response["id"].get<int>());

        LOG_INFO("PlexAuthenticator", "Got PIN: " + pin + " (ID: " + pin_id + ")");
        return std::make_pair(pin_id, pin);
    } catch (const std::exception& e) {
        LOG_ERROR("PlexAuthenticator", "Error parsing PIN response: " + std::string(e.what()));
        LOG_DEBUG("PlexAuthenticator", "Raw response: " + response->body);
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }
}

void PlexAuthenticator::open_authorization_url(const std::string& pin, const std::string& client_id) {
    std::string auth_url = "https://app.plex.tv/auth#" +
                          std::string("?clientID=") + client_id +
                          "&code=" + pin +
                          "&context%5Bdevice%5D%5Bproduct%5D=Presence%20For%20Plex";

    LOG_INFO("PlexAuthenticator", "Opening browser for authentication: " + auth_url);

    // Use browser launcher to show message and open URL
    if (m_browser_launcher) {
        m_browser_launcher->show_message(
            "Plex Authentication Required",
            "A browser window will open for Plex authentication.\n\n"
            "Please log in to your Plex account and authorize Presence For Plex.\n\n"
            "The application will continue setup after successful authentication."
        );

        auto result = m_browser_launcher->open_url(auth_url);
        if (!result) {
            LOG_ERROR("PlexAuthenticator", "Failed to open browser for authentication");
        }
    } else {
        LOG_ERROR("PlexAuthenticator", "No browser launcher available");
    }
}

std::expected<core::PlexToken, core::PlexError> PlexAuthenticator::poll_for_pin_authorization(
        const std::string& pin_id,
        const std::string& pin) {
    (void)pin;
    const int max_attempts = 30;  // Try for about 5 minutes
    const int poll_interval = 10; // seconds

    LOG_INFO("PlexAuthenticator", "Waiting for user to authorize PIN...");

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        if (m_shutting_down) {
            LOG_INFO("PlexAuthenticator", "Application is shutting down, aborting PIN authorization");
            return std::unexpected<core::PlexError>(core::PlexError::Timeout);
        }

        // Wait before polling, but check shutdown flag every 100ms for responsiveness
        for (int i = 0; i < poll_interval * 10; ++i) {
            if (m_shutting_down) {
                LOG_INFO("PlexAuthenticator", "Application is shutting down, aborting PIN authorization");
                return std::unexpected<core::PlexError>(core::PlexError::Timeout);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Check PIN status
        std::string status_url = "https://plex.tv/api/v2/pins/" + pin_id;

        auto headers_map = get_standard_headers();
        HttpHeaders headers(headers_map.begin(), headers_map.end());
        auto response = m_http_client->get(status_url, headers);

        if (!response || !response->is_success()) {
            LOG_DEBUG("PlexAuthenticator", "PIN status check failed, retrying... Status: " +
                          (response ? std::to_string(static_cast<int>(response->status_code)) : "No response"));
            continue;
        }

        // Check if response looks like JSON before parsing
        if (response->body.empty() || response->body[0] == '<') {
            LOG_DEBUG("PlexAuthenticator", "Received XML/HTML instead of JSON during PIN polling, retrying...");
            continue;
        }

        try {
            auto json_response = json::parse(response->body);
            std::string auth_token = json_response.value("authToken", "");

            if (!auth_token.empty()) {
                LOG_INFO("PlexAuthenticator", "PIN authorized successfully!");
                return core::PlexToken{auth_token};
            }
        } catch (const std::exception& e) {
            LOG_DEBUG("PlexAuthenticator", "Error parsing PIN status: " + std::string(e.what()));
            LOG_DEBUG("PlexAuthenticator", "Raw polling response: " + response->body.substr(0, std::min(response->body.length(), size_t(200))));
        }
    }

    LOG_ERROR("PlexAuthenticator", "PIN authorization timed out");
    return std::unexpected<core::PlexError>(core::PlexError::AuthenticationError);
}

} // namespace services
} // namespace presence_for_plex
