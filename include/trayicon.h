#pragma once

// Standard library headers
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

// Platform-specific headers
#ifdef _WIN32
#include <winsock2.h>
#include <shellapi.h>
#endif

// Project headers
#include "logger.h"
#include "resources.h"
#include "thread_utils.h"

// Constants for Windows
#ifdef _WIN32
#define ID_TRAY_APP_ICON 1000
#define ID_TRAY_EXIT 1001
#define ID_TRAY_RELOAD_CONFIG 1002
#define ID_TRAY_OPEN_CONFIG_LOCATION 1003
#define ID_TRAY_STATUS 1004
#define ID_TRAY_CHECK_UPDATES 1005
#define WM_TRAYICON (WM_USER + 1)
#endif

#ifdef _WIN32
/**
 * @brief Manages the system tray icon and menu for Windows.
 */
class TrayIcon
{
public:
    /**
     * @brief Creates a tray icon with the specified application name
     * @param appName The name of the application to display
     */
    TrayIcon(const std::string &appName);

    /**
     * @brief Cleans up resources and removes the tray icon
     */
    ~TrayIcon();

    // Public interface
    void show();
    void hide();
    void setExitCallback(std::function<void()> callback);
    void setConnectionStatus(const std::string &status);
    void setUpdateCheckCallback(std::function<void()> callback);
    void showNotification(const std::string &title, const std::string &message, bool isError = false);
    void showUpdateNotification(const std::string &title, const std::string &message, const std::string &downloadUrl);

private:
    // Windows window procedure
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    // UI thread and menu management
    void uiThreadFunction();
    void updateMenu();
    void executeExitCallback();
    void executeUpdateCheckCallback();
    void openDownloadUrl();

    // Window and menu handles
    HWND m_hWnd;
    HMENU m_hMenu;
    NOTIFYICONDATAW m_nid;

    // Application data
    std::string m_appName;
    std::string m_connectionStatus;
    std::string m_downloadUrl;
    std::function<void()> m_exitCallback;
    std::function<void()> m_updateCheckCallback;

    // Thread management
    std::atomic<bool> m_running;
    std::thread m_uiThread;
    std::atomic<bool> m_iconShown; // Track if the icon is currently shown

    // Static instance for Windows callback
    static TrayIcon *s_instance;
};
#endif