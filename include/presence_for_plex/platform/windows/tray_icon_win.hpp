#pragma once

#ifdef _WIN32

#include "presence_for_plex/platform/ui_service.hpp"
#include "presence_for_plex/utils/logger.hpp"

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <ShellScalingApi.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "Shcore.lib")

namespace presence_for_plex::platform::windows {

// Enhanced error information with C++23 features
struct TrayErrorInfo {
    UiError error;
    DWORD win_error{0};
    std::string message;
    std::source_location location;

    TrayErrorInfo(UiError err, std::string_view msg,
                  std::source_location loc = std::source_location::current())
        : error(err), win_error(GetLastError()), message(msg), location(loc) {}
};

// Type alias for cleaner code
template<typename T = void>
using TrayResult = std::expected<T, TrayErrorInfo>;

// Constants using C++23 inline constexpr
namespace TrayConstants {
    inline constexpr UINT WM_TRAYICON = WM_USER + 1;
    inline constexpr UINT TRAY_ICON_ID = 1000;
    inline constexpr UINT MENU_ITEM_BASE_ID = 2000;

    // GUID for persistent icon identification (Windows 11)
    // {F7E4B3C2-8A91-4D7E-B3F1-9C8E5A2D4B6A}
    inline constexpr GUID TRAY_ICON_GUID = {
        0xf7e4b3c2, 0x8a91, 0x4d7e,
        {0xb3, 0xf1, 0x9c, 0x8e, 0x5a, 0x2d, 0x4b, 0x6a}
    };

    inline constexpr std::wstring_view APP_NAME = L"Presence For Plex";
    inline constexpr std::wstring_view WINDOW_CLASS = L"PresenceForPlexTrayWin";
}

// RAII wrapper for menu management
class MenuHandle {
public:
    explicit MenuHandle(HMENU menu = nullptr) noexcept : menu_(menu) {}

    ~MenuHandle() {
        if (menu_) {
            DestroyMenu(menu_);
        }
    }

