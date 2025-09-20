#ifdef _WIN32

#include "presence_for_plex/platform/windows/tray_icon_win.hpp"
#include <codecvt>
#include <locale>

namespace presence_for_plex {
namespace platform {
namespace windows {

// Static member initialization
WindowsTrayIcon* WindowsTrayIcon::s_instance = nullptr;

WindowsTrayIcon::WindowsTrayIcon()
    : m_component_name("WindowsTrayIcon")
{
    s_instance = this;
    PLEX_LOG_DEBUG(m_component_name, "WindowsTrayIcon constructed");
}

WindowsTrayIcon::~WindowsTrayIcon() {
    PLEX_LOG_DEBUG(m_component_name, "WindowsTrayIcon destructor called");
    shutdown();
    s_instance = nullptr;
}

std::expected<void, UiError> WindowsTrayIcon::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_initialized) {
        PLEX_LOG_DEBUG(m_component_name, "Already initialized, skipping");
        return {};
    }

    PLEX_LOG_INFO(m_component_name, "Initializing Windows tray icon");

    m_running = true;
    m_ui_thread = std::thread(&WindowsTrayIcon::ui_thread_function, this);

    // Wait for window to be created
    for (int i = 0; i < 100 && m_window_handle == nullptr; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (m_window_handle == nullptr) {
        PLEX_LOG_ERROR(m_component_name, "Failed to create window within timeout");
        return std::unexpected(UiError::InitializationFailed);
    }

    m_initialized = true;
    PLEX_LOG_INFO(m_component_name, "Windows tray icon initialized successfully");
    return {};
}

void WindowsTrayIcon::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Shutting down Windows tray icon");

    hide();

    m_running = false;

    if (m_window_handle) {
        PostMessage(m_window_handle, WM_CLOSE, 0, 0);
    }

    if (m_ui_thread.joinable()) {
        m_ui_thread.join();
    }

    m_initialized = false;
    PLEX_LOG_INFO(m_component_name, "Windows tray icon shut down");
}

bool WindowsTrayIcon::is_initialized() const {
    return m_initialized;
}

std::expected<void, UiError> WindowsTrayIcon::set_icon(const std::string& icon_path) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto icon_result = load_icon_from_path(icon_path);
    if (!icon_result) {
        return std::unexpected(icon_result.error());
    }

    if (m_current_icon && m_current_icon != LoadIcon(nullptr, IDI_APPLICATION)) {
        DestroyIcon(m_current_icon);
    }

    m_current_icon = icon_result.value();
    m_notify_icon_data.hIcon = m_current_icon;

    if (m_visible) {
        if (!Shell_NotifyIconW(NIM_MODIFY, &m_notify_icon_data)) {
            DWORD error = GetLastError();
            std::string msg = "Failed to update tray icon, error: " + std::to_string(error);
            PLEX_LOG_ERROR(m_component_name, msg);
            return std::unexpected(UiError::OperationFailed);
        }
    }

    std::string msg = "Icon set from path: " + icon_path;
    PLEX_LOG_DEBUG(m_component_name, msg);
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::set_icon_from_resource(int resource_id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto icon_result = load_icon_from_resource(resource_id);
    if (!icon_result) {
        return std::unexpected(icon_result.error());
    }

    if (m_current_icon && m_current_icon != LoadIcon(nullptr, IDI_APPLICATION)) {
        DestroyIcon(m_current_icon);
    }

    m_current_icon = icon_result.value();
    m_notify_icon_data.hIcon = m_current_icon;

    if (m_visible) {
        if (!Shell_NotifyIconW(NIM_MODIFY, &m_notify_icon_data)) {
            DWORD error = GetLastError();
            std::string msg = "Failed to update tray icon, error: " + std::to_string(error);
            PLEX_LOG_ERROR(m_component_name, msg);
            return std::unexpected(UiError::OperationFailed);
        }
    }

    std::string msg = "Icon set from resource: " + std::to_string(resource_id);
    PLEX_LOG_DEBUG(m_component_name, msg);
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::set_tooltip(const std::string& tooltip) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        return std::unexpected(UiError::InitializationFailed);
    }

