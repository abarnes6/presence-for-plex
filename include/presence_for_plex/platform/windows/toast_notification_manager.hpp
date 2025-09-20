#pragma once

#ifdef _WIN32

#include "presence_for_plex/platform/ui_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace presence_for_plex {
namespace platform {
namespace windows {

class WindowsToastNotificationManager : public NotificationManager {
public:
    WindowsToastNotificationManager();
    ~WindowsToastNotificationManager() override;

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
    winrt::Windows::Data::Xml::Dom::XmlDocument create_toast_xml(
        const std::string& title,
        const std::string& message,
        NotificationType type) const;

    std::string generate_notification_id();
    void setup_notification_handlers(
        const winrt::Windows::UI::Notifications::ToastNotification& toast,
        const NotificationId& id);

    bool m_initialized{false};
    bool m_com_initialized{false};
    std::atomic<int> m_next_id{1};
    std::string m_component_name;
    std::string m_app_id;

    mutable std::mutex m_mutex;
    std::unordered_map<NotificationId, winrt::Windows::UI::Notifications::ToastNotification> m_active_notifications;

    NotificationCallback m_click_callback;
    NotificationCallback m_dismiss_callback;

    winrt::Windows::UI::Notifications::ToastNotifier m_notifier{nullptr};
};

} // namespace windows
} // namespace platform
} // namespace presence_for_plex

#endif // _WIN32