    // Move-only semantics
    MenuHandle(const MenuHandle&) = delete;
    MenuHandle& operator=(const MenuHandle&) = delete;
    MenuHandle(MenuHandle&& other) noexcept : menu_(std::exchange(other.menu_, nullptr)) {}
    MenuHandle& operator=(MenuHandle&& other) noexcept {
        if (this != &other) {
            if (menu_) DestroyMenu(menu_);
            menu_ = std::exchange(other.menu_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] HMENU get() const noexcept { return menu_; }
    [[nodiscard]] HMENU release() noexcept { return std::exchange(menu_, nullptr); }
    [[nodiscard]] explicit operator bool() const noexcept { return menu_ != nullptr; }

    void reset(HMENU menu = nullptr) noexcept {
        if (menu_) DestroyMenu(menu_);
        menu_ = menu;
    }

private:
    HMENU menu_;
};

// RAII wrapper for icon management
class IconHandle {
public:
    explicit IconHandle(HICON icon = nullptr) noexcept : icon_(icon) {}

    ~IconHandle() {
        if (icon_ && !is_system_icon_) {
            DestroyIcon(icon_);
        }
    }

    // Move-only semantics
    IconHandle(const IconHandle&) = delete;
    IconHandle& operator=(const IconHandle&) = delete;
    IconHandle(IconHandle&& other) noexcept
        : icon_(std::exchange(other.icon_, nullptr)),
          is_system_icon_(std::exchange(other.is_system_icon_, false)) {}
    IconHandle& operator=(IconHandle&& other) noexcept {
        if (this != &other) {
            if (icon_ && !is_system_icon_) DestroyIcon(icon_);
            icon_ = std::exchange(other.icon_, nullptr);
            is_system_icon_ = std::exchange(other.is_system_icon_, false);
        }
        return *this;
    }

    [[nodiscard]] HICON get() const noexcept { return icon_; }
    [[nodiscard]] explicit operator bool() const noexcept { return icon_ != nullptr; }

    void set_system_icon(HICON icon) noexcept {
        if (icon_ && !is_system_icon_) DestroyIcon(icon_);
        icon_ = icon;
        is_system_icon_ = true;
    }

private:
    HICON icon_;
    bool is_system_icon_ = false;
};

// Windows tray icon implementation with C++23 features
class WindowsTrayIcon : public SystemTray {
public:
    WindowsTrayIcon();
    ~WindowsTrayIcon() override;

    // SystemTray interface implementation
    [[nodiscard]] std::expected<void, UiError> initialize() override;
    void shutdown() override;
    [[nodiscard]] bool is_initialized() const override;

    [[nodiscard]] std::expected<void, UiError> set_icon(const std::string& icon_path) override;
    [[nodiscard]] std::expected<void, UiError> set_icon_from_resource(int resource_id) override;
    [[nodiscard]] std::expected<void, UiError> set_tooltip(const std::string& tooltip) override;

    [[nodiscard]] std::expected<void, UiError> set_menu(const std::vector<MenuItem>& items) override;
    [[nodiscard]] std::expected<void, UiError> update_menu_item(const std::string& id, const MenuItem& item) override;
    [[nodiscard]] std::expected<void, UiError> enable_menu_item(const std::string& id, bool enabled) override;
    [[nodiscard]] std::expected<void, UiError> check_menu_item(const std::string& id, bool checked) override;

    void set_click_callback(ClickCallback callback) override;
    void set_double_click_callback(ClickCallback callback) override;
    void set_menu_callback(MenuCallback callback) override;

    void show() override;
    void hide() override;
    [[nodiscard]] bool is_visible() const override;

    // Extended notification support
    [[nodiscard]] TrayResult<> show_notification(
        std::string_view title,
        std::string_view message,
        bool is_error = false,
        std::chrono::seconds timeout = std::chrono::seconds(5)
    );

protected:
    void on_click() override;
    void on_double_click() override;
    void on_menu_item_selected(const std::string& item_id) override;

private:
    // Window procedure
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    // TaskbarCreated message ID for explorer restart handling
    static inline UINT s_taskbar_created_message = 0;

    // Message handling
    LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void handle_tray_message(LPARAM lParam);
    void handle_menu_command(UINT command_id);

    // Initialization helpers
    [[nodiscard]] TrayResult<> create_window();
    [[nodiscard]] TrayResult<> setup_tray_icon();
    void ui_thread_function();
    void recreate_tray_icon();

    // Menu management
    void rebuild_context_menu();
    void add_menu_items(MenuHandle& menu, std::span<const MenuItem> items, UINT& current_id);

    // Icon loading with DPI awareness
    [[nodiscard]] TrayResult<IconHandle> load_icon_from_path(std::string_view path);
    [[nodiscard]] TrayResult<IconHandle> load_icon_from_resource(int resource_id);
    [[nodiscard]] int get_dpi_scaled_size(int base_size) const;

    // Utility functions
    [[nodiscard]] std::wstring to_wide_string(std::string_view str) const;
    [[nodiscard]] bool is_ui_thread() const noexcept;

    // Template helper for safe string copy
    template<size_t N>
    void safe_copy(wchar_t (&dest)[N], std::wstring_view src) {
        auto result = std::format_to_n(dest, N - 1, L"{}", src);
        *result.out = L'\0';
    }

    // State management
    std::atomic<bool> initialized_{false};
    std::atomic<bool> visible_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> icon_added_{false};

    // Synchronization
    mutable std::mutex mutex_;
    std::condition_variable init_cv_;
    std::mutex init_mutex_;
    bool window_created_{false};

    // Window handles
    HWND window_handle_{nullptr};
    MenuHandle context_menu_;
    IconHandle current_icon_;
    NOTIFYICONDATAW notify_icon_data_{};

    // UI thread
    std::jthread ui_thread_;  // Using jthread for automatic joining
    std::thread::id ui_thread_id_;

    // DPI awareness
    UINT current_dpi_{96};

    // Menu data
    std::vector<MenuItem> menu_items_;
    std::unordered_map<UINT, std::string> command_to_menu_id_;
    std::unordered_map<std::string, UINT> menu_id_to_command_;
    UINT next_command_id_{TrayConstants::MENU_ITEM_BASE_ID};

    // Callbacks
    ClickCallback click_callback_;
    ClickCallback double_click_callback_;
    MenuCallback menu_callback_;

    // Component name for logging
    inline static constexpr std::string_view component_name_ = "WindowsTrayIcon";
};

} // namespace presence_for_plex::platform::windows

#endif // _WIN32