    std::wstring wtooltip = utf8_to_wstring(tooltip);
    wcsncpy_s(m_notify_icon_data.szTip, _countof(m_notify_icon_data.szTip),
              wtooltip.c_str(), _TRUNCATE);

    if (m_visible) {
        if (!Shell_NotifyIconW(NIM_MODIFY, &m_notify_icon_data)) {
            DWORD error = GetLastError();
            std::string msg = "Failed to update tooltip, error: " + std::to_string(error);
            PLEX_LOG_ERROR(m_component_name, msg);
            return std::unexpected(UiError::OperationFailed);
        }
    }

    std::string msg = "Tooltip set: " + tooltip;
    PLEX_LOG_DEBUG(m_component_name, msg);
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::set_menu(const std::vector<MenuItem>& items) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        return std::unexpected(UiError::InitializationFailed);
    }

    m_menu_items = items;
    m_command_id_to_menu_id.clear();
    m_menu_id_to_command_id.clear();
    m_next_command_id = MENU_ITEM_BASE_ID;

    build_context_menu();

    std::string msg = "Menu set with " + std::to_string(items.size()) + " items";
    PLEX_LOG_DEBUG(m_component_name, msg);
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::update_menu_item(const std::string& id, const MenuItem& item) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        return std::unexpected(UiError::InitializationFailed);
    }

    // Find and update the menu item
    for (auto& menu_item : m_menu_items) {
        if (menu_item.id == id) {
            menu_item = item;
            menu_item.id = id; // Preserve the ID
            break;
        }
    }

    build_context_menu();

    std::string msg = "Menu item updated: " + id;
    PLEX_LOG_DEBUG(m_component_name, msg);
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::enable_menu_item(const std::string& id, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto it = m_menu_id_to_command_id.find(id);
    if (it == m_menu_id_to_command_id.end()) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    UINT flags = enabled ? MF_ENABLED : MF_DISABLED | MF_GRAYED;
    if (!EnableMenuItem(m_context_menu, it->second, flags)) {
        return std::unexpected(UiError::OperationFailed);
    }

    // Update the stored menu item
    for (auto& menu_item : m_menu_items) {
        if (menu_item.id == id) {
            menu_item.enabled = enabled;
            break;
        }
    }

    std::string msg = "Menu item " + id + (enabled ? " enabled" : " disabled");
    PLEX_LOG_DEBUG(m_component_name, msg);
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::check_menu_item(const std::string& id, bool checked) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto it = m_menu_id_to_command_id.find(id);
    if (it == m_menu_id_to_command_id.end()) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    UINT flags = checked ? MF_CHECKED : MF_UNCHECKED;
    if (!CheckMenuItem(m_context_menu, it->second, flags)) {
        return std::unexpected(UiError::OperationFailed);
    }

    // Update the stored menu item
    for (auto& menu_item : m_menu_items) {
        if (menu_item.id == id) {
            menu_item.checked = checked;
            break;
        }
    }

    std::string msg = "Menu item " + id + (checked ? " checked" : " unchecked");
    PLEX_LOG_DEBUG(m_component_name, msg);
    return {};
}

void WindowsTrayIcon::set_click_callback(ClickCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_click_callback = std::move(callback);
}

void WindowsTrayIcon::set_double_click_callback(ClickCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_double_click_callback = std::move(callback);
}

void WindowsTrayIcon::set_menu_callback(MenuCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_menu_callback = std::move(callback);
}

void WindowsTrayIcon::set_notification_click_callback(NotificationClickCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_notification_click_callback = std::move(callback);
}

void WindowsTrayIcon::set_update_click_callback(UpdateClickCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_update_click_callback = std::move(callback);
}

