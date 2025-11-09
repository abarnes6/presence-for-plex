#include "presence_for_plex/utils/plex_headers_builder.hpp"

namespace presence_for_plex::utils {

std::string PlexHeadersBuilder::get_version() {
    return "1.0.0";
}

std::string PlexHeadersBuilder::get_platform() {
#ifdef _WIN32
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

void PlexHeadersBuilder::add_common_headers(std::map<std::string, std::string>& headers, const std::string& client_identifier) {
    headers["X-Plex-Product"] = "Presence For Plex";
    headers["X-Plex-Version"] = get_version();
    headers["X-Plex-Client-Identifier"] = client_identifier;
    headers["X-Plex-Platform"] = get_platform();
    headers["X-Plex-Platform-Version"] = "1.0";
    headers["X-Plex-Device"] = "PC";
    headers["X-Plex-Device-Name"] = "Presence For Plex";
    headers["Accept"] = "application/json";
}

std::map<std::string, std::string> PlexHeadersBuilder::create_standard_headers(const std::string& client_identifier) {
    std::map<std::string, std::string> headers;
    add_common_headers(headers, client_identifier);
    return headers;
}

std::map<std::string, std::string> PlexHeadersBuilder::create_authenticated_headers(
    const std::string& client_identifier,
    const std::string& auth_token
) {
    std::map<std::string, std::string> headers;
    add_common_headers(headers, client_identifier);

    if (!auth_token.empty()) {
        headers["X-Plex-Token"] = auth_token;
    }

    return headers;
}

} // namespace presence_for_plex::utils
