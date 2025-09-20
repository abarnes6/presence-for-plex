#include "presence_for_plex/platform/qt/qt_ui_service.hpp"
#include "presence_for_plex/platform/qt/qt_system_tray.hpp"
#include "presence_for_plex/platform/qt/qt_notification_manager.hpp"
#include "presence_for_plex/platform/qt/qt_dialog_manager.hpp"
#include "presence_for_plex/platform/qt/qt_window_manager.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <QCoreApplication>

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

    if (!QCoreApplication::instance()) {
        PLEX_LOG_INFO(m_component_name, "Creating QApplication instance");
        static int argc = 1;
        static char app_name[] = "PresenceForPlex";
        static char* argv[] = { app_name, nullptr };

        m_app = new QApplication(argc, argv);
        m_app->setApplicationName("Presence for Plex");
        m_app->setApplicationDisplayName("Presence for Plex");
        m_app->setOrganizationName("Andrew Barnes");
        m_app->setOrganizationDomain("presence-for-plex.github.io");

        #ifdef Q_OS_LINUX
        m_app->setDesktopFileName("presence-for-plex.desktop");
        #endif

        m_owns_app = true;
    } else {
        m_app = qobject_cast<QApplication*>(QCoreApplication::instance());
        if (!m_app) {
            PLEX_LOG_ERROR(m_component_name, "QCoreApplication exists but is not a QApplication");
            return std::unexpected(UiError::InitializationFailed);
        }
        m_owns_app = false;
    }

    m_app->setQuitOnLastWindowClosed(false);

    m_initialized = true;
    PLEX_LOG_INFO(m_component_name, "Qt UI service initialized successfully");
    return {};
}

void QtUiService::shutdown() {
    if (!m_initialized) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Shutting down Qt UI service");

    if (m_owns_app && m_app) {
        delete m_app;
        m_app = nullptr;
    }

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

void QtUiService::run_event_loop() {
    if (!m_initialized || !m_app) {
        PLEX_LOG_ERROR(m_component_name, "UI service not initialized, cannot run event loop");
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Starting Qt event loop");
    m_app->exec();
    PLEX_LOG_INFO(m_component_name, "Qt event loop exited");
}

void QtUiService::quit_event_loop() {
    if (!m_initialized || !m_app) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Requesting Qt event loop to quit");
    m_app->quit();
}

std::unique_ptr<UiService> QtUiServiceFactory::create_service(PlatformType type) {
    if (type != PlatformType::Auto &&
        type != PlatformType::Windows &&
        type != PlatformType::macOS &&
        type != PlatformType::Linux) {
        return nullptr;
    }

    return std::make_unique<QtUiService>();
}

} // namespace presence_for_plex::platform::qt