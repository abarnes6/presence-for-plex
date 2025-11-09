#include "presence_for_plex/platform/qt/qt_ui_service.hpp"
#include "presence_for_plex/platform/qt/qt_system_tray.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <QCoreApplication>
#include <QIcon>

namespace presence_for_plex::platform::qt {

QtUiService::QtUiService() {
    LOG_DEBUG(m_component_name, "QtUiService constructed");
}

QtUiService::~QtUiService() {
    LOG_DEBUG(m_component_name, "QtUiService destructor called");
    shutdown();
}

std::expected<void, UiError> QtUiService::initialize() {
    if (m_initialized) {
        LOG_DEBUG(m_component_name, "Already initialized, skipping");
        return {};
    }

    LOG_DEBUG(m_component_name, "Initializing Qt UI service");

    // Use the existing QApplication instance created in main
    m_app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!m_app) {
        LOG_ERROR(m_component_name, "QApplication not available - ensure Qt is initialized in main");
        return std::unexpected(UiError::InitializationFailed);
    }

    m_initialized = true;
    LOG_DEBUG(m_component_name, "Qt UI service initialized successfully");
    return {};
}

void QtUiService::shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO(m_component_name, "Shutting down Qt UI service");

    // Don't delete QApplication - it's managed by main
    m_app = nullptr;

    m_initialized = false;
    LOG_INFO(m_component_name, "Qt UI service shut down");
}

bool QtUiService::is_initialized() const {
    return m_initialized;
}

std::unique_ptr<SystemTray> QtUiService::create_system_tray() {
    if (!m_initialized) {
        LOG_ERROR(m_component_name, "UI service not initialized, cannot create system tray");
        return nullptr;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        LOG_WARNING(m_component_name, "System tray not available on this platform");
        return nullptr;
    }

    LOG_DEBUG(m_component_name, "Creating Qt system tray");
    return std::make_unique<QtSystemTray>();
}

bool QtUiService::supports_system_tray() const {
    return QSystemTrayIcon::isSystemTrayAvailable();
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

    LOG_INFO(m_component_name, "Requesting Qt event loop to quit");
    m_app->quit();
}

} // namespace presence_for_plex::platform::qt