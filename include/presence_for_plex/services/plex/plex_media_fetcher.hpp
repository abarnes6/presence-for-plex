#pragma once

#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/services/plex/plex_cache_manager.hpp"
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <expected>
#include <map>

namespace presence_for_plex {
namespace services {

// Forward declarations
class HttpClient;

// Interface for extracting media information from different content types
class IMediaExtractor {
public:
    virtual ~IMediaExtractor() = default;

    virtual bool can_extract(const nlohmann::json& metadata) const = 0;
    virtual core::MediaInfo extract(const nlohmann::json& metadata) const = 0;
    virtual core::MediaType get_media_type() const = 0;
};

// Interface for external metadata services
class IExternalMetadataService {
public:
    virtual ~IExternalMetadataService() = default;

    virtual std::expected<std::string, core::PlexError> fetch_artwork_url(
        const std::string& external_id,
        core::MediaType type
    ) = 0;

    virtual std::expected<void, core::PlexError> enrich_media_info(
        core::MediaInfo& info
    ) = 0;
};

// Interface for media fetching following SRP
class IPlexMediaFetcher {
public:
    virtual ~IPlexMediaFetcher() = default;

    // Core media fetching
    virtual std::expected<core::MediaInfo, core::PlexError> fetch_media_details(
        const std::string& server_uri,
        const core::PlexToken& access_token,
        const std::string& media_key
    ) = 0;

    // Extractor management
    virtual void add_media_extractor(std::unique_ptr<IMediaExtractor> extractor) = 0;
    virtual void add_external_service(std::unique_ptr<IExternalMetadataService> service) = 0;

    // Utility methods
    virtual std::expected<void, core::PlexError> fetch_grandparent_metadata(
        const std::string& server_uri,
        const core::PlexToken& access_token,
        core::MediaInfo& info
    ) = 0;
};

// TMDB service implementation
class TMDBService : public IExternalMetadataService {
public:
    explicit TMDBService(std::shared_ptr<HttpClient> http_client, const std::string& access_token);

    std::expected<std::string, core::PlexError> fetch_artwork_url(
        const std::string& tmdb_id,
        core::MediaType type
    ) override;

    std::expected<void, core::PlexError> enrich_media_info(core::MediaInfo& info) override;

private:
    std::shared_ptr<HttpClient> m_http_client;
    std::string m_access_token;
    static constexpr const char* TMDB_IMAGE_BASE_URL = "https://image.tmdb.org/t/p/w500";
};

// Jikan/MAL service implementation
class JikanService : public IExternalMetadataService {
public:
    explicit JikanService(std::shared_ptr<HttpClient> http_client);

    std::expected<std::string, core::PlexError> fetch_artwork_url(
        const std::string& mal_id,
        core::MediaType type
    ) override;

    std::expected<void, core::PlexError> enrich_media_info(core::MediaInfo& info) override;

private:
    std::expected<std::string, core::PlexError> search_anime_by_title(
        const std::string& title,
        int year = 0
    );

    std::string url_encode(const std::string& value);

    std::shared_ptr<HttpClient> m_http_client;
    static constexpr const char* JIKAN_API_URL = "https://api.jikan.moe/v4/anime";
};

// Movie extractor
class MovieExtractor : public IMediaExtractor {
public:
    bool can_extract(const nlohmann::json& metadata) const override;
    core::MediaInfo extract(const nlohmann::json& metadata) const override;
    core::MediaType get_media_type() const override { return core::MediaType::Movie; }

private:
    void extract_guids(const nlohmann::json& metadata, core::MediaInfo& info) const;
    void extract_genres(const nlohmann::json& metadata, core::MediaInfo& info) const;
};

// TV Show extractor
class TVShowExtractor : public IMediaExtractor {
public:
    bool can_extract(const nlohmann::json& metadata) const override;
    core::MediaInfo extract(const nlohmann::json& metadata) const override;
    core::MediaType get_media_type() const override { return core::MediaType::TVShow; }

private:
    void extract_episode_info(const nlohmann::json& metadata, core::MediaInfo& info) const;
};

// Music extractor
class MusicExtractor : public IMediaExtractor {
public:
    bool can_extract(const nlohmann::json& metadata) const override;
    core::MediaInfo extract(const nlohmann::json& metadata) const override;
    core::MediaType get_media_type() const override { return core::MediaType::Music; }

private:
    void extract_track_info(const nlohmann::json& metadata, core::MediaInfo& info) const;
};

// Concrete implementation
class PlexMediaFetcher : public IPlexMediaFetcher {
public:
    PlexMediaFetcher(
        std::shared_ptr<HttpClient> http_client,
        std::shared_ptr<PlexCacheManager> cache_manager
    );

    ~PlexMediaFetcher() override = default;

    std::expected<core::MediaInfo, core::PlexError> fetch_media_details(
        const std::string& server_uri,
        const core::PlexToken& access_token,
        const std::string& media_key
    ) override;

    void add_media_extractor(std::unique_ptr<IMediaExtractor> extractor) override;
    void add_external_service(std::unique_ptr<IExternalMetadataService> service) override;

    std::expected<void, core::PlexError> fetch_grandparent_metadata(
        const std::string& server_uri,
        const core::PlexToken& access_token,
        core::MediaInfo& info
    ) override;

private:
    void extract_basic_media_info(const nlohmann::json& metadata, core::MediaInfo& info) const;
    IMediaExtractor* find_extractor(const nlohmann::json& metadata) const;
    void enrich_with_external_services(core::MediaInfo& info);

    std::map<std::string, std::string> get_standard_headers(const core::PlexToken& token) const;

    std::shared_ptr<HttpClient> m_http_client;
    std::shared_ptr<PlexCacheManager> m_cache_manager;
    std::vector<std::unique_ptr<IMediaExtractor>> m_extractors;
    std::vector<std::unique_ptr<IExternalMetadataService>> m_external_services;
};

} // namespace services
} // namespace presence_for_plex
