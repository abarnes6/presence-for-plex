#include "presence_for_plex/platform/qt/qt_notification_manager.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <QApplication>

namespace presence_for_plex::platform::qt {

QtNotificationManager::QtNotificationManager(QSystemTrayIcon* tray_icon)
    : m_tray_icon(tray_icon), m_owns_tray_icon(false) {

    if (!m_tray_icon) {
        m_tray_icon = new QSystemTrayIcon();
        m_owns_tray_icon = true;
    }

    LOG_DEBUG(m_component_name, "QtNotificationManager constructed");
}

QtNotificationManager::~QtNotificationManager() {
    LOG_DEBUG(m_component_name, "QtNotificationManager destructor called");
    shutdown();
}

std::expected<void, UiError> QtNotificationManager::initialize() {
    std::lock_guard lock(m_mutex);

    if (m_initialized) {
        LOG_DEBUG(m_component_name, "Already initialized");
        return {};
    }

    if (!QSystemTrayIcon::supportsMessages()) {
        LOG_WARNING(m_component_name, "System does not support tray notifications");
        return std::unexpected(UiError::NotSupported);
    }

    if (m_owns_tray_icon && m_tray_icon) {
        m_tray_icon->setIcon(QApplication::windowIcon());
        m_tray_icon->show();
    }

    QObject::connect(m_tray_icon, &QSystemTrayIcon::messageClicked,
                     [this]() {
                         std::string notification_id;
                         std::function<void()> on_click_callback;
                         std::function<void(const std::string&)> click_callback;
                         
                         // Copy necessary data while holding the mutex
                         {
                             std::lock_guard<std::mutex> guard(m_mutex);
                             if (!m_active_notifications.empty()) {
                                 notification_id = m_active_notifications.begin()->first;
                                 auto it = m_active_notifications.find(notification_id);
                                 if (it != m_active_notifications.end()) {
                                     on_click_callback = it->second.on_click;
                                     click_callback = m_click_callback;
                                 }
                             }
                         }
                         
                         // Invoke callbacks outside the mutex scope
                         if (on_click_callback) {
                             on_click_callback();
                         }
                         if (click_callback) {
                             click_callback(notification_id);
                         }
                     });

    m_initialized = true;
    LOG_INFO(m_component_name, "Qt notification manager initialized");
    return {};
}

void QtNotificationManager::shutdown() {
    std::lock_guard lock(m_mutex);

    if (!m_initialized) {
        return;
    }

    LOG_INFO(m_component_name, "Shutting down Qt notification manager");

    if (m_owns_tray_icon && m_tray_icon) {
        m_tray_icon->hide();
        delete m_tray_icon;
        m_tray_icon = nullptr;
    }

    m_active_notifications.clear();
    m_initialized = false;
    LOG_INFO(m_component_name, "Qt notification manager shut down");
}

bool QtNotificationManager::is_supported() const {
    return QSystemTrayIcon::supportsMessages();
}

std::expected<QtNotificationManager::NotificationId, UiError>
QtNotificationManager::show_notification(const Notification& notification) {
    std::lock_guard lock(m_mutex);

    if (!m_initialized || !m_tray_icon) {
        return std::unexpected(UiError::NotSupported);
    }

    NotificationId id = generate_id();
    m_active_notifications[id] = notification;

    QSystemTrayIcon::MessageIcon icon = convert_notification_type(notification.type);

    m_tray_icon->showMessage(
        QString::fromStdString(notification.title),
        QString::fromStdString(notification.message),
        icon,
        static_cast<int>(notification.duration.count() * 1000)
    );

    LOG_DEBUG(m_component_name, "Notification shown: " + notification.title);
    return id;
}

std::expected<void, UiError>
QtNotificationManager::update_notification(const NotificationId& id, const Notification& notification) {
    std::lock_guard lock(m_mutex);

    auto it = m_active_notifications.find(id);
    if (it == m_active_notifications.end()) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    it->second = notification;

    QSystemTrayIcon::MessageIcon icon = convert_notification_type(notification.type);

    m_tray_icon->showMessage(
        QString::fromStdString(notification.title),
        QString::fromStdString(notification.message),
        icon,
        static_cast<int>(notification.duration.count() * 1000)
    );

    LOG_DEBUG(m_component_name, "Notification updated: " + id);
    return {};
}

std::expected<void, UiError>
QtNotificationManager::hide_notification(const NotificationId& id) {
    std::lock_guard lock(m_mutex);

    auto it = m_active_notifications.find(id);
    if (it == m_active_notifications.end()) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    m_active_notifications.erase(it);
    LOG_DEBUG(m_component_name, "Notification hidden: " + id);
    return {};
}

void QtNotificationManager::clear_all_notifications() {
    std::lock_guard lock(m_mutex);
    m_active_notifications.clear();
    LOG_DEBUG(m_component_name, "All notifications cleared");
}

void QtNotificationManager::set_click_callback(NotificationCallback callback) {
    std::lock_guard lock(m_mutex);
    m_click_callback = callback;
}

void QtNotificationManager::set_dismiss_callback(NotificationCallback callback) {
    std::lock_guard lock(m_mutex);
    m_dismiss_callback = callback;
}

void QtNotificationManager::on_notification_clicked(const NotificationId& id) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto it = m_active_notifications.find(id);
    if (it != m_active_notifications.end()) {
        if (it->second.on_click) {
            it->second.on_click();
        }
        if (m_click_callback) {
            m_click_callback(id);
        }
    }
}

void QtNotificationManager::on_notification_dismissed(const NotificationId& id) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto it = m_active_notifications.find(id);
    if (it != m_active_notifications.end()) {
        if (it->second.on_dismiss) {
            it->second.on_dismiss();
        }
        if (m_dismiss_callback) {
            m_dismiss_callback(id);
        }
    }
    m_active_notifications.erase(id);
}

QtNotificationManager::NotificationId QtNotificationManager::generate_id() {
    return "notification_" + std::to_string(++m_id_counter);
}

QSystemTrayIcon::MessageIcon QtNotificationManager::convert_notification_type(NotificationType type) {
    switch (type) {
        case NotificationType::Info:
            return QSystemTrayIcon::Information;
        case NotificationType::Warning:
            return QSystemTrayIcon::Warning;
        case NotificationType::Error:
            return QSystemTrayIcon::Critical;
        case NotificationType::Success:
            return QSystemTrayIcon::Information;
        default:
            return QSystemTrayIcon::NoIcon;
    }
}

} // namespace presence_for_plex::platform::qt