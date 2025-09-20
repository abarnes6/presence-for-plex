#pragma once

#include "presence_for_plex/core/dependency_injection.hpp"
#include "presence_for_plex/services/media_service.hpp"
#include "presence_for_plex/services/plex/plex_service_impl.hpp"
#include "presence_for_plex/services/network_service.hpp"
#include <memory>

namespace presence_for_plex {
namespace services {

// Service factory following Dependency Inversion Principle
class ServiceFactory {
public:
    explicit ServiceFactory(std::shared_ptr<core::DependencyContainer> container);

    // Create media service with dependency injection
    std::unique_ptr<MediaService> create_media_service(
        MediaServiceFactory::ServiceType type,
        const core::ApplicationConfig& config
    );

    // Create HTTP client
    std::shared_ptr<HttpClient> create_http_client();

    // Create individual Plex components
    std::shared_ptr<IPlexAuthenticator> create_plex_authenticator();
    std::shared_ptr<IPlexCacheManager> create_plex_cache_manager();
    std::shared_ptr<IPlexConnectionManager> create_plex_connection_manager();
    std::shared_ptr<IPlexMediaFetcher> create_plex_media_fetcher();
    std::shared_ptr<IPlexSessionManager> create_plex_session_manager();

    // Configure the dependency container with default bindings
    void configure_default_bindings();

private:
    std::shared_ptr<core::DependencyContainer> m_container;
};

// Alternative factory using composition over inheritance
template<typename ServiceType>
class ComponentFactory {
public:
    template<typename... Dependencies>
    static std::shared_ptr<ServiceType> create_with_dependencies(Dependencies&&... deps) {
        return std::make_shared<ServiceType>(std::forward<Dependencies>(deps)...);
    }
};

// Configuration for service creation
struct ServiceConfiguration {
    bool use_caching = true;
    bool enable_tmdb = true;
    bool enable_mal = true;
    std::chrono::seconds cache_timeout{3600};
    std::string tmdb_token;
};

} // namespace services
} // namespace presence_for_plex