#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <iostream>

class TrayIcon {
public:
    TrayIcon();
private:
    NOTIFYICONDATA nid;
    HICON hIcon;
    HWND hwnd;
    std::string tooltip;
    void createTrayIcon();
    void updateTrayIcon();
    void showContextMenu();
    void onClick();
    void onExit();
}