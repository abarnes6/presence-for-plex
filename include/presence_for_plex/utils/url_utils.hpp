#pragma once

#include <string>
#include <unordered_map>
#include <optional>

namespace presence_for_plex {
namespace utils {

// URL utilities
class UrlUtils {
public:
    static std::string encode(const std::string& str);
    static std::string decode(const std::string& str);
    static std::string join_path(const std::string& base, const std::string& path);
    static std::unordered_map<std::string, std::string> parse_query_string(const std::string& query);
    static std::string build_query_string(const std::unordered_map<std::string, std::string>& params);
    static bool is_valid_url(const std::string& url);
    static std::optional<std::string> get_host(const std::string& url);
    static std::optional<int> get_port(const std::string& url);
    static std::optional<std::string> get_scheme(const std::string& url);
};

} // namespace utils
} // namespace presence_for_plex
