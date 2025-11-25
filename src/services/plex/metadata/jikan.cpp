#include "presence_for_plex/services/plex/metadata/jikan.hpp"
#include "presence_for_plex/services/network/http_client.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/json_helper.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

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

Jikan::Jikan(std::shared_ptr<HttpClient> http_client)
    : m_http_client(std::move(http_client)) {
}

std::expected<std::string, core::PlexError> Jikan::fetch_artwork_url(
    const std::string& mal_id,
    core::MediaType type) {
    (void)type;
    LOG_DEBUG("Jikan", "Fetching artwork for MAL ID: " + mal_id);

    std::string url = std::string(JIKAN_API_URL) + "/" + mal_id;

    auto response = m_http_client->get(url, {});
    if (!response.has_value()) {
        LOG_ERROR("Jikan", "Failed to fetch data for ID: " + mal_id + " - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    if (!response.value().is_success()) {
        LOG_ERROR("Jikan", "Failed to fetch data for ID: " + mal_id);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response.value().body);
    if (!json_result) {
        LOG_ERROR("Jikan", "Error parsing response: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();
    if (utils::JsonHelper::has_field(json_response, "data")) {
        auto data = json_response["data"];
        if (utils::JsonHelper::has_field(data, "images")) {
            auto images = data["images"];
            if (utils::JsonHelper::has_field(images, "jpg")) {
                auto jpg = images["jpg"];
                if (utils::JsonHelper::has_field(jpg, "large_image_url")) {
                    std::string artwork_url = jpg["large_image_url"];
                    LOG_INFO("Jikan", "Found artwork for MAL ID " + mal_id + ": " + artwork_url);
                    return artwork_url;
                }
            }
        }
    }

    return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
}

std::expected<void, core::PlexError> Jikan::enrich_media_info(core::MediaInfo& info) {
    bool is_anime = std::any_of(info.genres.begin(), info.genres.end(),
        [](const std::string& genre) { return genre == "Anime"; });

    if (!is_anime) {
        return {};
    }

    if (info.mal_id.empty()) {
        std::string search_title = (info.type == core::MediaType::TVShow && !info.grandparent_title.empty())
            ? info.grandparent_title
            : info.title;

        LOG_DEBUG("Jikan", "Searching for anime: " + search_title);
        auto search_result = search_anime_by_title(search_title, info.year);
        if (search_result.has_value()) {
            info.mal_id = search_result.value();
        }
    }

    if (!info.mal_id.empty()) {
        auto artwork_result = fetch_artwork_url(info.mal_id, info.type);
        if (artwork_result.has_value()) {
            info.art_path = artwork_result.value();
            LOG_DEBUG("Jikan", "Set art_path: " + info.art_path);
        }
    }

    return {};
}

std::expected<std::string, core::PlexError> Jikan::search_anime_by_title(
    const std::string& title,
    int year) {

    std::string encoded_title = url_encode(title);
    std::string url = std::string(JIKAN_API_URL) + "?q=" + encoded_title;

    if (year > 0) {
        url += "&start_date=" + std::to_string(year) + "-01-01";
        url += "&end_date=" + std::to_string(year) + "-12-31";
    }

    auto response = m_http_client->get(url, {});
    if (!response.has_value()) {
        LOG_ERROR("Jikan", "Failed to search anime: " + title + " - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    if (!response.value().is_success()) {
        LOG_ERROR("Jikan", "Failed to search anime: " + title);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    auto json_result = utils::JsonHelper::safe_parse(response.value().body);
    if (!json_result) {
        LOG_ERROR("Jikan", "Error parsing search response: " + json_result.error());
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }

    auto json_response = json_result.value();
    if (utils::JsonHelper::has_array(json_response, "data")) {
        auto first_result = json_response["data"][0];
        auto mal_id_result = utils::JsonHelper::get_required<int>(first_result, "mal_id");
        if (mal_id_result) {
            std::string mal_id = std::to_string(mal_id_result.value());
            LOG_INFO("Jikan", "Found MAL ID for " + title + ": " + mal_id);
            return mal_id;
        }
    }

    return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
}

std::string Jikan::url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

} // namespace services
} // namespace presence_for_plex
