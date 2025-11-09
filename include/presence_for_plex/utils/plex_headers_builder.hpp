#pragma once

#include <string>
#include <map>

namespace presence_for_plex::utils {

/**
 * @brief Helper for building standard Plex API headers
 */
class PlexHeadersBuilder {
public:
    /**
     * @brief Create standard Plex headers without authentication
     *
     * @param client_identifier The unique client identifier
     * @return std::map<std::string, std::string> The headers map
     */
    static std::map<std::string, std::string> create_standard_headers(const std::string& client_identifier);

    /**
     * @brief Create Plex headers with authentication token
     *
     * @param client_identifier The unique client identifier
     * @param auth_token The Plex authentication token
     * @return std::map<std::string, std::string> The headers map
     */
    static std::map<std::string, std::string> create_authenticated_headers(
        const std::string& client_identifier,
        const std::string& auth_token
    );

    /**
     * @brief Get the application version string
     *
     * @return std::string The version (e.g., "1.0.0")
     */
    static std::string get_version();

    /**
     * @brief Get the platform string
     *
     * @return std::string The platform (e.g., "Windows", "Linux", "macOS")
     */
    static std::string get_platform();

private:
    static void add_common_headers(std::map<std::string, std::string>& headers, const std::string& client_identifier);
};

} // namespace presence_for_plex::utils
