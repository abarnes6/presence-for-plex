#include "trayicon.h"
#include "resources.h"

#ifdef _WIN32
// Static instance pointer for Windows callback
TrayIcon* TrayIcon::s_instance = nullptr;

TrayIcon::TrayIcon(const std::string& appName) 
    : m_appName(appName), m_running(false), m_hWnd(NULL), m_hMenu(NULL) {
    // Set static instance for callback
    s_instance = this;
    
    // Start UI thread
    m_running = true;
    m_uiThread = std::thread(&TrayIcon::uiThreadFunction, this);
    
    // Wait for window to be created before returning
    for (int i = 0; i < 50 && m_hWnd == NULL; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    if (m_hWnd == NULL) {
        LOG_ERROR("TrayIcon", "Failed to create window in time");
    }
}

TrayIcon::~TrayIcon() {
    // Clean up resources
    hide();
    m_running = false;
    
    // If there's a window, send a message to destroy it
    if (m_hWnd) {
        PostMessage(m_hWnd, WM_CLOSE, 0, 0);
    }
    
    // Wait for UI thread to finish
    if (m_uiThread.joinable()) {
        m_uiThread.join();
    }
    
    s_instance = nullptr;
}

void TrayIcon::show() {
    if (m_hWnd && m_nid.cbSize > 0) {
        LOG_INFO("TrayIcon", "Adding tray icon");
        if (!Shell_NotifyIconW(NIM_ADD, &m_nid)) {
            DWORD error = GetLastError();
            LOG_ERROR_STREAM("TrayIcon", "Failed to show tray icon, error code: " << error);
        } else {
            LOG_INFO("TrayIcon", "Tray icon shown successfully");
        }
    } else {
        LOG_ERROR_STREAM("TrayIcon", "Cannot show tray icon, window handle: " 
            << (m_hWnd ? "valid" : "NULL") << ", nid size: " << m_nid.cbSize);
    }
}

void TrayIcon::hide() {
    if (m_hWnd && m_nid.cbSize > 0) {
        LOG_INFO("TrayIcon", "Removing tray icon");
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
    }
}

void TrayIcon::setTooltip(const std::string& tooltip) {
    if (m_nid.cbSize > 0) {
        // Convert UTF-8 string to wide string
        int length = MultiByteToWideChar(CP_UTF8, 0, tooltip.c_str(), -1, NULL, 0);
        if (length > 0 && length <= 128) { // 128 is the max size for tooltip
            MultiByteToWideChar(CP_UTF8, 0, tooltip.c_str(), -1, m_nid.szTip, length);
            
            // Update the tooltip
            if (m_hWnd) {
                LOG_DEBUG_STREAM("TrayIcon", "Updating tooltip to: " << tooltip);
                Shell_NotifyIconW(NIM_MODIFY, &m_nid);
            }
        }
    }
}

bool TrayIcon::setIcon(HICON hIcon) {
    if (m_nid.cbSize > 0 && hIcon) {
        m_nid.hIcon = hIcon;
        
        if (m_hWnd) {
            LOG_INFO("TrayIcon", "Updating tray icon");
            return Shell_NotifyIconW(NIM_MODIFY, &m_nid) ? true : false;
        }
    }
    return false;
}

void TrayIcon::setExitCallback(std::function<void()> callback) {
    m_exitCallback = callback;
}

// Static window procedure
LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // Access the instance
    TrayIcon* instance = s_instance;
    if (!instance) {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    
    switch (message) {
        case WM_CREATE:
            // Create the icon menu
            instance->m_hMenu = CreatePopupMenu();
            AppendMenuW(instance->m_hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
            break;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_EXIT:
                    LOG_INFO("TrayIcon", "Exit selected from menu via WM_COMMAND");
                    if (instance->m_exitCallback) {
                        instance->m_exitCallback();
                    }
                    break;
            }
            break;
            
        case WM_TRAYICON:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP) {
                LOG_DEBUG_STREAM("TrayIcon", "Tray icon clicked: " << LOWORD(lParam));
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd); 
                UINT clicked = TrackPopupMenu(
                    instance->m_hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                    pt.x, pt.y, 0, hwnd, NULL
                );
                
                if (clicked == ID_TRAY_EXIT) {
                    LOG_INFO("TrayIcon", "Exit selected from tray menu");
                    if (instance->m_exitCallback) {
                        instance->m_exitCallback();
                    }
                }
            }
            break;
            
        case WM_CLOSE:
        case WM_DESTROY:
            LOG_INFO("TrayIcon", "Window destroyed");
            instance->m_running = false;
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

void TrayIcon::uiThreadFunction() {
    // Register window class
    const wchar_t* className = L"PlexRichPresenceTray";
    
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = className;
    
    // Load icon with error checking - try multiple methods
    HICON hIcon = NULL;
    
    // First try loading by resource ID
    hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APPICON));
    
    // If that fails, try loading by name
    if (!hIcon) {
        LOG_INFO("TrayIcon", "Failed to load icon by ID, trying by name");
        hIcon = LoadIconW(GetModuleHandle(NULL), L"IDI_APPICON");
    }
    
    // If still no icon, try the default system icon
    if (!hIcon) {
        DWORD error = GetLastError();
        LOG_ERROR_STREAM("TrayIcon", "Failed to load application icon, error code: " << error);
        // Try to load a default system icon instead
        hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(IDI_APPLICATION));
        LOG_INFO("TrayIcon", "Using default system icon instead");
    } else {
        LOG_INFO("TrayIcon", "Application icon loaded successfully");
    }
    
    wc.hIcon = hIcon;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(IDC_ARROW));
    
    if (!RegisterClassExW(&wc)) {
        DWORD error = GetLastError();
        LOG_ERROR_STREAM("TrayIcon", "Failed to register window class, error code: " << error);
        return;
    }
    
    // Convert app name to wide string
    std::wstring wAppName;
    int length = MultiByteToWideChar(CP_UTF8, 0, m_appName.c_str(), -1, NULL, 0);
    if (length > 0) {
        wAppName.resize(length);
        MultiByteToWideChar(CP_UTF8, 0, m_appName.c_str(), -1, &wAppName[0], length);
    } else {
        wAppName = L"Plex Rich Presence";
    }
    
    // Create the hidden window
    m_hWnd = CreateWindowExW(
        0,
        className, wAppName.c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        10, 10, NULL, NULL, GetModuleHandle(NULL), NULL
    );
    
    if (!m_hWnd) {
        DWORD error = GetLastError();
        LOG_ERROR_STREAM("TrayIcon", "Failed to create window, error code: " << error);
        return;
    }
    
    // Keep window hidden
    ShowWindow(m_hWnd, SW_HIDE);
    UpdateWindow(m_hWnd);
    
    // Initialize tray icon
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = ID_TRAY_APP_ICON;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    
    // Use the same icon for the tray as we used for the window
    m_nid.hIcon = hIcon;
    
    // If we still don't have an icon, try one more time specifically for the tray
    if (!m_nid.hIcon) {
        LOG_INFO("TrayIcon", "Trying to load tray icon separately");
        m_nid.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APPICON));
        
        if (!m_nid.hIcon) {
            DWORD error = GetLastError();
            LOG_ERROR_STREAM("TrayIcon", "Failed to load tray icon, error code: " << error);
            // Try to load a default system icon instead
            m_nid.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(IDI_APPLICATION));
            LOG_INFO("TrayIcon", "Using default system icon for tray");
        } else {
            LOG_INFO("TrayIcon", "Tray icon loaded successfully");
        }
    }
    
    // Set initial tooltip
    wcscpy_s(m_nid.szTip, _countof(m_nid.szTip), L"Plex Rich Presence");
    
    LOG_INFO("TrayIcon", "Tray icon initialized, ready to be shown");
    
    // Message loop
    MSG msg;
    while (m_running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    LOG_INFO("TrayIcon", "UI thread exiting");
}

