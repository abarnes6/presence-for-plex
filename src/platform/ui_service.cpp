#include "presence_for_plex/platform/ui_service.hpp"

namespace presence_for_plex {
namespace platform {

MenuItem::MenuItem(std::string id, std::string label, std::function<void()> action)
    : type(MenuItemType::Action), id(std::move(id)), label(std::move(label)), action(std::move(action)) {}

Notification::Notification(std::string title, std::string message, NotificationType type)
    : type(type), title(std::move(title)), message(std::move(message)) {}

} // namespace platform
} // namespace presence_for_plex
