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
#define ID_TRAY_EXIT     1001
#define WM_TRAYICON      (WM_USER + 1)
#elif defined(__linux__)
#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>
#endif

class TrayIcon {
public:
    TrayIcon(const std::string& appName);
    ~TrayIcon();

    void show();
    void hide();
    void setTooltip(const std::string& tooltip);
    void setExitCallback(std::function<void()> callback);
    
    // Add a method to customize the tray icon
    bool setIcon(HICON hIcon);

private:
#ifdef _WIN32
    // Windows specific members
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void uiThreadFunction();

    HWND m_hWnd;
    HMENU m_hMenu;
    NOTIFYICONDATAW m_nid;
    std::string m_appName;
    std::function<void()> m_exitCallback;
    std::atomic<bool> m_running;
    std::thread m_uiThread;
    
    static TrayIcon* s_instance; // To access instance from static WndProc
#elif defined(__linux__)
    // Linux specific members
    AppIndicator* indicator;
    GtkWidget* menu;
    GtkWidget* quit_item;
    static void quit_activate(GtkMenuItem *item, gpointer user_data);
    std::function<void()> exitCallback;
    bool initialized;
#endif
    // Common members
    std::string tooltip;
};