void WindowsTrayIcon::show() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized || m_visible) {
        return;
    }

    if (!Shell_NotifyIconW(NIM_ADD, &m_notify_icon_data)) {
        DWORD error = GetLastError();
        std::string msg = "Failed to show tray icon, error: " + std::to_string(error);
        PLEX_LOG_ERROR(m_component_name, msg);
        return;
    }

    m_visible = true;
    PLEX_LOG_INFO(m_component_name, "Tray icon shown");
}

void WindowsTrayIcon::hide() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_visible) {
        return;
    }

    if (!Shell_NotifyIconW(NIM_DELETE, &m_notify_icon_data)) {
        DWORD error = GetLastError();
        std::string msg = "Failed to hide tray icon, error: " + std::to_string(error);
        PLEX_LOG_ERROR(m_component_name, msg);
    }

    m_visible = false;
    PLEX_LOG_INFO(m_component_name, "Tray icon hidden");
}

bool WindowsTrayIcon::is_visible() const {
    return m_visible;
}

std::expected<void, UiError> WindowsTrayIcon::show_balloon_notification(
    const std::string& title,
    const std::string& message,
    bool is_error,
    std::chrono::seconds timeout) {

    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized || !m_visible) {
        return std::unexpected(UiError::InitializationFailed);
    }

    // Convert title and message to wide strings
    std::wstring wtitle = utf8_to_wstring(title);
    std::wstring wmessage = utf8_to_wstring(message);

    if (wtitle.empty() || wmessage.empty()) {
        return std::unexpected(UiError::OperationFailed);
    }

    // Create notification data
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = m_window_handle;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = is_error ? NIIF_ERROR : NIIF_INFO;
    nid.uTimeout = static_cast<UINT>(timeout.count() * 1000); // Convert to milliseconds

    // Truncate strings to Windows limits
    wcsncpy_s(nid.szInfoTitle, _countof(nid.szInfoTitle), wtitle.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, _countof(nid.szInfo), wmessage.c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        DWORD error = GetLastError();
        std::string msg = "Failed to show balloon notification, error: " + std::to_string(error);
        PLEX_LOG_ERROR(m_component_name, msg);

        // Fallback to MessageBox in a separate thread
        std::thread([wtitle, wmessage, is_error]() {
            UINT type = MB_OK | (is_error ? MB_ICONERROR : MB_ICONINFORMATION);
            MessageBoxW(nullptr, wmessage.c_str(), wtitle.c_str(), type);
        }).detach();

        return std::unexpected(UiError::OperationFailed);
    }

    std::string msg = "Balloon notification shown: " + title;
    PLEX_LOG_DEBUG(m_component_name, msg);
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::show_update_notification(
    const std::string& title,
    const std::string& message,
    const std::string& download_url) {

    std::lock_guard<std::mutex> lock(m_mutex);

    // Store download URL for later use
    m_download_url = download_url;

    std::string msg = "Storing download URL: " + download_url;
    PLEX_LOG_DEBUG(m_component_name, msg);

    // Show notification using the regular method
    auto result = show_balloon_notification(title, message, false);
    return result;
}

void WindowsTrayIcon::on_click() {
    if (m_click_callback) {
        m_click_callback();
    }
}

void WindowsTrayIcon::on_double_click() {
    if (m_double_click_callback) {
        m_double_click_callback();
    }
}

void WindowsTrayIcon::on_menu_item_selected(const std::string& item_id) {
    if (m_menu_callback) {
        m_menu_callback(item_id);
    }

    // Also call the item's action if it has one
    for (const auto& item : m_menu_items) {
        if (item.id == item_id && item.action) {
            try {
                item.action();
            } catch (const std::exception& e) {
                std::string msg = "Exception in menu item action for " + item_id + ": " + e.what();
                PLEX_LOG_ERROR(m_component_name, msg);
            }
            break;
        }
    }
}

void WindowsTrayIcon::on_notification_clicked() {
    if (m_notification_click_callback) {
        execute_with_timeout(
            [this]() {
                if (m_notification_click_callback) {
                    m_notification_click_callback();
                }
            },
            std::chrono::seconds(5),
            "Notification click callback"
        );
    }
}

