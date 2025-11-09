#include "presence_for_plex/services/network/http_types.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/services/network/request_builder.hpp"
#include "presence_for_plex/utils/url_utils.hpp"

#include <algorithm>
#include <cctype>

namespace presence_for_plex {
namespace services {

// HttpRequest validation
bool HttpRequest::is_valid() const {
    return !url.empty() && utils::UrlUtils::is_valid_url(url);
}

// HttpResponse convenience methods
bool HttpResponse::is_success() const {
    return static_cast<int>(status_code) >= 200 && static_cast<int>(status_code) < 300;
}

bool HttpResponse::is_client_error() const {
    return static_cast<int>(status_code) >= 400 && static_cast<int>(status_code) < 500;
}

bool HttpResponse::is_server_error() const {
    return static_cast<int>(status_code) >= 500 && static_cast<int>(status_code) < 600;
}

std::optional<std::string> HttpResponse::get_header(const std::string& name) const {
    auto it = std::find_if(headers.begin(), headers.end(),
        [&name](const auto& pair) {
            return std::equal(pair.first.begin(), pair.first.end(),
                            name.begin(), name.end(),
                            [](char a, char b) {
                                return std::tolower(a) == std::tolower(b);
                            });
        });
    return it != headers.end() ? std::make_optional(it->second) : std::nullopt;
}

// HttpClientConfig validation
bool HttpClientConfig::is_valid() const {
    return default_timeout.count() > 0 &&
           connection_pool_size > 0 &&
           max_redirects >= 0;
}

// RequestBuilder implementation
RequestBuilder::RequestBuilder(const std::string& url) {
    m_request.url = url;
}

RequestBuilder& RequestBuilder::method(HttpMethod method) {
    m_request.method = method;
    return *this;
}

RequestBuilder& RequestBuilder::url(const std::string& url) {
    m_request.url = url;
    return *this;
}

RequestBuilder& RequestBuilder::header(const std::string& name, const std::string& value) {
    m_request.headers[name] = value;
    return *this;
}

RequestBuilder& RequestBuilder::headers(const HttpHeaders& headers) {
    for (const auto& [name, value] : headers) {
        m_request.headers[name] = value;
    }
    return *this;
}

RequestBuilder& RequestBuilder::body(const std::string& body) {
    m_request.body = body;
    return *this;
}

RequestBuilder& RequestBuilder::json_body(const std::string& json) {
    m_request.body = json;
    m_request.headers["Content-Type"] = "application/json";
    return *this;
}

RequestBuilder& RequestBuilder::timeout(std::chrono::seconds timeout) {
    m_request.timeout = timeout;
    return *this;
}

RequestBuilder& RequestBuilder::basic_auth(const std::string& username, const std::string& password) {
    m_request.basic_auth = username + ":" + password;
    return *this;
}

RequestBuilder& RequestBuilder::bearer_token(const std::string& token) {
    m_request.bearer_token = token;
    return *this;
}

RequestBuilder& RequestBuilder::follow_redirects(bool follow) {
    m_request.follow_redirects = follow;
    return *this;
}

RequestBuilder& RequestBuilder::verify_ssl(bool verify) {
    m_request.verify_ssl = verify;
    return *this;
}

HttpRequest RequestBuilder::build() const {
    return m_request;
}

} // namespace services
} // namespace presence_for_plex
