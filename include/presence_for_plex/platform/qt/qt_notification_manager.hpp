#pragma once

#include "presence_for_plex/platform/ui_service.hpp"
#include <QSystemTrayIcon>
#include <map>
#include <mutex>
#include <atomic>

namespace presence_for_plex::platform::qt {

class QtNotificationManager : public NotificationManager {
public:
    QtNotificationManager(QSystemTrayIcon* tray_icon = nullptr);
    ~QtNotificationManager() override;

    std::expected<void, UiError> initialize() override;
    void shutdown() override;
    bool is_supported() const override;

    std::expected<NotificationId, UiError> show_notification(const Notification& notification) override;
    std::expected<void, UiError> update_notification(const NotificationId& id, const Notification& notification) override;
    std::expected<void, UiError> hide_notification(const NotificationId& id) override;
    void clear_all_notifications() override;

    void set_click_callback(NotificationCallback callback) override;
    void set_dismiss_callback(NotificationCallback callback) override;

protected:
    void on_notification_clicked(const NotificationId& id) override;
    void on_notification_dismissed(const NotificationId& id) override;

private:
    NotificationId generate_id();
    QSystemTrayIcon::MessageIcon convert_notification_type(NotificationType type);

    QSystemTrayIcon* m_tray_icon;
    bool m_owns_tray_icon;
    std::map<NotificationId, Notification> m_active_notifications;
    NotificationCallback m_click_callback;
    NotificationCallback m_dismiss_callback;
    std::atomic<int> m_id_counter{0};
    mutable std::mutex m_mutex;
    bool m_initialized = false;
    std::string m_component_name = "QtNotificationManager";
};

} // namespace presence_for_plex::platform::qt