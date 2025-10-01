#include "presence_for_plex/platform/ui_service.hpp"
#include <memory>

#ifdef USE_QT_UI
#include "presence_for_plex/platform/qt/qt_ui_service.hpp"
#endif

namespace presence_for_plex {
namespace platform {

// MenuItem constructor implementation
MenuItem::MenuItem(std::string id, std::string label, std::function<void()> action)
    : type(MenuItemType::Action), id(std::move(id)), label(std::move(label)), action(std::move(action)) {}

// Notification constructor implementation
Notification::Notification(std::string title, std::string message, NotificationType type)
    : type(type), title(std::move(title)), message(std::move(message)) {}

// Static factory method implementation
std::unique_ptr<UiService> UiService::create_default() {
#ifdef USE_QT_UI
    return qt::QtUiService::create();
#else
    return nullptr;
#endif
}

} // namespace platform
} // namespace presence_for_plex