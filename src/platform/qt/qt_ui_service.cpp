#include "presence_for_plex/platform/qt/qt_ui_service.hpp"
#include "presence_for_plex/platform/qt/qt_system_tray.hpp"
#include "presence_for_plex/platform/qt/qt_notification_manager.hpp"
#include "presence_for_plex/platform/qt/qt_dialog_manager.hpp"
#include "presence_for_plex/platform/qt/qt_window_manager.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <QCoreApplication>
#include <QIcon>

namespace presence_for_plex::platform::qt {

QtUiService::QtUiService() {
    PLEX_LOG_DEBUG(m_component_name, "QtUiService constructed");
}

QtUiService::~QtUiService() {
    PLEX_LOG_DEBUG(m_component_name, "QtUiService destructor called");
    shutdown();
}

std::expected<void, UiError> QtUiService::initialize() {
    if (m_initialized) {
        PLEX_LOG_DEBUG(m_component_name, "Already initialized, skipping");
        return {};
    }

    PLEX_LOG_INFO(m_component_name, "Initializing Qt UI service");

    // Use the existing QApplication instance created in main
    m_app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!m_app) {
        PLEX_LOG_ERROR(m_component_name, "QApplication not available - ensure Qt is initialized in main");
        return std::unexpected(UiError::InitializationFailed);
    }

    m_initialized = true;
    PLEX_LOG_INFO(m_component_name, "Qt UI service initialized successfully");
    return {};
}

void QtUiService::shutdown() {
    if (!m_initialized) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Shutting down Qt UI service");

    // Don't delete QApplication - it's managed by main
    m_app = nullptr;

    m_initialized = false;
    PLEX_LOG_INFO(m_component_name, "Qt UI service shut down");
}

bool QtUiService::is_initialized() const {
    return m_initialized;
}

std::unique_ptr<SystemTray> QtUiService::create_system_tray() {
    if (!m_initialized) {
        PLEX_LOG_ERROR(m_component_name, "UI service not initialized, cannot create system tray");
        return nullptr;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        PLEX_LOG_WARNING(m_component_name, "System tray not available on this platform");
        return nullptr;
    }

    PLEX_LOG_DEBUG(m_component_name, "Creating Qt system tray");
    return std::make_unique<QtSystemTray>();
}

std::unique_ptr<NotificationManager> QtUiService::create_notification_manager() {
    if (!m_initialized) {
        PLEX_LOG_ERROR(m_component_name, "UI service not initialized, cannot create notification manager");
        return nullptr;
    }

    PLEX_LOG_DEBUG(m_component_name, "Creating Qt notification manager");
    return std::make_unique<QtNotificationManager>();
}

std::unique_ptr<WindowManager> QtUiService::create_window_manager() {
    if (!m_initialized) {
        PLEX_LOG_ERROR(m_component_name, "UI service not initialized, cannot create window manager");
        return nullptr;
    }

    PLEX_LOG_DEBUG(m_component_name, "Creating Qt window manager");
    return std::make_unique<QtWindowManager>();
}

std::unique_ptr<DialogManager> QtUiService::create_dialog_manager() {
    if (!m_initialized) {
        PLEX_LOG_ERROR(m_component_name, "UI service not initialized, cannot create dialog manager");
        return nullptr;
    }

    PLEX_LOG_DEBUG(m_component_name, "Creating Qt dialog manager");
    return std::make_unique<QtDialogManager>();
}

bool QtUiService::supports_system_tray() const {
    return QSystemTrayIcon::isSystemTrayAvailable();
}

bool QtUiService::supports_notifications() const {
    return QSystemTrayIcon::supportsMessages();
}

bool QtUiService::supports_window_management() const {
    return true;
}

bool QtUiService::supports_dialogs() const {
    return true;
}

void QtUiService::process_events() {
    if (!m_initialized || !m_app) {
        return;
    }

    m_app->processEvents();
}

void QtUiService::quit_event_loop() {
    if (!m_initialized || !m_app) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Requesting Qt event loop to quit");
    m_app->quit();
}

} // namespace presence_for_plex::platform::qt