void WindowsTrayIcon::on_balloon_clicked() {
    if (!m_download_url.empty() && m_update_click_callback) {
        std::string url = m_download_url; // Copy for thread safety
        execute_with_timeout(
            [this, url]() {
                if (m_update_click_callback) {
                    m_update_click_callback(url);
                }
            },
            std::chrono::seconds(5),
            "Update click callback"
        );
    } else if (!m_download_url.empty()) {
        // Fallback: open URL directly
        open_url(m_download_url);
    } else {
        // Regular notification click
        on_notification_clicked();
    }
}

std::expected<void, UiError> WindowsTrayIcon::open_url(const std::string& url) {
    if (url.empty()) {
        return std::unexpected(UiError::OperationFailed);
    }

    std::string msg = "Opening URL: " + url;
    PLEX_LOG_INFO(m_component_name, msg);

    std::wstring wurl = utf8_to_wstring(url);
    if (wurl.empty()) {
        return std::unexpected(UiError::OperationFailed);
    }

    INT_PTR result = reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL
    ));

    if (result <= 32) { // ShellExecute returns <= 32 for errors
        std::string error_msg = "Failed to open URL, error code: " + std::to_string(result);
        PLEX_LOG_ERROR(m_component_name, error_msg);
        return std::unexpected(UiError::OperationFailed);
    }

    PLEX_LOG_INFO(m_component_name, "URL opened successfully");
    return {};
}

template<typename Func>
bool WindowsTrayIcon::execute_with_timeout(Func func, std::chrono::milliseconds timeout, const std::string& operation_name) {
    auto future = std::async(std::launch::async, func);

    if (future.wait_for(timeout) == std::future_status::timeout) {
        std::string msg = "Operation '" + operation_name + "' timed out after " +
                         std::to_string(timeout.count()) + "ms";
        PLEX_LOG_WARNING(m_component_name, msg);
        return false;
    }

    try {
        future.get(); // This will rethrow any exception that occurred in the async task
        return true;
    } catch (const std::exception& e) {
        std::string msg = "Exception in operation '" + operation_name + "': " + e.what();
        PLEX_LOG_ERROR(m_component_name, msg);
        return false;
    }
}

LRESULT CALLBACK WindowsTrayIcon::window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (!s_instance) {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    switch (message) {
    case WM_CREATE:
        PLEX_LOG_DEBUG(s_instance->m_component_name, "Window created");
        break;

    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
            PLEX_LOG_DEBUG(s_instance->m_component_name, "Tray icon left clicked");
            s_instance->on_click();
            break;
        case WM_LBUTTONDBLCLK:
            PLEX_LOG_DEBUG(s_instance->m_component_name, "Tray icon double clicked");
            s_instance->on_double_click();
            break;
        case WM_RBUTTONUP:
            PLEX_LOG_DEBUG(s_instance->m_component_name, "Tray icon right clicked");
            if (s_instance->m_context_menu) {
                POINT cursor_pos;
                GetCursorPos(&cursor_pos);
                SetForegroundWindow(hwnd);

                UINT command = TrackPopupMenu(
                    s_instance->m_context_menu,
                    TPM_RETURNCMD | TPM_NONOTIFY,
                    cursor_pos.x, cursor_pos.y,
                    0, hwnd, nullptr
                );

                if (command != 0) {
                    std::string menu_id = s_instance->get_menu_item_id_by_command_id(command);
                    if (!menu_id.empty()) {
                        s_instance->on_menu_item_selected(menu_id);
                    }
                }
            }
            break;
        case NIN_BALLOONUSERCLICK:
            PLEX_LOG_DEBUG(s_instance->m_component_name, "Balloon notification clicked");
            s_instance->on_balloon_clicked();
            break;
        }
        break;

    case WM_CLOSE:
    case WM_DESTROY:
        PLEX_LOG_DEBUG(s_instance->m_component_name, "Window destroyed");
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    return 0;
}

