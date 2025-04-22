#pragma once
#include "logger.h"
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <iostream>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <shellapi.h>
// Constants for Windows
#define ID_TRAY_APP_ICON 1000
#define ID_TRAY_EXIT 1001
#define ID_TRAY_RELOAD_CONFIG 1002
#define ID_TRAY_OPEN_CONFIG_LOCATION 1003
#define ID_TRAY_STATUS 1004
#define WM_TRAYICON (WM_USER + 1)

class TrayIcon
{
public:
    TrayIcon(const std::string &appName);
    ~TrayIcon();

    void show();
    void hide();
    void setExitCallback(std::function<void()> callback);
    void setReloadConfigCallback(std::function<void()> callback);
    void setOpenConfigLocationCallback(std::function<void()> callback);
    void setConnectionStatus(const std::string &status);

private:
    // Windows specific members
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void uiThreadFunction();
    void updateMenu();

    HWND m_hWnd;
    HMENU m_hMenu;
    NOTIFYICONDATAW m_nid;
    std::string m_appName;
    std::string m_connectionStatus;
    std::function<void()> m_exitCallback;
    std::function<void()> m_reloadConfigCallback;
    std::function<void()> m_openConfigLocationCallback;
    std::atomic<bool> m_running;
    std::thread m_uiThread;

    static TrayIcon *s_instance; // To access instance from static WndProc
};
#endif