#include "trayicon.h"

#ifdef _WIN32
// Static instance pointer for Windows callback
TrayIcon *TrayIcon::s_instance = nullptr;

TrayIcon::TrayIcon(const std::string &appName)
    : m_appName(appName), m_running(false), m_hWnd(NULL), m_hMenu(NULL),
      m_connectionStatus("Status: Initializing..."), m_iconShown(false)
{
    // Set static instance for callback
    s_instance = this;

    // Start UI thread
    m_running = true;
    m_uiThread = std::thread(&TrayIcon::uiThreadFunction, this);

    // Wait for window to be created before returning
    for (int i = 0; i < 50 && m_hWnd == NULL; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (m_hWnd == NULL)
    {
        LOG_ERROR("TrayIcon", "Failed to create window in time");
    }
}

TrayIcon::~TrayIcon()
{
    LOG_INFO("TrayIcon", "Destroying tray icon");

    // Make sure we hide the icon first
    if (m_iconShown)
    {
        hide();
    }

    // Clean up resources
    m_running = false;

    // If there's a window, send a message to destroy it
    if (m_hWnd)
    {
        PostMessage(m_hWnd, WM_CLOSE, 0, 0);
    }

    // Wait for UI thread to finish
    if (m_uiThread.joinable())
    {
        m_uiThread.join();
    }

    s_instance = nullptr;
}

void TrayIcon::show()
{
    if (!m_hWnd)
    {
        LOG_ERROR("TrayIcon", "Cannot show tray icon: window handle is NULL");
        return;
    }

    if (m_nid.cbSize == 0)
    {
        LOG_ERROR("TrayIcon", "Cannot show tray icon: notification data not initialized");
        return;
    }

    if (m_iconShown)
    {
        LOG_DEBUG("TrayIcon", "Tray icon already shown, skipping");
        return;
    }

    LOG_INFO("TrayIcon", "Adding tray icon");

    if (!Shell_NotifyIconW(NIM_ADD, &m_nid))
    {
        DWORD error = GetLastError();
        LOG_ERROR_STREAM("TrayIcon", "Failed to show tray icon, error code: " << error);
    }
    else
    {
        LOG_INFO("TrayIcon", "Tray icon shown successfully");
        m_iconShown = true;
    }
}

void TrayIcon::hide()
{
    if (!m_iconShown)
    {
        LOG_DEBUG("TrayIcon", "Tray icon not showing, nothing to hide");
        return;
    }

    if (!m_hWnd)
    {
        LOG_ERROR("TrayIcon", "Cannot hide tray icon: window handle is NULL");
        m_iconShown = false;
        return;
    }

    if (m_nid.cbSize == 0)
    {
        LOG_ERROR("TrayIcon", "Cannot hide tray icon: notification data not initialized");
        m_iconShown = false;
        return;
    }

    LOG_INFO("TrayIcon", "Removing tray icon");

    if (!Shell_NotifyIconW(NIM_DELETE, &m_nid))
    {
        DWORD error = GetLastError();
        LOG_ERROR_STREAM("TrayIcon", "Failed to hide tray icon, error code: " << error);
    }
    else
    {
        LOG_INFO("TrayIcon", "Tray icon hidden successfully");
    }

    m_iconShown = false;
}

void TrayIcon::setExitCallback(std::function<void()> callback)
{
    m_exitCallback = callback;
}

void TrayIcon::setUpdateCheckCallback(std::function<void()> callback)
{
    m_updateCheckCallback = callback;
}

void TrayIcon::executeExitCallback()
{
    if (m_exitCallback)
    {
        // Create a local copy of the callback to avoid potential use-after-free
        auto exitCallback = m_exitCallback;

        // Use ThreadUtils to execute the callback with a timeout
        ThreadUtils::executeWithTimeout(
            [exitCallback]()
            {
                if (exitCallback)
                {
                    exitCallback();
                }
            },
            std::chrono::seconds(5),
            "Exit callback");
    }
}

void TrayIcon::executeUpdateCheckCallback()
{
    if (m_updateCheckCallback)
    {
        // Create a local copy of the callback to avoid potential use-after-free
        auto updateCheckCallback = m_updateCheckCallback;

        // Use ThreadUtils to execute the callback with a timeout
        ThreadUtils::executeWithTimeout(
            [updateCheckCallback]()
            {
                if (updateCheckCallback)
                {
                    updateCheckCallback();
                }
            },
            std::chrono::seconds(5),
            "Update check callback");
    }
}

void TrayIcon::setConnectionStatus(const std::string &status)
{
    if (status == m_connectionStatus)
        return;
    LOG_DEBUG_STREAM("TrayIcon", "Setting connection status: " << status);
    m_connectionStatus = status;
    updateMenu();
}

void TrayIcon::updateMenu()
{
    if (!m_hMenu)
        return;

    // Delete the existing status item if it exists
    RemoveMenu(m_hMenu, ID_TRAY_STATUS, MF_BYCOMMAND);

    // Convert status to wide string
    std::wstring wStatus;
    int length = MultiByteToWideChar(CP_UTF8, 0, m_connectionStatus.c_str(), -1, NULL, 0);
    if (length > 0)
    {
        wStatus.resize(length);
        MultiByteToWideChar(CP_UTF8, 0, m_connectionStatus.c_str(), -1, &wStatus[0], length);
    }
    else
    {
        wStatus = L"Status: Unknown";
    }

    // Insert the status at the top
    InsertMenuW(m_hMenu, 0, MF_BYPOSITION | MF_STRING | MF_DISABLED | MF_GRAYED, ID_TRAY_STATUS, wStatus.c_str());
}

void TrayIcon::showNotification(const std::string &title, const std::string &message, bool isError)
{
    if (!m_hWnd || !m_iconShown)
    {
        LOG_ERROR("TrayIcon", "Cannot show notification: window handle is NULL or icon not shown");
        return;
    }

    // Convert title and message to wide strings
    std::wstring wTitle, wMessage;
    int titleLength = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, NULL, 0);
    int messageLength = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, NULL, 0);

    if (titleLength > 0 && messageLength > 0)
    {
        wTitle.resize(titleLength);
        wMessage.resize(messageLength);

        MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, &wTitle[0], titleLength);
        MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, &wMessage[0], messageLength);
    }
    else
    {
        LOG_ERROR("TrayIcon", "Failed to convert notification text to wide string");
        return;
    }

    // Show the notification in a non-blocking way
    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = m_hWnd;
    nid.uID = m_nid.uID;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = isError ? NIIF_ERROR : NIIF_INFO;

    wcsncpy_s(nid.szInfoTitle, _countof(nid.szInfoTitle), wTitle.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, _countof(nid.szInfo), wMessage.c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_MODIFY, &nid))
    {
        DWORD error = GetLastError();
        LOG_ERROR_STREAM("TrayIcon", "Failed to show notification, error code: " << error);

        // Fall back to a threaded MessageBox if balloon notification fails
        std::thread([wTitle, wMessage, isError]()
                    {
            UINT type = MB_OK | (isError ? MB_ICONERROR : MB_ICONINFORMATION);
            MessageBoxW(NULL, wMessage.c_str(), wTitle.c_str(), type); })
            .detach();
    }
    else
    {
        LOG_INFO("TrayIcon", "Notification shown successfully");
    }
}

