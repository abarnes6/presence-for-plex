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
    OperationFailed
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

// Notification manager interface
class NotificationManager {
public:
    virtual ~NotificationManager() = default;

    // Lifecycle
    virtual std::expected<void, UiError> initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool is_supported() const = 0;

    // Notification management
    using NotificationId = std::string;

    virtual std::expected<NotificationId, UiError> show_notification(const Notification& notification) = 0;
    virtual std::expected<void, UiError> update_notification(const NotificationId& id, const Notification& notification) = 0;
    virtual std::expected<void, UiError> hide_notification(const NotificationId& id) = 0;
    virtual void clear_all_notifications() = 0;

    // Event callbacks
    using NotificationCallback = std::function<void(const NotificationId&)>;

    virtual void set_click_callback(NotificationCallback callback) = 0;
    virtual void set_dismiss_callback(NotificationCallback callback) = 0;

protected:
    virtual void on_notification_clicked(const NotificationId& id) = 0;
    virtual void on_notification_dismissed(const NotificationId& id) = 0;
};

// Window management interface
class WindowManager {
public:
    virtual ~WindowManager() = default;

    // Window discovery
    virtual std::vector<std::string> find_windows_by_title(const std::string& title) const = 0;
    virtual std::vector<std::string> find_windows_by_class(const std::string& class_name) const = 0;
    virtual std::optional<std::string> find_window_by_process(const std::string& process_name) const = 0;

    // Window operations
    virtual std::expected<void, UiError> bring_to_front(const std::string& window_id) = 0;
    virtual std::expected<void, UiError> minimize_window(const std::string& window_id) = 0;
    virtual std::expected<void, UiError> maximize_window(const std::string& window_id) = 0;
    virtual std::expected<void, UiError> close_window(const std::string& window_id) = 0;

    // Window properties
    virtual std::optional<std::string> get_window_title(const std::string& window_id) const = 0;
    virtual std::expected<bool, UiError> is_window_visible(const std::string& window_id) const = 0;
    virtual std::expected<bool, UiError> is_window_minimized(const std::string& window_id) const = 0;
};

// Dialog interface for user interaction
class DialogManager {
public:
    virtual ~DialogManager() = default;

    enum class DialogType {
        Info,
        Warning,
        Error,
        Question,
        Input
    };

    enum class DialogResult {
        OK,
        Cancel,
        Yes,
        No,
        Retry
    };

    // Message dialogs
    virtual std::expected<DialogResult, UiError> show_message(
        const std::string& title,
        const std::string& message,
        DialogType type = DialogType::Info
    ) = 0;

    virtual std::expected<DialogResult, UiError> show_question(
        const std::string& title,
        const std::string& question,
        bool show_cancel = true
    ) = 0;

    // Input dialogs
    virtual std::expected<std::string, UiError> show_input_dialog(
        const std::string& title,
        const std::string& prompt,
        const std::string& default_value = ""
    ) = 0;

    virtual std::expected<std::string, UiError> show_password_dialog(
        const std::string& title,
        const std::string& prompt
    ) = 0;

    // File dialogs
    virtual std::expected<std::string, UiError> show_open_file_dialog(
        const std::string& title,
        const std::string& filter = "",
        const std::string& initial_dir = ""
    ) = 0;

    virtual std::expected<std::string, UiError> show_save_file_dialog(
        const std::string& title,
        const std::string& filter = "",
        const std::string& initial_dir = "",
        const std::string& default_name = ""
    ) = 0;

    virtual std::expected<std::string, UiError> show_folder_dialog(
        const std::string& title,
        const std::string& initial_dir = ""
    ) = 0;
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
    virtual std::unique_ptr<NotificationManager> create_notification_manager() = 0;
    virtual std::unique_ptr<WindowManager> create_window_manager() = 0;
    virtual std::unique_ptr<DialogManager> create_dialog_manager() = 0;

    // Platform capabilities
    virtual bool supports_system_tray() const = 0;
    virtual bool supports_notifications() const = 0;
    virtual bool supports_window_management() const = 0;
    virtual bool supports_dialogs() const = 0;

    // Event loop integration
    virtual void process_events() = 0;
    virtual void quit_event_loop() = 0;
};

// Factory for creating platform-specific UI services
class UiServiceFactory {
public:
    virtual ~UiServiceFactory() = default;

    enum class PlatformType {
        Windows,
        macOS,
        Linux,
        Auto // Detect automatically
    };

    virtual std::unique_ptr<UiService> create_service(PlatformType type = PlatformType::Auto) = 0;

    static std::unique_ptr<UiServiceFactory> create_default_factory();
};

} // namespace platform
} // namespace presence_for_plex