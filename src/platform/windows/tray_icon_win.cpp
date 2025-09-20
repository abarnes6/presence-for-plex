#ifdef _WIN32

#include "presence_for_plex/platform/windows/tray_icon_win.hpp"
#include <VersionHelpers.h>
#include <format>
#include <print>
#include <ranges>

namespace presence_for_plex::platform::windows {

WindowsTrayIcon::WindowsTrayIcon() {
    // Register TaskbarCreated message for explorer restart handling
    s_taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    PLEX_LOG_DEBUG(component_name_, "WindowsTrayIcon constructed");
}

WindowsTrayIcon::~WindowsTrayIcon() {
    PLEX_LOG_DEBUG(component_name_, "WindowsTrayIcon destructor called");
    shutdown();
}

std::expected<void, UiError> WindowsTrayIcon::initialize() {
    std::lock_guard lock(mutex_);

    if (initialized_) {
        PLEX_LOG_DEBUG(component_name_, "Already initialized");
        return {};
    }

    PLEX_LOG_INFO(component_name_, "Initializing Windows 11 tray icon");

    // Set DPI awareness for Windows 11
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Initialize common controls for modern visuals
    INITCOMMONCONTROLSEX icc{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_STANDARD_CLASSES
    };
    InitCommonControlsEx(&icc);

    running_ = true;

    // Start UI thread with jthread
    ui_thread_ = std::jthread([this] { ui_thread_function(); });

    // Wait for window creation with proper synchronization
    std::unique_lock init_lock(init_mutex_);
    if (!init_cv_.wait_for(init_lock, std::chrono::seconds(5),
                           [this] { return window_created_ || !running_; })) {
        PLEX_LOG_ERROR(component_name_, "Window creation timed out");
        running_ = false;
        return std::unexpected(UiError::InitializationFailed);
    }

    if (!window_handle_) {
        PLEX_LOG_ERROR(component_name_, "Window creation failed");
        return std::unexpected(UiError::InitializationFailed);
    }

    initialized_ = true;
    PLEX_LOG_INFO(component_name_, "Windows 11 tray icon initialized successfully");
    return {};
}

void WindowsTrayIcon::shutdown() {
    std::lock_guard lock(mutex_);

    if (!initialized_) {
        return;
    }

    PLEX_LOG_INFO(component_name_, "Shutting down tray icon");

    hide();
    running_ = false;

    if (window_handle_) {
        PostMessage(window_handle_, WM_CLOSE, 0, 0);
    }

    // jthread will auto-join in destructor, but we can request stop
    if (ui_thread_.joinable()) {
        ui_thread_.request_stop();
    }

    initialized_ = false;
    PLEX_LOG_INFO(component_name_, "Tray icon shut down");
}

bool WindowsTrayIcon::is_initialized() const {
    return initialized_;
}

std::expected<void, UiError> WindowsTrayIcon::set_icon(const std::string& icon_path) {
    std::lock_guard lock(mutex_);

    if (!initialized_) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto icon_result = load_icon_from_path(icon_path);
    if (!icon_result) {
        PLEX_LOG_ERROR(component_name_,
                      std::format("Failed to load icon: {}", icon_result.error().message));
        return std::unexpected(icon_result.error().error);
    }

    current_icon_ = std::move(icon_result.value());
    notify_icon_data_.hIcon = current_icon_.get();

    if (visible_) {
        if (!Shell_NotifyIconW(NIM_MODIFY, &notify_icon_data_)) {
            auto error = GetLastError();
            PLEX_LOG_ERROR(component_name_,
                          std::format("Failed to update tray icon, error: {}", error));
            return std::unexpected(UiError::OperationFailed);
        }
    }

    PLEX_LOG_DEBUG(component_name_, std::format("Icon set from path: {}", icon_path));
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::set_icon_from_resource(int resource_id) {
    std::lock_guard lock(mutex_);

    if (!initialized_) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto icon_result = load_icon_from_resource(resource_id);
    if (!icon_result) {
        return std::unexpected(icon_result.error().error);
    }

    current_icon_ = std::move(icon_result.value());
    notify_icon_data_.hIcon = current_icon_.get();

    if (visible_) {
        if (!Shell_NotifyIconW(NIM_MODIFY, &notify_icon_data_)) {
            auto error = GetLastError();
            PLEX_LOG_ERROR(component_name_,
                          std::format("Failed to update tray icon, error: {}", error));
            return std::unexpected(UiError::OperationFailed);
        }
    }

    PLEX_LOG_DEBUG(component_name_, std::format("Icon set from resource: {}", resource_id));
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::set_tooltip(const std::string& tooltip) {
    std::lock_guard lock(mutex_);

    if (!initialized_) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto wide_tooltip = to_wide_string(tooltip);
    safe_copy(notify_icon_data_.szTip, wide_tooltip);

    if (visible_) {
        if (!Shell_NotifyIconW(NIM_MODIFY, &notify_icon_data_)) {
            auto error = GetLastError();
            PLEX_LOG_ERROR(component_name_,
                          std::format("Failed to update tooltip, error: {}", error));
            return std::unexpected(UiError::OperationFailed);
        }
    }

    PLEX_LOG_DEBUG(component_name_, std::format("Tooltip set: {}", tooltip));
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::set_menu(const std::vector<MenuItem>& items) {
    std::lock_guard lock(mutex_);

    if (!initialized_) {
        return std::unexpected(UiError::InitializationFailed);
    }

    menu_items_ = items;
    command_to_menu_id_.clear();
    menu_id_to_command_.clear();
    next_command_id_ = TrayConstants::MENU_ITEM_BASE_ID;

    rebuild_context_menu();

    PLEX_LOG_DEBUG(component_name_, std::format("Menu set with {} items", items.size()));
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::update_menu_item(
    const std::string& id, const MenuItem& item) {
    std::lock_guard lock(mutex_);

    if (!initialized_) {
        return std::unexpected(UiError::InitializationFailed);
    }

    // Find and update the menu item using ranges
    auto it = std::ranges::find_if(menu_items_,
        [&id](const auto& mi) { return mi.id == id; });

    if (it != menu_items_.end()) {
        *it = item;
        it->id = id; // Preserve the ID
        rebuild_context_menu();
        PLEX_LOG_DEBUG(component_name_, std::format("Menu item updated: {}", id));
    }

    return {};
}

std::expected<void, UiError> WindowsTrayIcon::enable_menu_item(
    const std::string& id, bool enabled) {
    std::lock_guard lock(mutex_);

    if (!initialized_) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto cmd_it = menu_id_to_command_.find(id);
    if (cmd_it == menu_id_to_command_.end()) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    UINT flags = enabled ? MF_ENABLED : (MF_DISABLED | MF_GRAYED);
    if (!EnableMenuItem(context_menu_.get(), cmd_it->second, flags)) {
        return std::unexpected(UiError::OperationFailed);
    }

    // Update stored menu item
    auto item_it = std::ranges::find_if(menu_items_,
        [&id](const auto& mi) { return mi.id == id; });
    if (item_it != menu_items_.end()) {
        item_it->enabled = enabled;
    }

    PLEX_LOG_DEBUG(component_name_,
                  std::format("Menu item {} {}", id, enabled ? "enabled" : "disabled"));
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::check_menu_item(
    const std::string& id, bool checked) {
    std::lock_guard lock(mutex_);

    if (!initialized_) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto cmd_it = menu_id_to_command_.find(id);
    if (cmd_it == menu_id_to_command_.end()) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    UINT flags = checked ? MF_CHECKED : MF_UNCHECKED;
    if (!CheckMenuItem(context_menu_.get(), cmd_it->second, flags)) {
        return std::unexpected(UiError::OperationFailed);
    }

    // Update stored menu item
    auto item_it = std::ranges::find_if(menu_items_,
        [&id](const auto& mi) { return mi.id == id; });
    if (item_it != menu_items_.end()) {
        item_it->checked = checked;
    }

    PLEX_LOG_DEBUG(component_name_,
                  std::format("Menu item {} {}", id, checked ? "checked" : "unchecked"));
    return {};
}

void WindowsTrayIcon::set_click_callback(ClickCallback callback) {
    std::lock_guard lock(mutex_);
    click_callback_ = std::move(callback);
}

void WindowsTrayIcon::set_double_click_callback(ClickCallback callback) {
    std::lock_guard lock(mutex_);
    double_click_callback_ = std::move(callback);
}

void WindowsTrayIcon::set_menu_callback(MenuCallback callback) {
    std::lock_guard lock(mutex_);
    menu_callback_ = std::move(callback);
}

void WindowsTrayIcon::show() {
    std::lock_guard lock(mutex_);

    if (!initialized_ || visible_) {
        return;
    }

    if (!Shell_NotifyIconW(NIM_ADD, &notify_icon_data_)) {
        auto error = GetLastError();
        PLEX_LOG_ERROR(component_name_,
                      std::format("Failed to show tray icon, error: {}", error));
        return;
    }

    // Set version for enhanced Windows 11 features
    NOTIFYICONDATAW version_data{
        .cbSize = sizeof(NOTIFYICONDATAW),
        .hWnd = window_handle_,
        .uID = TrayConstants::TRAY_ICON_ID,
        .uVersion = NOTIFYICON_VERSION_4
    };
    Shell_NotifyIconW(NIM_SETVERSION, &version_data);

    visible_ = true;
    icon_added_ = true;
    PLEX_LOG_INFO(component_name_, "Tray icon shown with Windows 11 enhancements");
}

void WindowsTrayIcon::hide() {
    std::lock_guard lock(mutex_);

    if (!visible_) {
        return;
    }

    if (!Shell_NotifyIconW(NIM_DELETE, &notify_icon_data_)) {
        auto error = GetLastError();
        PLEX_LOG_ERROR(component_name_,
                      std::format("Failed to hide tray icon, error: {}", error));
    }

    visible_ = false;
    icon_added_ = false;
    PLEX_LOG_INFO(component_name_, "Tray icon hidden");
}

bool WindowsTrayIcon::is_visible() const {
    return visible_;
}

TrayResult<> WindowsTrayIcon::show_notification(
    std::string_view title, std::string_view message, bool is_error,
    std::chrono::seconds timeout) {

    std::lock_guard lock(mutex_);

    if (!initialized_ || !visible_) {
        return std::unexpected(TrayErrorInfo{
            UiError::InitializationFailed,
            "Tray icon not initialized or visible"
        });
    }

    auto wtitle = to_wide_string(std::string(title));
    auto wmessage = to_wide_string(std::string(message));

    NOTIFYICONDATAW nid{
        .cbSize = sizeof(NOTIFYICONDATAW),
        .hWnd = window_handle_,
        .uID = TrayConstants::TRAY_ICON_ID,
        .uFlags = NIF_INFO,
        .uTimeout = static_cast<UINT>(timeout.count()) * 1000,
        .dwInfoFlags = static_cast<DWORD>(is_error ? NIIF_ERROR : (NIIF_INFO | NIIF_LARGE_ICON))
    };

    safe_copy(nid.szInfoTitle, wtitle);
    safe_copy(nid.szInfo, wmessage);

    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        return std::unexpected(TrayErrorInfo{
            UiError::OperationFailed,
            std::format("Failed to show notification: {}", GetLastError())
        });
    }

    PLEX_LOG_DEBUG(component_name_, std::format("Notification shown: {}", title));
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::show_balloon_notification(
    const std::string& title,
    const std::string& message,
    bool is_error,
    std::chrono::seconds timeout) {

    auto result = show_notification(title, message, is_error, timeout);
    if (!result) {
        return std::unexpected(result.error().error);
    }
    return {};
}

std::expected<void, UiError> WindowsTrayIcon::show_update_notification(
    const std::string& title,
    const std::string& message,
    const std::string& download_url) {

    // For now, just show a regular notification
    // In a full implementation, you'd store the download_url for handling clicks
    auto result = show_notification(title,
                                   std::format("{}\n\nDownload: {}", message, download_url),
                                   false);
    if (!result) {
        return std::unexpected(result.error().error);
    }
    return {};
}

void WindowsTrayIcon::on_click() {
    if (click_callback_) {
        click_callback_();
    }
}

void WindowsTrayIcon::on_double_click() {
    if (double_click_callback_) {
        double_click_callback_();
    }
}

void WindowsTrayIcon::on_menu_item_selected(const std::string& item_id) {
    if (menu_callback_) {
        menu_callback_(item_id);
    }

    // Also call the item's action if it has one
    auto it = std::ranges::find_if(menu_items_,
        [&item_id](const auto& item) { return item.id == item_id; });

    if (it != menu_items_.end() && it->action) {
        try {
            it->action();
        } catch (const std::exception& e) {
            PLEX_LOG_ERROR(component_name_,
                          std::format("Exception in menu action for {}: {}", item_id, e.what()));
        }
    }
}

LRESULT CALLBACK WindowsTrayIcon::window_proc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {

    WindowsTrayIcon* instance = nullptr;

    if (message == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        instance = static_cast<WindowsTrayIcon*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
    } else {
        instance = reinterpret_cast<WindowsTrayIcon*>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (instance) {
        return instance->handle_message(hwnd, message, wParam, lParam);
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

LRESULT WindowsTrayIcon::handle_message(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

    // Handle TaskbarCreated message for explorer restart
    if (msg == s_taskbar_created_message) {
        PLEX_LOG_INFO(component_name_, "Explorer restarted, recreating tray icon");
        recreate_tray_icon();
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        PLEX_LOG_DEBUG(component_name_, "Window created");
        return 0;

    case TrayConstants::WM_TRAYICON:
        handle_tray_message(lParam);
        return 0;

    case WM_COMMAND:
        handle_menu_command(LOWORD(wParam));
        return 0;

    case WM_CLOSE:
    case WM_DESTROY:
        PLEX_LOG_DEBUG(component_name_, "Window destroyed");
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void WindowsTrayIcon::handle_tray_message(LPARAM lParam) {
    switch (LOWORD(lParam)) {
    case WM_LBUTTONUP:
        PLEX_LOG_DEBUG(component_name_, "Tray icon clicked");
        on_click();
        break;

    case WM_LBUTTONDBLCLK:
        PLEX_LOG_DEBUG(component_name_, "Tray icon double-clicked");
        on_double_click();
        break;

    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        PLEX_LOG_DEBUG(component_name_, "Showing context menu");
        if (context_menu_) {
            POINT cursor_pos;
            GetCursorPos(&cursor_pos);
            SetForegroundWindow(window_handle_);

            UINT command = TrackPopupMenu(
                context_menu_.get(),
                TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                cursor_pos.x, cursor_pos.y,
                0, window_handle_, nullptr
            );

            // Ensure menu dismisses properly (Windows 11)
            PostMessage(window_handle_, WM_NULL, 0, 0);

            if (command != 0) {
                handle_menu_command(command);
            }
        }
        break;

    case NIN_BALLOONUSERCLICK:
        PLEX_LOG_DEBUG(component_name_, "Balloon notification clicked");
        // Handle notification click if needed
        break;
    }
}

void WindowsTrayIcon::handle_menu_command(UINT command_id) {
    auto it = command_to_menu_id_.find(command_id);
    if (it != command_to_menu_id_.end()) {
        on_menu_item_selected(it->second);
    }
}

void WindowsTrayIcon::ui_thread_function() {
    ui_thread_id_ = std::this_thread::get_id();
    PLEX_LOG_DEBUG(component_name_, "UI thread started");

    auto result = create_window();
    if (!result) {
        PLEX_LOG_ERROR(component_name_,
                      std::format("Failed to create window: {}", result.error().message));
        std::lock_guard lock(init_mutex_);
        window_created_ = false;
        init_cv_.notify_one();
        return;
    }

    // Setup tray icon
    auto tray_result = setup_tray_icon();
    if (!tray_result) {
        PLEX_LOG_ERROR(component_name_,
                      std::format("Failed to setup tray icon: {}", tray_result.error().message));
    }

    // Notify that window is created
    {
        std::lock_guard lock(init_mutex_);
        window_created_ = true;
    }
    init_cv_.notify_one();

    // Message loop
    MSG msg;
    while (running_ && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    if (context_menu_) {
        context_menu_.reset();
    }
    if (current_icon_) {
        current_icon_ = IconHandle{};
    }
    if (window_handle_) {
        DestroyWindow(window_handle_);
        window_handle_ = nullptr;
    }

    PLEX_LOG_DEBUG(component_name_, "UI thread exiting");
}

TrayResult<> WindowsTrayIcon::create_window() {
    const wchar_t* class_name = TrayConstants::WINDOW_CLASS.data();

    WNDCLASSEXW wc{
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = window_proc,
        .hInstance = GetModuleHandle(nullptr),
        .hCursor = LoadCursor(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = class_name
    };

    if (!RegisterClassExW(&wc)) {
        auto error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            return std::unexpected(TrayErrorInfo{
                UiError::InitializationFailed,
                std::format("Failed to register window class: {}", error)
            });
        }
    }

    window_handle_ = CreateWindowExW(
        0, class_name, TrayConstants::APP_NAME.data(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 10, 10,
        nullptr, nullptr,
        GetModuleHandle(nullptr), this
    );

    if (!window_handle_) {
        return std::unexpected(TrayErrorInfo{
            UiError::InitializationFailed,
            std::format("Failed to create window: {}", GetLastError())
        });
    }

    ShowWindow(window_handle_, SW_HIDE);
    UpdateWindow(window_handle_);

    // Get DPI for proper scaling
    current_dpi_ = GetDpiForWindow(window_handle_);

    PLEX_LOG_DEBUG(component_name_, "Window created successfully");
    return {};
}

TrayResult<> WindowsTrayIcon::setup_tray_icon() {
    // Initialize tray icon data with Windows 11 features
    notify_icon_data_ = {
        .cbSize = sizeof(NOTIFYICONDATAW),
        .hWnd = window_handle_,
        .uID = TrayConstants::TRAY_ICON_ID,
        .uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP | NIF_GUID,
        .uCallbackMessage = TrayConstants::WM_TRAYICON,
        .guidItem = TrayConstants::TRAY_ICON_GUID
    };

    // Set default icon with DPI awareness
    auto icon_result = load_icon_from_resource(101); // IDI_APPICON
    if (icon_result) {
        current_icon_ = std::move(icon_result.value());
    } else {
        // Fallback to system icon
        current_icon_.set_system_icon(LoadIcon(nullptr, IDI_APPLICATION));
    }
    notify_icon_data_.hIcon = current_icon_.get();

    // Set default tooltip
    safe_copy(notify_icon_data_.szTip, TrayConstants::APP_NAME);

    PLEX_LOG_DEBUG(component_name_, "Tray icon setup complete");
    return {};
}

void WindowsTrayIcon::recreate_tray_icon() {
    std::lock_guard lock(mutex_);

    if (!icon_added_ || !initialized_) {
        return;
    }

    PLEX_LOG_INFO(component_name_, "Recreating tray icon after explorer restart");

    if (Shell_NotifyIconW(NIM_ADD, &notify_icon_data_)) {
        // Set version for enhanced features
        NOTIFYICONDATAW version_data{
            .cbSize = sizeof(NOTIFYICONDATAW),
            .hWnd = window_handle_,
            .uID = TrayConstants::TRAY_ICON_ID,
            .uVersion = NOTIFYICON_VERSION_4
        };
        Shell_NotifyIconW(NIM_SETVERSION, &version_data);

        visible_ = true;
        PLEX_LOG_INFO(component_name_, "Tray icon recreated successfully");
    } else {
        auto error = GetLastError();
        PLEX_LOG_ERROR(component_name_,
                      std::format("Failed to recreate tray icon, error: {}", error));
    }
}

void WindowsTrayIcon::rebuild_context_menu() {
    context_menu_.reset(CreatePopupMenu());

    if (!context_menu_) {
        PLEX_LOG_ERROR(component_name_, "Failed to create context menu");
        return;
    }

    UINT current_id = TrayConstants::MENU_ITEM_BASE_ID;
    add_menu_items(context_menu_, menu_items_, current_id);
}

void WindowsTrayIcon::add_menu_items(
    MenuHandle& menu, std::span<const MenuItem> items, UINT& current_id) {

    for (const auto& item : items) {
        switch (item.type) {
        case MenuItemType::Separator:
            AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
            break;

        case MenuItemType::Action:
        case MenuItemType::Checkbox:
        case MenuItemType::Radio: {
            UINT flags = MF_STRING;
            if (!item.enabled) {
                flags |= MF_DISABLED | MF_GRAYED;
            }
            if (item.checked && (item.type == MenuItemType::Checkbox ||
                                 item.type == MenuItemType::Radio)) {
                flags |= MF_CHECKED;
            }

            auto wlabel = to_wide_string(item.label);
            AppendMenuW(menu.get(), flags, current_id, wlabel.c_str());

            command_to_menu_id_[current_id] = item.id;
            menu_id_to_command_[item.id] = current_id;
            current_id++;
            break;
        }

        case MenuItemType::Submenu: {
            MenuHandle submenu(CreatePopupMenu());
            add_menu_items(submenu, item.submenu, current_id);

            auto wlabel = to_wide_string(item.label);
            AppendMenuW(menu.get(), MF_POPUP,
                       reinterpret_cast<UINT_PTR>(submenu.release()),
                       wlabel.c_str());
            break;
        }
        }
    }
}

TrayResult<IconHandle> WindowsTrayIcon::load_icon_from_path(std::string_view path) {
    auto wpath = to_wide_string(std::string(path));
    int size = get_dpi_scaled_size(GetSystemMetrics(SM_CXSMICON));

    HICON icon = static_cast<HICON>(LoadImageW(
        nullptr, wpath.c_str(),
        IMAGE_ICON, size, size,
        LR_LOADFROMFILE
    ));

    if (!icon) {
        // Try without specific size
        icon = static_cast<HICON>(LoadImageW(
            nullptr, wpath.c_str(),
            IMAGE_ICON, 0, 0,
            LR_LOADFROMFILE | LR_DEFAULTSIZE
        ));
    }

    if (!icon) {
        return std::unexpected(TrayErrorInfo{
            UiError::ResourceNotFound,
            std::format("Failed to load icon from path: {}", path)
        });
    }

    return IconHandle(icon);
}

TrayResult<IconHandle> WindowsTrayIcon::load_icon_from_resource(int resource_id) {
    HMODULE module = GetModuleHandle(nullptr);
    HICON icon = LoadIconW(module, MAKEINTRESOURCEW(resource_id));

    if (!icon) {
        // Try loading by name for backward compatibility
        icon = LoadIconW(module, L"IDI_APPICON");
    }

    if (!icon) {
        return std::unexpected(TrayErrorInfo{
            UiError::ResourceNotFound,
            std::format("Failed to load icon from resource: {}", resource_id)
        });
    }

    return IconHandle(icon);
}

int WindowsTrayIcon::get_dpi_scaled_size(int base_size) const {
    if (current_dpi_ != 96) {
        return MulDiv(base_size, current_dpi_, 96);
    }
    return base_size;
}

std::wstring WindowsTrayIcon::to_wide_string(std::string_view str) const {
    if (str.empty()) {
        return L"";
    }

    int length = MultiByteToWideChar(CP_UTF8, 0, str.data(),
                                     static_cast<int>(str.size()), nullptr, 0);
    if (length == 0) {
        return L"";
    }

    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(),
                        static_cast<int>(str.size()), result.data(), length);
    return result;
}

bool WindowsTrayIcon::is_ui_thread() const noexcept {
    return std::this_thread::get_id() == ui_thread_id_;
}

} // namespace presence_for_plex::platform::windows

#endif // _WIN32