void WindowsTrayIcon::ui_thread_function() {
    m_ui_thread_id = std::this_thread::get_id();
    PLEX_LOG_DEBUG(m_component_name, "UI thread started");

    auto result = create_window();
    if (!result) {
        PLEX_LOG_ERROR(m_component_name, "Failed to create window in UI thread");
        return;
    }

    // Message loop
    MSG msg;
    while (m_running && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    destroy_window();
    PLEX_LOG_DEBUG(m_component_name, "UI thread exiting");
}

std::expected<void, UiError> WindowsTrayIcon::create_window() {
    const wchar_t* class_name = L"PresenceForPlexTrayWin";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    if (!RegisterClassExW(&wc)) {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            std::string msg = "Failed to register window class, error: " + std::to_string(error);
            PLEX_LOG_ERROR(m_component_name, msg);
            return std::unexpected(UiError::InitializationFailed);
        }
    }

    m_window_handle = CreateWindowExW(
        0, class_name, L"PresenceForPlex",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        10, 10,
        nullptr, nullptr,
        GetModuleHandle(nullptr), nullptr
    );

    if (!m_window_handle) {
        DWORD error = GetLastError();
        std::string msg = "Failed to create window, error: " + std::to_string(error);
        PLEX_LOG_ERROR(m_component_name, msg);
        return std::unexpected(UiError::InitializationFailed);
    }

    // Keep window hidden
    ShowWindow(m_window_handle, SW_HIDE);
    UpdateWindow(m_window_handle);

    // Initialize tray icon data
    ZeroMemory(&m_notify_icon_data, sizeof(m_notify_icon_data));
    m_notify_icon_data.cbSize = sizeof(NOTIFYICONDATAW);
    m_notify_icon_data.hWnd = m_window_handle;
    m_notify_icon_data.uID = TRAY_ICON_ID;
    m_notify_icon_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_notify_icon_data.uCallbackMessage = WM_TRAYICON;

    // Set default icon and tooltip
    auto icon_result = load_icon_from_resource(101); // IDI_APPICON
    if (icon_result) {
        m_current_icon = icon_result.value();
        m_notify_icon_data.hIcon = m_current_icon;
    } else {
        m_current_icon = LoadIcon(nullptr, IDI_APPLICATION);
        m_notify_icon_data.hIcon = m_current_icon;
    }

    wcscpy_s(m_notify_icon_data.szTip, _countof(m_notify_icon_data.szTip), L"Presence For Plex");

    PLEX_LOG_DEBUG(m_component_name, "Window created successfully");
    return {};
}

void WindowsTrayIcon::destroy_window() {
    if (m_context_menu) {
        DestroyMenu(m_context_menu);
        m_context_menu = nullptr;
    }

    if (m_current_icon && m_current_icon != LoadIcon(nullptr, IDI_APPLICATION)) {
        DestroyIcon(m_current_icon);
        m_current_icon = nullptr;
    }

    if (m_window_handle) {
        DestroyWindow(m_window_handle);
        m_window_handle = nullptr;
    }

    PLEX_LOG_DEBUG(m_component_name, "Window destroyed");
}

void WindowsTrayIcon::build_context_menu() {
    if (m_context_menu) {
        DestroyMenu(m_context_menu);
    }

    m_context_menu = CreatePopupMenu();
    if (!m_context_menu) {
        PLEX_LOG_ERROR(m_component_name, "Failed to create context menu");
        return;
    }

    UINT current_id = MENU_ITEM_BASE_ID;
    add_menu_items_to_hmenu(m_context_menu, m_menu_items, current_id);
}

