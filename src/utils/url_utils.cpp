#include "presence_for_plex/utils/url_utils.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>

namespace presence_for_plex {
namespace utils {

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

} // namespace utils
} // namespace presence_for_plex
