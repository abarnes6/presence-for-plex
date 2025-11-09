#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <expected>

namespace presence_for_plex {
namespace platform {

// UI error types
enum class UiError {
    NotSupported,
    InitializationFailed,
    ResourceNotFound,
    OperationFailed,
    Cancelled
};

// Menu item types
enum class MenuItemType {
    Action,
    Separator,
    Submenu,
    Checkbox,
    Radio
};

// Menu item structure
struct MenuItem {
    MenuItemType type = MenuItemType::Action;
    std::string id;
    std::string label;
    std::string tooltip;
    std::string icon_path;
    bool enabled = true;
    bool checked = false; // For checkbox/radio items
    std::function<void()> action;
    std::vector<MenuItem> submenu; // For submenu items

    MenuItem() = default;
    MenuItem(std::string id, std::string label, std::function<void()> action = {});
};

// Notification types
enum class NotificationType {
    Info,
    Warning,
    Error,
    Success
};

// Notification structure
struct Notification {
    NotificationType type = NotificationType::Info;
    std::string title;
    std::string message;
    std::string icon_path;
    std::chrono::seconds duration{5}; // 0 means persistent
    std::function<void()> on_click;
    std::function<void()> on_dismiss;

    Notification() = default;
    Notification(std::string title, std::string message, NotificationType type = NotificationType::Info);
};

// System tray interface
class SystemTray {
public:
    virtual ~SystemTray() = default;

    // Lifecycle
    virtual std::expected<void, UiError> initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool is_initialized() const = 0;

    // Icon management
    virtual std::expected<void, UiError> set_icon(const std::string& icon_path) = 0;
    virtual std::expected<void, UiError> set_icon_from_resource(int resource_id) = 0;
    virtual std::expected<void, UiError> set_tooltip(const std::string& tooltip) = 0;

    // Menu management
    virtual std::expected<void, UiError> set_menu(const std::vector<MenuItem>& items) = 0;
    virtual std::expected<void, UiError> update_menu_item(const std::string& id, const MenuItem& item) = 0;
    virtual std::expected<void, UiError> enable_menu_item(const std::string& id, bool enabled) = 0;
    virtual std::expected<void, UiError> check_menu_item(const std::string& id, bool checked) = 0;

    // Status text management
    virtual std::expected<void, UiError> set_status_text(const std::string& id, const std::string& text) = 0;

    // Event callbacks
    using ClickCallback = std::function<void()>;
    using MenuCallback = std::function<void(const std::string& item_id)>;

    virtual void set_click_callback(ClickCallback callback) = 0;
    virtual void set_double_click_callback(ClickCallback callback) = 0;
    virtual void set_menu_callback(MenuCallback callback) = 0;

    // Visibility
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual bool is_visible() const = 0;

protected:
    virtual void on_click() = 0;
    virtual void on_double_click() = 0;
    virtual void on_menu_item_selected(const std::string& item_id) = 0;
};

// Main UI service interface that aggregates all UI functionality
class UiService {
public:
    virtual ~UiService() = default;

    // Lifecycle
    virtual std::expected<void, UiError> initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool is_initialized() const = 0;

    // Component access
    virtual std::unique_ptr<SystemTray> create_system_tray() = 0;

    // Platform capabilities
    virtual bool supports_system_tray() const = 0;

    // Event loop integration
    virtual void process_events() = 0;
    virtual void quit_event_loop() = 0;
};

} // namespace platform
} // namespace presence_for_plex