#elif defined(__linux__)
// Linux implementation using AppIndicator

// Callback for quit menu item
void TrayIcon::quit_activate(GtkMenuItem *item, gpointer user_data) {
    TrayIcon* tray = static_cast<TrayIcon*>(user_data);
    if (tray && tray->exitCallback) {
        tray->exitCallback();
    }
}

TrayIcon::TrayIcon(const std::string& appName) : initialized(false) {
    LOG_DEBUG("TrayIcon", "Creating tray icon for Linux");
    
    // Initialize GTK if not already initialized
    if (!gtk_init_check(nullptr, nullptr)) {
        LOG_ERROR("TrayIcon", "Failed to initialize GTK");
        return;
    }
    
    // Create menu
    menu = gtk_menu_new();
    quit_item = gtk_menu_item_new_with_label("Exit");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
    g_signal_connect(quit_item, "activate", G_CALLBACK(quit_activate), this);
    gtk_widget_show_all(menu);
    
    // Create app indicator
    indicator = app_indicator_new("plex-rich-presence", 
                                 "indicator-messages", 
                                 APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    
    // Set properties
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_menu(indicator, GTK_MENU(menu));
    
    // Try to set the icon to the app icon
    app_indicator_set_icon(indicator, "plex-rich-presence");
    
    initialized = true;
    LOG_INFO("TrayIcon", "Linux tray icon created successfully");
}

TrayIcon::~TrayIcon() {
    if (initialized) {
        LOG_DEBUG("TrayIcon", "Destroying Linux tray icon");
        g_object_unref(indicator);
        gtk_widget_destroy(menu);
    }
}

void TrayIcon::show() {
    if (initialized) {
        LOG_DEBUG("TrayIcon", "Showing Linux tray icon");
        app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    }
}

void TrayIcon::hide() {
    if (initialized) {
        LOG_DEBUG("TrayIcon", "Hiding Linux tray icon");
        app_indicator_set_status(indicator, APP_INDICATOR_STATUS_PASSIVE);
    }
}

void TrayIcon::setTooltip(const std::string& tip) {
    tooltip = tip;
}

void TrayIcon::setExitCallback(std::function<void()> callback) {
    exitCallback = callback;
}

bool TrayIcon::setIcon(HICON hIcon) {
    // Not applicable for Linux
    return false;
}

#endif