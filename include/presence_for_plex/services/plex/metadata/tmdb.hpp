#pragma once

#include "presence_for_plex/services/plex/metadata/metadata_service.hpp"
#include <memory>
#include <string>

namespace presence_for_plex {
namespace services {

class HttpClient;

class TMDB : public IMetadataService {
public:
    explicit TMDB(std::shared_ptr<HttpClient> http_client, const std::string& access_token);

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

} // namespace services
} // namespace presence_for_plex
