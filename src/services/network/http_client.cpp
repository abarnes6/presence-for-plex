#include "presence_for_plex/services/network_service.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>

namespace presence_for_plex {
namespace services {

// HttpRequest validation
bool HttpRequest::is_valid() const {
    return !url.empty() && UrlUtils::is_valid_url(url);
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

// UrlUtils implementation
std::string UrlUtils::encode(const std::string& str) {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex;

    for (char c : str) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << std::uppercase;
            encoded << '%' << std::setw(2) << static_cast<int>(uc);
            encoded << std::nouppercase;
        }
    }

    return encoded.str();
}

std::string UrlUtils::decode(const std::string& str) {
    std::ostringstream decoded;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value;
            std::istringstream is(str.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                decoded << static_cast<char>(value);
                i += 2;
            } else {
                decoded << str[i];
            }
        } else if (str[i] == '+') {
            decoded << ' ';
        } else {
            decoded << str[i];
        }
    }
    return decoded.str();
}

std::string UrlUtils::join_path(const std::string& base, const std::string& path) {
    if (base.empty()) return path;
    if (path.empty()) return base;

    bool base_ends_with_slash = base.back() == '/';
    bool path_starts_with_slash = path.front() == '/';

    if (base_ends_with_slash && path_starts_with_slash) {
        return base + path.substr(1);
    } else if (!base_ends_with_slash && !path_starts_with_slash) {
        return base + "/" + path;
    } else {
        return base + path;
    }
}

std::unordered_map<std::string, std::string> UrlUtils::parse_query_string(const std::string& query) {
    std::unordered_map<std::string, std::string> params;
    std::istringstream iss(query);
    std::string pair;

    while (std::getline(iss, pair, '&')) {
        size_t pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = decode(pair.substr(0, pos));
            std::string value = decode(pair.substr(pos + 1));
            params[key] = value;
        }
    }

    return params;
}

std::string UrlUtils::build_query_string(const std::unordered_map<std::string, std::string>& params) {
    std::ostringstream oss;
    bool first = true;

    for (const auto& [key, value] : params) {
        if (!first) oss << "&";
        oss << encode(key) << "=" << encode(value);
        first = false;
    }

    return oss.str();
}

bool UrlUtils::is_valid_url(const std::string& url) {
    // Simple URL validation - check for scheme and host
    size_t scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos || scheme_pos == 0) {
        return false;
    }

    std::string scheme = url.substr(0, scheme_pos);
    return scheme == "http" || scheme == "https";
}

std::optional<std::string> UrlUtils::get_host(const std::string& url) {
    size_t scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) return std::nullopt;

    size_t host_start = scheme_pos + 3;
    size_t host_end = url.find_first_of(":/", host_start);
    if (host_end == std::string::npos) host_end = url.length();

    if (host_start >= host_end) return std::nullopt;

    return url.substr(host_start, host_end - host_start);
}

std::optional<int> UrlUtils::get_port(const std::string& url) {
    auto host = get_host(url);
    if (!host) return std::nullopt;

    size_t scheme_pos = url.find("://");
    size_t host_start = scheme_pos + 3;
    size_t port_pos = url.find(':', host_start + host->length());

    if (port_pos == std::string::npos) {
        // Return default ports
        std::string scheme = url.substr(0, scheme_pos);
        if (scheme == "http") return 80;
        if (scheme == "https") return 443;
        return std::nullopt;
    }

    size_t port_end = url.find('/', port_pos);
    if (port_end == std::string::npos) port_end = url.length();

    try {
        return std::stoi(url.substr(port_pos + 1, port_end - port_pos - 1));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> UrlUtils::get_scheme(const std::string& url) {
    size_t scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos || scheme_pos == 0) {
        return std::nullopt;
    }
    return url.substr(0, scheme_pos);
}

} // namespace services
} // namespace presence_for_plex
