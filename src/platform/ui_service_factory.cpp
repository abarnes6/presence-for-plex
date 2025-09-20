#include "presence_for_plex/platform/ui_service.hpp"
#include <memory>

#ifdef _WIN32
#include "presence_for_plex/platform/windows/windows_ui_service.hpp"
#endif

namespace presence_for_plex {
namespace platform {

// Static factory method implementation
std::unique_ptr<UiServiceFactory> UiServiceFactory::create_default_factory() {
#ifdef _WIN32
    return std::make_unique<windows::WindowsUiServiceFactory>();
#elif defined(__APPLE__)
    // TODO: Implement macOS factory
    return nullptr;
#elif defined(__linux__)
    // TODO: Implement Linux factory
    return nullptr;
#else
    return nullptr;
#endif
}

} // namespace platform
} // namespace presence_for_plex