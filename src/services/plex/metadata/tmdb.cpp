#include "presence_for_plex/services/plex/metadata/tmdb.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/json_helper.hpp"

namespace presence_for_plex {
namespace services {

static std::string network_error_to_string(NetworkError error) {
    switch (error) {
        case NetworkError::ConnectionFailed: return "Connection failed";
        case NetworkError::Timeout: return "Timeout";
        case NetworkError::DNSResolutionFailed: return "DNS resolution failed";
        case NetworkError::SSLError: return "SSL error";
        case NetworkError::InvalidUrl: return "Invalid URL";
        case NetworkError::TooManyRedirects: return "Too many redirects";
        case NetworkError::BadResponse: return "Bad response";
        case NetworkError::Cancelled: return "Cancelled";
        default: return "Unknown error";
    }
}

TMDB::TMDB(std::shared_ptr<HttpClient> http_client, const std::string& access_token)
    : m_http_client(std::move(http_client))
    , m_access_token(access_token) {
}

std::expected<std::string, core::PlexError> TMDB::fetch_artwork_url(
    const std::string& tmdb_id,
    core::MediaType type) {

    LOG_DEBUG("TMDB", "Fetching artwork for ID: " + tmdb_id);

    if (m_access_token.empty()) {
        LOG_DEBUG("TMDB", "No access token available");
        return std::unexpected<core::PlexError>(core::PlexError::AuthenticationError);
    }

    std::string url;
    if (type == core::MediaType::Movie) {
        url = "https://api.themoviedb.org/3/movie/" + tmdb_id + "/images";
    } else {
        url = "https://api.themoviedb.org/3/tv/" + tmdb_id + "/images";
    }

    HttpHeaders headers = {
        {"Authorization", "Bearer " + m_access_token},
        {"Accept", "application/json"}
    };

    auto response = m_http_client->get(url, headers);
    if (!response.has_value()) {
        LOG_ERROR("TMDB", "Failed to fetch images for ID: " + tmdb_id + " - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    if (!response.value().is_success()) {
        LOG_ERROR("TMDB", "Failed to fetch images for ID: " + tmdb_id);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response.value().body);
    if (!json_result) {
        LOG_ERROR("TMDB", "Error parsing response: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();

    if (utils::JsonHelper::has_array(json_response, "posters")) {
        std::string poster_path = json_response["posters"][0]["file_path"];
        std::string full_url = std::string(TMDB_IMAGE_BASE_URL) + poster_path;
        LOG_INFO("TMDB", "Found poster for ID " + tmdb_id + ": " + full_url);
        return full_url;
    }
    else if (utils::JsonHelper::has_array(json_response, "backdrops")) {
        std::string backdrop_path = json_response["backdrops"][0]["file_path"];
        std::string full_url = std::string(TMDB_IMAGE_BASE_URL) + backdrop_path;
        LOG_INFO("TMDB", "Found backdrop for ID " + tmdb_id + ": " + full_url);
        return full_url;
    }

    return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
}

std::expected<void, core::PlexError> TMDB::enrich_media_info(core::MediaInfo& info) {
    if (info.tmdb_id.empty()) {
        return {};
    }

    auto artwork_result = fetch_artwork_url(info.tmdb_id, info.type);
    if (artwork_result.has_value()) {
        info.art_path = artwork_result.value();
        LOG_DEBUG("TMDB", "Set art_path: " + info.art_path);
    }

    return {};
}

} // namespace services
} // namespace presence_for_plex
