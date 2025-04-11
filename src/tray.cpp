//#include "tray.h"
//
//
//TrayIcon::TrayIcon() {
//    // Initialize the NOTIFYICONDATA structure
//    ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
//    nid.cbSize = sizeof(NOTIFYICONDATA);
//    nid.hWnd = hwnd;
//    nid.uID = 1; // Unique ID for the tray icon
//    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
//    nid.uCallbackMessage = WM_USER + 1; // Custom message for tray icon events
//    hIcon = LoadIcon(NULL, IDI_APPLICATION); // Load a default icon
//    nid.hIcon = hIcon;
//    tooltip = "Plex Rich Presence"; // Tooltip text
//
//    createTrayIcon();
//}
//
//void TrayIcon::createTrayIcon() {
//    // Add the icon to the system tray
//    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
//        std::cerr << "Failed to add tray icon." << std::endl;
//    }
//    updateTrayIcon();
//}
//
//void TrayIcon::updateTrayIcon() {
//    // Update the tray icon tooltip
//    strncpy_s(nid.szTip, tooltip.c_str(), sizeof(nid.szTip) - 1);
//    Shell_NotifyIcon(NIM_MODIFY, &nid);
//}
//
//void TrayIcon::showContextMenu() {
//    // Create a context menu
//    HMENU hMenu = CreatePopupMenu();
//    AppendMenu(hMenu, MF_STRING, 1, "Exit");
//    
//    // Get the cursor position
//    POINT pt;
//    GetCursorPos(&pt);
//    
//    // Display the context menu
//    SetForegroundWindow(hwnd);
//    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
//    
//    DestroyMenu(hMenu);
//}
//
//void TrayIcon::onClick() {
//    // Handle tray icon click event
//    std::cout << "Tray icon clicked!" << std::endl;
//}
//
//void TrayIcon::onExit() {
//    // Handle exit event
//    std::cout << "Exiting application..." << std::endl;
//    Shell_NotifyIcon(NIM_DELETE, &nid); // Remove the tray icon
//    PostQuitMessage(0); // Post a quit message to the message loop
//}