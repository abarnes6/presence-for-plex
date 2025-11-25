#pragma once

#include "presence_for_plex/services/plex/metadata/metadata_service.hpp"
#include <memory>
#include <string>

namespace presence_for_plex {
namespace services {

class HttpClient;

class Jikan : public IMetadataService {
public:
    explicit Jikan(std::shared_ptr<HttpClient> http_client);

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

} // namespace services
} // namespace presence_for_plex
