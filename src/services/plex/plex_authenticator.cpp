#include "presence_for_plex/services/plex/plex_authenticator.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/services/plex/plex_auth_storage.hpp"
#include "presence_for_plex/platform/browser_launcher.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/json_helper.hpp"
#include "presence_for_plex/utils/plex_headers_builder.hpp"
#include <nlohmann/json.hpp>

namespace presence_for_plex {
namespace services {

using json = nlohmann::json;

PlexAuthenticator::PlexAuthenticator(std::shared_ptr<HttpClient> http_client,
                                   std::shared_ptr<PlexAuthStorage> auth_service,
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
    auto json_result = utils::JsonHelper::safe_parse(response->body);
    if (!json_result) {
        LOG_ERROR("PlexAuthenticator", "Failed to parse user info: " + json_result.error());
        LOG_DEBUG("PlexAuthenticator", "Raw user response: " + response->body.substr(0, std::min(response->body.length(), size_t(200))));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto username_result = utils::JsonHelper::get_required<std::string>(json_result.value(), "username");
    if (!username_result) {
        LOG_ERROR("PlexAuthenticator", username_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    LOG_INFO("PlexAuthenticator", "Fetched username: " + username_result.value());
    return username_result.value();
}

std::expected<std::string, core::PlexError> PlexAuthenticator::validate_token(const core::PlexToken& token) {
    LOG_DEBUG("PlexAuthenticator", "validate_token() called");
    auto result = fetch_username(token);
    if (!result) {
        LOG_DEBUG("PlexAuthenticator", "Token validation failed: " + std::to_string(static_cast<int>(result.error())));
        return std::unexpected(result.error());
    }
    LOG_DEBUG("PlexAuthenticator", "Token validation succeeded for user: " + result.value());
    return result.value();
}

std::expected<std::pair<core::PlexToken, std::string>, core::PlexError> PlexAuthenticator::ensure_authenticated(bool skip_validation) {
    LOG_DEBUG("PlexAuthenticator", "ensure_authenticated() called (skip_validation=" + std::string(skip_validation ? "true" : "false") + ")");

    // Load stored token from configuration
    std::string stored_token_value = m_auth_service->get_plex_token();
    core::PlexToken stored_token(stored_token_value);
    LOG_DEBUG("PlexAuthenticator", "Loaded stored token from config (length: " + std::to_string(stored_token_value.length()) + ")");

    // Check if we have a stored token
    if (!stored_token.empty()) {
        if (skip_validation) {
            // Optimistic approach: assume token is valid, skip validation
            LOG_INFO("PlexAuthenticator", "Using stored token optimistically");
            return std::make_pair(stored_token, std::string{});  // Empty username for now
        } else {
            // Validate the token
            auto validation_result = validate_token(stored_token);
            if (validation_result) {
                LOG_INFO("PlexAuthenticator", "Using stored valid token for user: " + validation_result.value());
                return std::make_pair(stored_token, validation_result.value());
            } else {
                LOG_DEBUG("PlexAuthenticator", "Stored token validation failed: " + std::to_string(static_cast<int>(validation_result.error())));
            }
        }
    }

    // No valid stored token, need to authenticate
    LOG_INFO("PlexAuthenticator", "No valid stored token, starting authentication flow");
    auto new_token = acquire_auth_token();

    // Save the new token if authentication was successful
    if (new_token.has_value()) {
        m_auth_service->set_plex_token(new_token.value());
        m_auth_service->save();

        // Fetch username for the new token
        auto username_result = fetch_username(new_token.value());
        if (username_result) {
            return std::make_pair(new_token.value(), username_result.value());
        } else {
            // Token acquired but couldn't fetch username - still return token with empty username
            LOG_WARNING("PlexAuthenticator", "Token acquired but couldn't fetch username");
            return std::make_pair(new_token.value(), std::string{});
        }
    }

    return std::unexpected(new_token.error());
}

std::map<std::string, std::string> PlexAuthenticator::get_standard_headers(const core::PlexToken& token) const {
    LOG_DEBUG("PlexAuthenticator", "get_standard_headers() called with token length: " + std::to_string(token.length()));

    auto headers = utils::PlexHeadersBuilder::create_authenticated_headers(
        m_auth_service->get_plex_client_identifier(),
        token
    );

    // Add auth-specific headers
    headers["Content-Type"] = "application/x-www-form-urlencoded";

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

    auto json_result = utils::JsonHelper::safe_parse(response->body);
    if (!json_result) {
        LOG_ERROR("PlexAuthenticator", "Failed to parse PIN response: " + json_result.error());
        LOG_DEBUG("PlexAuthenticator", "Raw response: " + response->body.substr(0, std::min(response->body.length(), size_t(200))));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    auto code_result = utils::JsonHelper::get_required<std::string>(json_response, "code");
    if (!code_result) {
        LOG_ERROR("PlexAuthenticator", "PIN response missing code field: " + code_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto id_result = utils::JsonHelper::get_required<int>(json_response, "id");
    if (!id_result) {
        LOG_ERROR("PlexAuthenticator", "PIN response missing id field: " + id_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    std::string pin = code_result.value();
    std::string pin_id = std::to_string(id_result.value());

    LOG_INFO("PlexAuthenticator", "Got PIN: " + pin + " (ID: " + pin_id + ")");
    return std::make_pair(pin_id, pin);
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

        auto json_result = utils::JsonHelper::safe_parse(response->body);
        if (!json_result) {
            LOG_DEBUG("PlexAuthenticator", "Failed to parse PIN status: " + json_result.error() + ", retrying...");
            LOG_DEBUG("PlexAuthenticator", "Raw polling response: " + response->body.substr(0, std::min(response->body.length(), size_t(200))));
            continue;
        }

        auto json_response = json_result.value();
        std::string auth_token = utils::JsonHelper::get_optional<std::string>(json_response, "authToken", "");

        if (!auth_token.empty()) {
            LOG_INFO("PlexAuthenticator", "PIN authorized successfully!");
            return core::PlexToken{auth_token};
        }
    }

    LOG_ERROR("PlexAuthenticator", "PIN authorization timed out");
    return std::unexpected<core::PlexError>(core::PlexError::AuthenticationError);
}

} // namespace services
} // namespace presence_for_plex
