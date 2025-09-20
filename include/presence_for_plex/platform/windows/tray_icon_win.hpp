#pragma once

#ifdef _WIN32

#include "presence_for_plex/platform/ui_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <windows.h>
#include <shellapi.h>

namespace presence_for_plex {
namespace platform {
namespace windows {

class WindowsTrayIcon : public SystemTray {
public:
    WindowsTrayIcon();
    ~WindowsTrayIcon() override;

    // SystemTray interface implementation
    std::expected<void, UiError> initialize() override;
    void shutdown() override;
    bool is_initialized() const override;

    std::expected<void, UiError> set_icon(const std::string& icon_path) override;
    std::expected<void, UiError> set_icon_from_resource(int resource_id) override;
    std::expected<void, UiError> set_tooltip(const std::string& tooltip) override;

    std::expected<void, UiError> set_menu(const std::vector<MenuItem>& items) override;
    std::expected<void, UiError> update_menu_item(const std::string& id, const MenuItem& item) override;
    std::expected<void, UiError> enable_menu_item(const std::string& id, bool enabled) override;
    std::expected<void, UiError> check_menu_item(const std::string& id, bool checked) override;

    void set_click_callback(ClickCallback callback) override;
    void set_double_click_callback(ClickCallback callback) override;
    void set_menu_callback(MenuCallback callback) override;

    void show() override;
    void hide() override;
    bool is_visible() const override;

    // Balloon notification support
    std::expected<void, UiError> show_balloon_notification(
        const std::string& title,
        const std::string& message,
        bool is_error = false,
        std::chrono::seconds timeout = std::chrono::seconds(5)
    );

    std::expected<void, UiError> show_update_notification(
        const std::string& title,
        const std::string& message,
        const std::string& download_url
    );

    // Callback types for notifications
    using NotificationClickCallback = std::function<void()>;
    using UpdateClickCallback = std::function<void(const std::string& url)>;

    void set_notification_click_callback(NotificationClickCallback callback);
    void set_update_click_callback(UpdateClickCallback callback);

protected:
    void on_click() override;
    void on_double_click() override;
    void on_menu_item_selected(const std::string& item_id) override;

private:
    // Constants
    static constexpr UINT WM_TRAYICON = WM_USER + 1;
    static constexpr UINT TRAY_ICON_ID = 1000;
    static constexpr UINT MENU_ITEM_BASE_ID = 2000;

    // Static members for Windows callback
    static WindowsTrayIcon* s_instance;
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    // Window and UI thread management
    void ui_thread_function();
    std::expected<void, UiError> create_window();
    void destroy_window();

    // Menu management
    void build_context_menu();
    void add_menu_items_to_hmenu(HMENU menu, const std::vector<MenuItem>& items, UINT& current_id);
    std::string get_menu_item_id_by_command_id(UINT command_id) const;

    // Icon loading
    std::expected<HICON, UiError> load_icon_from_path(const std::string& path);
    std::expected<HICON, UiError> load_icon_from_resource(int resource_id);

    // Utility functions
    std::wstring utf8_to_wstring(const std::string& utf8_str) const;
    bool is_ui_thread() const;

    // Member variables
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_visible{false};
    std::atomic<bool> m_running{false};

    // Window handles
    HWND m_window_handle{nullptr};
    HMENU m_context_menu{nullptr};
    NOTIFYICONDATAW m_notify_icon_data{};
    HICON m_current_icon{nullptr};

    // UI thread
    std::thread m_ui_thread;
    std::thread::id m_ui_thread_id;

    // Menu data
    std::vector<MenuItem> m_menu_items;
    std::unordered_map<UINT, std::string> m_command_id_to_menu_id;
    std::unordered_map<std::string, UINT> m_menu_id_to_command_id;
    UINT m_next_command_id{MENU_ITEM_BASE_ID};

    // Callbacks
    ClickCallback m_click_callback;
    ClickCallback m_double_click_callback;
    MenuCallback m_menu_callback;
    NotificationClickCallback m_notification_click_callback;
    UpdateClickCallback m_update_click_callback;

    // Update URL storage
    std::string m_download_url;

    // Timeout utilities
    template<typename Func>
    bool execute_with_timeout(Func func, std::chrono::milliseconds timeout, const std::string& operation_name);

    // URL opening utility
    std::expected<void, UiError> open_url(const std::string& url);

    // Notification handlers
    void on_notification_clicked();
    void on_balloon_clicked();

    // Synchronization
    mutable std::mutex m_mutex;

    // Component name for logging
    std::string m_component_name;
};

} // namespace windows
} // namespace platform
} // namespace presence_for_plex

#endif // _WIN32