#ifdef _WIN32

#include "presence_for_plex/platform/windows/windows_ui_service.hpp"
#include "presence_for_plex/platform/windows/tray_icon_win.hpp"
#include "presence_for_plex/platform/windows/toast_notification_manager.hpp"
#include <objbase.h>

namespace presence_for_plex {
namespace platform {
namespace windows {

WindowsUiService::WindowsUiService()
    : m_component_name("WindowsUiService")
{
    PLEX_LOG_DEBUG(m_component_name, "WindowsUiService constructed");
}

WindowsUiService::~WindowsUiService() {
    PLEX_LOG_DEBUG(m_component_name, "WindowsUiService destructor called");
    shutdown();
}

std::expected<void, UiError> WindowsUiService::initialize() {
    if (m_initialized) {
        PLEX_LOG_DEBUG(m_component_name, "Already initialized, skipping");
        return {};
    }

    PLEX_LOG_INFO(m_component_name, "Initializing Windows UI service");

    // Initialize COM for Windows APIs
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::string msg = "Failed to initialize COM, error: 0x" + std::to_string(hr);
        PLEX_LOG_ERROR(m_component_name, msg);
        return std::unexpected(UiError::InitializationFailed);
    }

    m_initialized = true;
    PLEX_LOG_INFO(m_component_name, "Windows UI service initialized successfully");
    return {};
}

void WindowsUiService::shutdown() {
    if (!m_initialized) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Shutting down Windows UI service");

    // Clean up COM
    CoUninitialize();

    m_initialized = false;
    PLEX_LOG_INFO(m_component_name, "Windows UI service shut down");
}

bool WindowsUiService::is_initialized() const {
    return m_initialized;
}

std::unique_ptr<SystemTray> WindowsUiService::create_system_tray() {
    if (!m_initialized) {
        PLEX_LOG_ERROR(m_component_name, "UI service not initialized, cannot create system tray");
        return nullptr;
    }

    PLEX_LOG_DEBUG(m_component_name, "Creating Windows system tray");
    return std::make_unique<WindowsTrayIcon>();
}

std::unique_ptr<NotificationManager> WindowsUiService::create_notification_manager() {
    if (!m_initialized) {
        PLEX_LOG_ERROR(m_component_name, "UI service not initialized, cannot create notification manager");
        return nullptr;
    }

    PLEX_LOG_DEBUG(m_component_name, "Creating Windows toast notification manager");
    return std::make_unique<WindowsToastNotificationManager>();
}

std::unique_ptr<WindowManager> WindowsUiService::create_window_manager() {
    if (!m_initialized) {
        PLEX_LOG_ERROR(m_component_name, "UI service not initialized, cannot create window manager");
        return nullptr;
    }

    PLEX_LOG_WARNING(m_component_name, "Windows window manager not yet implemented");
    return nullptr; // TODO: Implement WindowsWindowManager
}

std::unique_ptr<DialogManager> WindowsUiService::create_dialog_manager() {
    if (!m_initialized) {
        PLEX_LOG_ERROR(m_component_name, "UI service not initialized, cannot create dialog manager");
        return nullptr;
    }

    PLEX_LOG_WARNING(m_component_name, "Windows dialog manager not yet implemented");
    return nullptr; // TODO: Implement WindowsDialogManager
}

bool WindowsUiService::supports_system_tray() const {
    return true;
}

bool WindowsUiService::supports_notifications() const {
    return true;
}

bool WindowsUiService::supports_window_management() const {
    return false; // TODO: Return true when WindowManager is implemented
}

bool WindowsUiService::supports_dialogs() const {
    return false; // TODO: Return true when DialogManager is implemented
}

void WindowsUiService::process_events() {
    if (!m_initialized) {
        return;
    }

    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void WindowsUiService::run_event_loop() {
    if (!m_initialized) {
        PLEX_LOG_ERROR(m_component_name, "UI service not initialized, cannot run event loop");
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Starting Windows event loop");

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    PLEX_LOG_INFO(m_component_name, "Windows event loop exited");
}

void WindowsUiService::quit_event_loop() {
    if (!m_initialized) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Requesting Windows event loop to quit");
    PostQuitMessage(0);
}

// WindowsUiServiceFactory implementation

std::unique_ptr<UiService> WindowsUiServiceFactory::create_service(PlatformType type) {
    if (type != PlatformType::Auto && type != PlatformType::Windows) {
        return nullptr;
    }

    return std::make_unique<WindowsUiService>();
}

} // namespace windows
} // namespace platform
} // namespace presence_for_plex

#endif // _WIN32