void TrayIcon::showUpdateNotification(const std::string &title, const std::string &message, const std::string &downloadUrl)
{
    // Store download URL for later use if notification is clicked
    m_downloadUrl = downloadUrl;
    LOG_DEBUG_STREAM("TrayIcon", "Storing download URL for notification: " << downloadUrl);

    // Use the regular notification system
    showNotification(title, message, false);
}

void TrayIcon::openDownloadUrl()
{
    if (m_downloadUrl.empty())
    {
        LOG_DEBUG("TrayIcon", "No download URL available to open");
        return;
    }

    LOG_INFO_STREAM("TrayIcon", "Opening download URL: " << m_downloadUrl);

    // Convert URL to wide string
    std::wstring wUrl;
    int urlLength = MultiByteToWideChar(CP_UTF8, 0, m_downloadUrl.c_str(), -1, NULL, 0);
    if (urlLength > 0)
    {
        wUrl.resize(urlLength);
        MultiByteToWideChar(CP_UTF8, 0, m_downloadUrl.c_str(), -1, &wUrl[0], urlLength);

        // Open URL in default browser
        INT_PTR result = (INT_PTR)ShellExecuteW(NULL, L"open", wUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        if (result <= 32) // ShellExecute returns a value <= 32 for errors
        {
            LOG_ERROR_STREAM("TrayIcon", "Failed to open URL, error code: " << result);
        }
        else
        {
            LOG_INFO("TrayIcon", "URL opened successfully");
        }
    }
    else
    {
        LOG_ERROR("TrayIcon", "Failed to convert URL to wide string");
    }
}

// Static window procedure
LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Access the instance
    TrayIcon *instance = s_instance;
    if (!instance)
    {
        return DefWindowProc(hwnd, message, wParam, lParam);
    }

    switch (message)
    {
    case WM_CREATE:
        // Create the icon menu
        instance->m_hMenu = CreatePopupMenu();
        // Status will be added dynamically
        AppendMenuW(instance->m_hMenu, MF_SEPARATOR, 0, NULL); // Add separator
        AppendMenuW(instance->m_hMenu, MF_STRING, ID_TRAY_CHECK_UPDATES, L"Check for Updates");
        AppendMenuW(instance->m_hMenu, MF_SEPARATOR, 0, NULL); // Add separator
        AppendMenuW(instance->m_hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

        // Initialize with default status
        if (instance->m_connectionStatus.empty())
        {
            instance->m_connectionStatus = "Status: Initializing...";
        }
        instance->updateMenu();
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_EXIT:
            LOG_INFO("TrayIcon", "Exit selected from menu via WM_COMMAND");
            instance->executeExitCallback();
            break;
        case ID_TRAY_CHECK_UPDATES:
            LOG_INFO("TrayIcon", "Check for updates selected from menu via WM_COMMAND");
            instance->executeUpdateCheckCallback();
            break;
        }
        break;

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP)
        {
            LOG_DEBUG_STREAM("TrayIcon", "Tray icon clicked: " << LOWORD(lParam));

            // Update the menu before showing it
            instance->updateMenu();

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            UINT clicked = TrackPopupMenu(
                instance->m_hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                pt.x, pt.y, 0, hwnd, NULL);

            switch (clicked)
            {
            case ID_TRAY_EXIT:
                LOG_INFO("TrayIcon", "Exit selected from tray menu");
                instance->executeExitCallback();
                break;
            case ID_TRAY_CHECK_UPDATES:
                LOG_INFO("TrayIcon", "Check for updates selected from tray menu");
                instance->executeUpdateCheckCallback();
                break;
            }
        }
        else if (LOWORD(lParam) == NIN_BALLOONUSERCLICK)
        {
            LOG_INFO("TrayIcon", "Notification balloon clicked");
            instance->openDownloadUrl();
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

void TrayIcon::uiThreadFunction()
{
    // Register window class
    const wchar_t *className = L"PresenceForPlexTray";

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
    if (!hIcon)
    {
        LOG_INFO("TrayIcon", "Failed to load icon by ID, trying by name");
        hIcon = LoadIconW(GetModuleHandle(NULL), L"IDI_APPICON");
    }

    // If still no icon, try the default system icon
    if (!hIcon)
    {
        DWORD error = GetLastError();
        LOG_ERROR_STREAM("TrayIcon", "Failed to load application icon, error code: " << error);
        // Try to load a default system icon instead
        hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(IDI_APPLICATION));
        LOG_INFO("TrayIcon", "Using default system icon instead");
    }
    else
    {
        LOG_INFO("TrayIcon", "Application icon loaded successfully");
    }

    wc.hIcon = hIcon;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(IDC_ARROW));

    if (!RegisterClassExW(&wc))
    {
        DWORD error = GetLastError();
        LOG_ERROR_STREAM("TrayIcon", "Failed to register window class, error code: " << error);
        return;
    }

    // Convert app name to wide string
    std::wstring wAppName;
    int length = MultiByteToWideChar(CP_UTF8, 0, m_appName.c_str(), -1, NULL, 0);
    if (length > 0)
    {
        wAppName.resize(length);
        MultiByteToWideChar(CP_UTF8, 0, m_appName.c_str(), -1, &wAppName[0], length);
    }
    else
    {
        wAppName = L"Presence For Plex";
    }

    // Create the hidden window
    m_hWnd = CreateWindowExW(
        0,
        className, wAppName.c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        10, 10, NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!m_hWnd)
    {
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
    if (!m_nid.hIcon)
    {
        LOG_INFO("TrayIcon", "Trying to load tray icon separately");
        m_nid.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APPICON));

        if (!m_nid.hIcon)
        {
            DWORD error = GetLastError();
            LOG_ERROR_STREAM("TrayIcon", "Failed to load tray icon, error code: " << error);
            // Try to load a default system icon instead
            m_nid.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(IDI_APPLICATION));
            LOG_INFO("TrayIcon", "Using default system icon for tray");
        }
        else
        {
            LOG_INFO("TrayIcon", "Tray icon loaded successfully");
        }
    }

    // Set initial tooltip
    wcscpy_s(m_nid.szTip, _countof(m_nid.szTip), L"Presence For Plex");

    LOG_INFO("TrayIcon", "Tray icon initialized, ready to be shown");

    // Message loop
    MSG msg;
    while (m_running && GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    LOG_INFO("TrayIcon", "UI thread exiting");
}

#endif