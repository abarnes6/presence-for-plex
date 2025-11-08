#include "presence_for_plex/services/plex/plex_media_fetcher.hpp"
#include "presence_for_plex/services/plex/plex_cache_manager.hpp"
#include "presence_for_plex/services/network_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <expected>

namespace presence_for_plex {
namespace services {

using json = nlohmann::json;

// Helper function to convert NetworkError to string
static std::string network_error_to_string(NetworkError error) {
    switch (error) {
        case NetworkError::ConnectionFailed:
            return "Connection failed";
        case NetworkError::Timeout:
            return "Timeout";
        case NetworkError::DNSResolutionFailed:
            return "DNS resolution failed";
        case NetworkError::SSLError:
            return "SSL error";
        case NetworkError::InvalidUrl:
            return "Invalid URL";
        case NetworkError::TooManyRedirects:
            return "Too many redirects";
        case NetworkError::BadResponse:
            return "Bad response";
        case NetworkError::Cancelled:
            return "Cancelled";
        default:
            return "Unknown error";
    }
}

// TMDBService implementation
TMDBService::TMDBService(std::shared_ptr<HttpClient> http_client, const std::string& access_token)
    : m_http_client(std::move(http_client))
    , m_access_token(access_token) {
}

std::expected<std::string, core::PlexError> TMDBService::fetch_artwork_url(
    const std::string& tmdb_id,
    core::MediaType type) {

    LOG_DEBUG("TMDB", "fetch_artwork_url() called for ID: " + tmdb_id);

    if (m_access_token.empty()) {
        LOG_DEBUG("TMDB", "No TMDB access token available");
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
        LOG_ERROR("TMDB", "Failed to fetch TMDB images for ID: " + tmdb_id + " - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }
    
    if (!response.value().is_success()) {
        LOG_ERROR("TMDB", "Failed to fetch TMDB images for ID: " + tmdb_id);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    try {
        auto json_response = json::parse(response.value().body);

        // First try to get a poster
        if (json_response.contains("posters") && !json_response["posters"].empty()) {
            std::string poster_path = json_response["posters"][0]["file_path"];
            std::string full_url = std::string(TMDB_IMAGE_BASE_URL) + poster_path;
            LOG_INFO("TMDB", "Found poster for ID " + tmdb_id + ": " + full_url);
            return full_url;
        }
        // Fallback to backdrops
        else if (json_response.contains("backdrops") && !json_response["backdrops"].empty()) {
            std::string backdrop_path = json_response["backdrops"][0]["file_path"];
            std::string full_url = std::string(TMDB_IMAGE_BASE_URL) + backdrop_path;
            LOG_INFO("TMDB", "Found backdrop for ID " + tmdb_id + ": " + full_url);
            return full_url;
        }

        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    } catch (const std::exception& e) {
        LOG_ERROR("TMDB", "Error parsing TMDB response: " + std::string(e.what()));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }
}

std::expected<void, core::PlexError> TMDBService::enrich_media_info(core::MediaInfo& info) {
    LOG_DEBUG("TMDB", "enrich_media_info() called for: " + info.title);
    if (info.tmdb_id.empty()) {
        LOG_DEBUG("TMDB", "No TMDB ID available for enrichment");
        return {}; // Nothing to enrich
    }

    auto artwork_result = fetch_artwork_url(info.tmdb_id, info.type);
    if (artwork_result.has_value()) {
        info.art_path = artwork_result.value();
        LOG_DEBUG("TMDB", "Set art_path from TMDB: " + info.art_path);
    } else {
        LOG_WARNING("TMDB", "Failed to fetch artwork for TMDB ID: " + info.tmdb_id);
    }

    return {};
}

// JikanService implementation
JikanService::JikanService(std::shared_ptr<HttpClient> http_client)
    : m_http_client(std::move(http_client)) {
}

std::expected<std::string, core::PlexError> JikanService::fetch_artwork_url(
    const std::string& mal_id,
    core::MediaType type) {
	(void)type;
    LOG_DEBUG("Jikan", "fetch_artwork_url() called for MAL ID: " + mal_id);

    // MAL ID directly provides artwork through the anime detail endpoint
    std::string url = std::string(JIKAN_API_URL) + "/" + mal_id;

    auto response = m_http_client->get(url, {});
    if (!response.has_value()) {
        LOG_ERROR("Jikan", "Failed to fetch MAL data for ID: " + mal_id + " - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }
    
    if (!response.value().is_success()) {
        LOG_ERROR("Jikan", "Failed to fetch MAL data for ID: " + mal_id);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    try {
        auto json_response = json::parse(response.value().body);
        if (json_response.contains("data") && json_response["data"].contains("images")) {
            auto images = json_response["data"]["images"];
            if (images.contains("jpg") && images["jpg"].contains("large_image_url")) {
                std::string artwork_url = images["jpg"]["large_image_url"];
                LOG_INFO("Jikan", "Found artwork for MAL ID " + mal_id + ": " + artwork_url);
                return artwork_url;
            }
        }

        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    } catch (const std::exception& e) {
        LOG_ERROR("Jikan", "Error parsing Jikan response: " + std::string(e.what()));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }
}

std::expected<void, core::PlexError> JikanService::enrich_media_info(core::MediaInfo& info) {
    LOG_DEBUG("Jikan", "enrich_media_info() called for: " + info.title);
    // Check if this is anime content
    bool is_anime = std::any_of(info.genres.begin(), info.genres.end(),
        [](const std::string& genre) { return genre == "Anime"; });
    LOG_DEBUG("Jikan", std::string("Is anime content: ") + (is_anime ? "true" : "false"));

    if (!is_anime) {
        return {}; // Not anime, nothing to enrich
    }

    // If we don't have MAL ID, try to search for it
    if (info.mal_id.empty()) {
        // For TV shows, use the show title (grandparent_title) instead of episode title
        std::string search_title = (info.type == core::MediaType::TVShow && !info.grandparent_title.empty())
            ? info.grandparent_title
            : info.title;

        LOG_DEBUG("Jikan", "Searching for anime with title: " + search_title);
        auto search_result = search_anime_by_title(search_title, info.year);
        if (search_result.has_value()) {
            info.mal_id = search_result.value();
        }
    }

    // Fetch artwork if we have MAL ID
    if (!info.mal_id.empty()) {
        auto artwork_result = fetch_artwork_url(info.mal_id, info.type);
        if (artwork_result.has_value()) {
            info.art_path = artwork_result.value();
            LOG_DEBUG("Jikan", "Set art_path from Jikan: " + info.art_path);
        } else {
            LOG_WARNING("Jikan", "Failed to fetch artwork for MAL ID: " + info.mal_id);
        }
    }

    return {};
}

std::expected<std::string, core::PlexError> JikanService::search_anime_by_title(
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

    try {
        auto json_response = json::parse(response.value().body);
        if (json_response.contains("data") && !json_response["data"].empty()) {
            auto first_result = json_response["data"][0];
            if (first_result.contains("mal_id")) {
                std::string mal_id = std::to_string(first_result["mal_id"].get<int>());
                LOG_INFO("Jikan", "Found MAL ID for " + title + ": " + mal_id);
                return mal_id;
            }
        }

        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    } catch (const std::exception& e) {
        LOG_ERROR("Jikan", "Error parsing search response: " + std::string(e.what()));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }
}

std::string JikanService::url_encode(const std::string& value) {
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

// MovieExtractor implementation
bool MovieExtractor::can_extract(const nlohmann::json& metadata) const {
    return metadata.value("type", "") == "movie";
}

core::MediaInfo MovieExtractor::extract(const nlohmann::json& metadata) const {
    core::MediaInfo info;
    info.type = core::MediaType::Movie;

    extract_guids(metadata, info);
    extract_genres(metadata, info);

    return info;
}

void MovieExtractor::extract_guids(const nlohmann::json& metadata, core::MediaInfo& info) const {
    if (metadata.contains("Guid") && metadata["Guid"].is_array()) {
        for (const auto& guid : metadata["Guid"]) {
            std::string id = guid.value("id", "");
            if (id.find("imdb://") == 0) {
                info.imdb_id = id.substr(7);
                LOG_DEBUG("MovieExtractor", "Found IMDB ID: " + info.imdb_id);
            } else if (id.find("tmdb://") == 0) {
                info.tmdb_id = id.substr(7);
                LOG_DEBUG("MovieExtractor", "Found TMDB ID: " + info.tmdb_id);
            }
        }
    }
}

void MovieExtractor::extract_genres(const nlohmann::json& metadata, core::MediaInfo& info) const {
    if (metadata.contains("Genre") && metadata["Genre"].is_array()) {
        for (const auto& genre : metadata["Genre"]) {
            info.genres.push_back(genre.value("tag", ""));
        }
    }
}

// TVShowExtractor implementation
bool TVShowExtractor::can_extract(const nlohmann::json& metadata) const {
    return metadata.value("type", "") == "episode";
}

core::MediaInfo TVShowExtractor::extract(const nlohmann::json& metadata) const {
    core::MediaInfo info;
    info.type = core::MediaType::TVShow;

    extract_episode_info(metadata, info);

    return info;
}

void TVShowExtractor::extract_episode_info(const nlohmann::json& metadata, core::MediaInfo& info) const {
    info.grandparent_title = metadata.value("grandparentTitle", "Unknown");
    info.show_title = info.grandparent_title;
    info.season = metadata.value("parentIndex", 0);
    info.episode = metadata.value("index", 0);

    if (metadata.contains("grandparentKey")) {
        info.grandparent_key = metadata.value("grandparentKey", "");
    }

    LOG_DEBUG("TVShowExtractor", "Extracted show: " + info.grandparent_title +
                   " S" + std::to_string(info.season) + "E" + std::to_string(info.episode));
}

// MusicExtractor implementation
bool MusicExtractor::can_extract(const nlohmann::json& metadata) const {
    return metadata.value("type", "") == "track";
}

core::MediaInfo MusicExtractor::extract(const nlohmann::json& metadata) const {
    core::MediaInfo info;
    info.type = core::MediaType::Music;

    extract_track_info(metadata, info);

    return info;
}

void MusicExtractor::extract_track_info(const nlohmann::json& metadata, core::MediaInfo& info) const {
    info.album = metadata.value("parentTitle", "");
    info.artist = metadata.value("grandparentTitle", "");
    info.track = metadata.value("index", 0);

    LOG_DEBUG("MusicExtractor", "Extracted track: " + info.artist + " - " + info.album + " - " + info.title);
}

// PlexMediaFetcher implementation
PlexMediaFetcher::PlexMediaFetcher(
    std::shared_ptr<HttpClient> http_client,
    std::shared_ptr<PlexCacheManager> cache_manager)
    : m_http_client(std::move(http_client))
    , m_cache_manager(std::move(cache_manager)) {

    LOG_INFO("PlexMediaFetcher", "Creating media fetcher");
}

std::expected<core::MediaInfo, core::PlexError> PlexMediaFetcher::fetch_media_details(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    const std::string& media_key) {

    LOG_DEBUG("PlexMediaFetcher", "Fetching media details for key: " + media_key);

    // Check cache first
    std::string cache_key = server_uri + media_key;
    auto cached_info = m_cache_manager->get_cached_media_info(cache_key);
    if (cached_info.has_value()) {
        LOG_DEBUG("PlexMediaFetcher", "Using cached media info for: " + media_key);
        return cached_info.value();
    }

    // Fetch from server
    std::string url = server_uri + media_key;
    auto headers_map = get_standard_headers(access_token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get(url, headers);
    if (!response.has_value()) {
        LOG_ERROR("PlexMediaFetcher", "Failed to fetch media details from: " + url + " - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }
    
    if (!response.value().is_success()) {
        LOG_ERROR("PlexMediaFetcher", "Failed to fetch media details from: " + url);
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    try {
        auto json_response = json::parse(response.value().body);

        if (!json_response.contains("MediaContainer") ||
            !json_response["MediaContainer"].contains("Metadata") ||
            json_response["MediaContainer"]["Metadata"].empty()) {
            LOG_ERROR("PlexMediaFetcher", "Invalid media details response");
            return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
        }

        auto metadata = json_response["MediaContainer"]["Metadata"][0];

        core::MediaInfo info;
        extract_basic_media_info(metadata, info);

        // Find appropriate extractor and process
        auto* extractor = find_extractor(metadata);
        if (extractor) {
            auto extracted_info = extractor->extract(metadata);

            // Merge extracted info with basic info
            info.type = extracted_info.type;
            info.imdb_id = extracted_info.imdb_id;
            info.tmdb_id = extracted_info.tmdb_id;
            info.mal_id = extracted_info.mal_id;
            info.genres = extracted_info.genres;
            info.grandparent_title = extracted_info.grandparent_title;
            info.show_title = extracted_info.show_title;
            info.season = extracted_info.season;
            info.episode = extracted_info.episode;
            info.album = extracted_info.album;
            info.artist = extracted_info.artist;
            info.track = extracted_info.track;
            info.grandparent_key = extracted_info.grandparent_key;
        }

        // For TV shows, fetch grandparent metadata before enrichment to get TMDB/IMDB IDs
        if (info.type == core::MediaType::TVShow && !info.grandparent_key.empty()) {
            LOG_DEBUG("PlexMediaFetcher", "Fetching grandparent metadata before enrichment");
            auto grandparent_result = fetch_grandparent_metadata(server_uri, access_token, info);
            if (!grandparent_result.has_value()) {
                LOG_WARNING("PlexMediaFetcher", "Failed to fetch grandparent metadata, continuing without it");
            }
        }

        // Enrich with external services
        enrich_with_external_services(info);

        // Cache the result
        m_cache_manager->cache_media_info(cache_key, info);

        LOG_INFO("PlexMediaFetcher", "Successfully fetched media: " + info.title);
        return info;

    } catch (const std::exception& e) {
        LOG_ERROR("PlexMediaFetcher", "Error parsing media details: " + std::string(e.what()));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }
}

void PlexMediaFetcher::add_media_extractor(std::unique_ptr<IMediaExtractor> extractor) {
    LOG_DEBUG("PlexMediaFetcher", "Adding media extractor");
    m_extractors.push_back(std::move(extractor));
}

void PlexMediaFetcher::add_external_service(std::unique_ptr<IExternalMetadataService> service) {
    LOG_DEBUG("PlexMediaFetcher", "Adding external metadata service");
    m_external_services.push_back(std::move(service));
}

std::expected<void, core::PlexError> PlexMediaFetcher::fetch_grandparent_metadata(
    const std::string& server_uri,
    const core::PlexToken& access_token,
    core::MediaInfo& info) {

    LOG_DEBUG("PlexMediaFetcher", "fetch_grandparent_metadata() called for: " + info.title + " (grandparent key: " + info.grandparent_key + ")");

    if (info.grandparent_key.empty()) {
        LOG_ERROR("PlexMediaFetcher", "No grandparent key available");
        return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
    }

    LOG_DEBUG("PlexMediaFetcher", "Fetching grandparent metadata for: " + info.grandparent_key);

    std::string url = server_uri + info.grandparent_key;
    auto headers_map = get_standard_headers(access_token);
    HttpHeaders headers(headers_map.begin(), headers_map.end());

    auto response = m_http_client->get(url, headers);
    if (!response.has_value()) {
        LOG_ERROR("PlexMediaFetcher", "Failed to fetch grandparent metadata - " + network_error_to_string(response.error()));
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }
    
    if (!response.value().is_success()) {
        LOG_ERROR("PlexMediaFetcher", "Failed to fetch grandparent metadata");
        return std::unexpected<core::PlexError>(core::PlexError::NetworkError);
    }

    try {
        auto json_response = json::parse(response.value().body);

        if (!json_response.contains("MediaContainer") ||
            !json_response["MediaContainer"].contains("Metadata") ||
            json_response["MediaContainer"]["Metadata"].empty()) {
            LOG_ERROR("PlexMediaFetcher", "Invalid grandparent metadata response");
            return std::unexpected<core::PlexError>(core::PlexError::InvalidResponse);
        }

        auto metadata = json_response["MediaContainer"]["Metadata"][0];

        // Extract GUIDs from show metadata
        if (metadata.contains("Guid") && metadata["Guid"].is_array()) {
            for (const auto& guid : metadata["Guid"]) {
                std::string id = guid.value("id", "");
                if (id.find("imdb://") == 0) {
                    info.imdb_id = id.substr(7);
                } else if (id.find("tmdb://") == 0) {
                    info.tmdb_id = id.substr(7);
                }
            }
        }

        // Extract genres
        if (metadata.contains("Genre") && metadata["Genre"].is_array()) {
            info.genres.clear();
            for (const auto& genre : metadata["Genre"]) {
                info.genres.push_back(genre.value("tag", ""));
            }
        }

        return {};

    } catch (const std::exception& e) {
        LOG_ERROR("PlexMediaFetcher", "Error parsing grandparent metadata: " + std::string(e.what()));
        return std::unexpected<core::PlexError>(core::PlexError::ParseError);
    }
}

void PlexMediaFetcher::extract_basic_media_info(const nlohmann::json& metadata, core::MediaInfo& info) const {
    info.title = metadata.value("title", "Unknown");
    info.original_title = metadata.value("originalTitle", info.title);
    info.duration = metadata.value("duration", 0) / 1000.0; // Convert from milliseconds to seconds
    info.summary = metadata.value("summary", "No summary available");
    info.year = metadata.value("year", 0);
    info.rating = metadata.value("rating", 0.0);
    info.studio = metadata.value("studio", "");

    if (metadata.contains("thumb")) {
        info.thumb = metadata["thumb"];
    }
    if (metadata.contains("art")) {
        info.art = metadata["art"];
    }
}

IMediaExtractor* PlexMediaFetcher::find_extractor(const nlohmann::json& metadata) const {
    for (const auto& extractor : m_extractors) {
        if (extractor->can_extract(metadata)) {
            return extractor.get();
        }
    }
    return nullptr;
}

void PlexMediaFetcher::enrich_with_external_services(core::MediaInfo& info) {
    for (const auto& service : m_external_services) {
        auto result = service->enrich_media_info(info);
        if (!result.has_value()) {
            LOG_DEBUG("PlexMediaFetcher", "External service enrichment failed");
        }
    }
}

std::map<std::string, std::string> PlexMediaFetcher::get_standard_headers(const core::PlexToken& token) const {
    std::map<std::string, std::string> headers = {
        {"X-Plex-Product", "Presence For Plex"},
        {"X-Plex-Version", "1.0.0"},
        {"X-Plex-Client-Identifier", "presence-for-plex"},
        {"X-Plex-Platform", "Linux"},
        {"X-Plex-Device", "PC"},
        {"Accept", "application/json"}
    };

    if (!token.empty()) {
        headers["X-Plex-Token"] = token.get();
    }

    return headers;
}

} // namespace services
} // namespace presence_for_plex