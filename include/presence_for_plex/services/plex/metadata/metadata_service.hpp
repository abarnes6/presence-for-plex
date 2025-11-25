#pragma once

#include "presence_for_plex/core/models.hpp"
#include <expected>
#include <string>

namespace presence_for_plex {
namespace services {

// Interface for external metadata services (TMDB, Jikan, etc.)
class IMetadataService {
public:
    virtual ~IMetadataService() = default;

    virtual std::expected<std::string, core::PlexError> fetch_artwork_url(
        const std::string& external_id,
        core::MediaType type
    ) = 0;

    virtual std::expected<void, core::PlexError> enrich_media_info(
        core::MediaInfo& info
    ) = 0;
};

} // namespace services
} // namespace presence_for_plex