void WindowsTrayIcon::add_menu_items_to_hmenu(HMENU menu, const std::vector<MenuItem>& items, UINT& current_id) {
    for (const auto& item : items) {
        switch (item.type) {
        case MenuItemType::Separator:
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            break;

        case MenuItemType::Action:
        case MenuItemType::Checkbox:
        case MenuItemType::Radio: {
            UINT flags = MF_STRING;
            if (!item.enabled) {
                flags |= MF_DISABLED | MF_GRAYED;
            }
            if (item.checked && (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio)) {
                flags |= MF_CHECKED;
            }

            std::wstring wlabel = utf8_to_wstring(item.label);
            AppendMenuW(menu, flags, current_id, wlabel.c_str());

            m_command_id_to_menu_id[current_id] = item.id;
            m_menu_id_to_command_id[item.id] = current_id;
            current_id++;
            break;
        }

        case MenuItemType::Submenu: {
            HMENU submenu = CreatePopupMenu();
            add_menu_items_to_hmenu(submenu, item.submenu, current_id);

            std::wstring wlabel = utf8_to_wstring(item.label);
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), wlabel.c_str());
            break;
        }
        }
    }
}

std::string WindowsTrayIcon::get_menu_item_id_by_command_id(UINT command_id) const {
    auto it = m_command_id_to_menu_id.find(command_id);
    return it != m_command_id_to_menu_id.end() ? it->second : "";
}

std::expected<HICON, UiError> WindowsTrayIcon::load_icon_from_path(const std::string& path) {
    std::wstring wpath = utf8_to_wstring(path);

    HICON icon = static_cast<HICON>(LoadImageW(
        nullptr, wpath.c_str(),
        IMAGE_ICON, 0, 0,
        LR_LOADFROMFILE | LR_DEFAULTSIZE
    ));

    if (!icon) {
        DWORD error = GetLastError();
        std::string msg = "Failed to load icon from path: " + path + ", error: " + std::to_string(error);
        PLEX_LOG_ERROR(m_component_name, msg);
        return std::unexpected(UiError::ResourceNotFound);
    }

    return icon;
}

std::expected<HICON, UiError> WindowsTrayIcon::load_icon_from_resource(int resource_id) {
    HMODULE module = GetModuleHandle(nullptr);
    HICON icon = nullptr;

    // First try loading by resource ID
    icon = LoadIconW(module, MAKEINTRESOURCEW(resource_id));

    // If that fails, try loading by name (for backward compatibility)
    if (!icon) {
        std::string msg = "Failed to load icon by ID " + std::to_string(resource_id) + ", trying by name";
        PLEX_LOG_INFO(m_component_name, msg);

        std::string resource_name = "IDI_APPICON";
        std::wstring wresource_name = utf8_to_wstring(resource_name);
        icon = LoadIconW(module, wresource_name.c_str());
    }

    // If still no icon, try the default system icon
    if (!icon) {
        DWORD error = GetLastError();
        std::string msg = "Failed to load application icon, error: " + std::to_string(error) +
                         ", using default system icon";
        PLEX_LOG_WARNING(m_component_name, msg);

        icon = LoadIcon(nullptr, IDI_APPLICATION);
        if (!icon) {
            DWORD sys_error = GetLastError();
            std::string error_msg = "Failed to load even default system icon, error: " + std::to_string(sys_error);
            PLEX_LOG_ERROR(m_component_name, error_msg);
            return std::unexpected(UiError::ResourceNotFound);
        }

        PLEX_LOG_INFO(m_component_name, "Using default system icon");
    } else {
        std::string msg = "Application icon loaded successfully from resource " + std::to_string(resource_id);
        PLEX_LOG_INFO(m_component_name, msg);
    }

    return icon;
}

std::wstring WindowsTrayIcon::utf8_to_wstring(const std::string& utf8_str) const {
    if (utf8_str.empty()) {
        return L"";
    }

    int length = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, nullptr, 0);
    if (length == 0) {
        return L"";
    }

    std::wstring wstr(length - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), -1, &wstr[0], length);

    return wstr;
}

bool WindowsTrayIcon::is_ui_thread() const {
    return std::this_thread::get_id() == m_ui_thread_id;
}

} // namespace windows
} // namespace platform
} // namespace presence_for_plex

#endif // _WIN32