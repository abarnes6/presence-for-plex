#ifdef _WIN32

#include "presence_for_plex/platform/windows/toast_notification_manager.hpp"
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <format>
#include <sstream>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::UI::Notifications;

namespace presence_for_plex {
namespace platform {
namespace windows {

WindowsToastNotificationManager::WindowsToastNotificationManager()
    : m_component_name("WindowsToastNotificationManager")
    , m_app_id("PresenceForPlex") {
    PLEX_LOG_DEBUG(m_component_name, "WindowsToastNotificationManager constructed");
}

WindowsToastNotificationManager::~WindowsToastNotificationManager() {
    PLEX_LOG_DEBUG(m_component_name, "WindowsToastNotificationManager destructor called");
    shutdown();
}

std::expected<void, UiError> WindowsToastNotificationManager::initialize() {
    std::lock_guard lock(m_mutex);

    if (m_initialized) {
        PLEX_LOG_DEBUG(m_component_name, "Already initialized, skipping");
        return {};
    }

    PLEX_LOG_INFO(m_component_name, "Initializing Windows toast notification manager");

    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        m_com_initialized = true;

        m_notifier = ToastNotificationManager::CreateToastNotifier(winrt::to_hstring(m_app_id));

        m_initialized = true;
        PLEX_LOG_INFO(m_component_name, "Windows toast notification manager initialized successfully");
        return {};
    }
    catch (const std::exception& e) {
        PLEX_LOG_ERROR(m_component_name, std::format("Failed to initialize: {}", e.what()));
        return std::unexpected(UiError::InitializationFailed);
    }
    catch (...) {
        PLEX_LOG_ERROR(m_component_name, "Failed to initialize: unknown error");
        return std::unexpected(UiError::InitializationFailed);
    }
}

void WindowsToastNotificationManager::shutdown() {
    std::lock_guard lock(m_mutex);

    if (!m_initialized) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Shutting down Windows toast notification manager");

    clear_all_notifications();
    m_notifier = nullptr;

    if (m_com_initialized) {
        winrt::uninit_apartment();
        m_com_initialized = false;
    }

    m_initialized = false;
    PLEX_LOG_INFO(m_component_name, "Windows toast notification manager shut down");
}

bool WindowsToastNotificationManager::is_supported() const {
    return true;
}

XmlDocument WindowsToastNotificationManager::create_toast_xml(
    const std::string& title,
    const std::string& message,
    NotificationType type) const {

    std::wstring wtitle(title.begin(), title.end());
    std::wstring wmessage(message.begin(), message.end());

    std::wstring icon;
    switch (type) {
        case NotificationType::Error:
            icon = L"❌";
            break;
        case NotificationType::Warning:
            icon = L"⚠️";
            break;
        case NotificationType::Success:
            icon = L"✅";
            break;
        case NotificationType::Info:
        default:
            icon = L"ℹ️";
            break;
    }

    std::wostringstream xml;
    xml << L"<toast duration=\"long\">"
        << L"  <visual>"
        << L"    <binding template=\"ToastGeneric\">"
        << L"      <text>" << icon << L" " << wtitle << L"</text>"
        << L"      <text>" << wmessage << L"</text>"
        << L"      <text placement=\"attribution\">PresenceForPlex</text>"
        << L"    </binding>"
        << L"  </visual>"
        << L"  <audio src=\"ms-winsoundevent:Notification.Default\"/>"
        << L"</toast>";

    XmlDocument doc;
    doc.LoadXml(xml.str());
    return doc;
}

std::string WindowsToastNotificationManager::generate_notification_id() {
    return std::format("notification_{}", m_next_id.fetch_add(1));
}

void WindowsToastNotificationManager::setup_notification_handlers(
    const ToastNotification& toast,
    const NotificationId& id) {

    toast.Activated([this, id](const auto&, const auto&) {
        PLEX_LOG_DEBUG(m_component_name, std::format("Toast {} activated", id));
        on_notification_clicked(id);
    });

    toast.Dismissed([this, id](const auto&, const ToastDismissedEventArgs& args) {
        auto reason = args.Reason();
        PLEX_LOG_DEBUG(m_component_name,
            std::format("Toast {} dismissed with reason: {}", id, static_cast<int>(reason)));

        if (reason == ToastDismissalReason::UserCanceled) {
            on_notification_dismissed(id);
        }
    });

    toast.Failed([this, id](const auto&, const ToastFailedEventArgs& args) {
        auto error_code = args.ErrorCode();
        PLEX_LOG_ERROR(m_component_name,
            std::format("Toast {} failed with error: 0x{:08X}", id, static_cast<uint32_t>(error_code)));
    });
}

std::expected<WindowsToastNotificationManager::NotificationId, UiError>
WindowsToastNotificationManager::show_notification(const Notification& notification) {
    std::lock_guard lock(m_mutex);

    if (!m_initialized) {
        PLEX_LOG_ERROR(m_component_name, "Not initialized");
        return std::unexpected(UiError::InitializationFailed);
    }

    try {
        auto xml = create_toast_xml(notification.title, notification.message, notification.type);
        auto toast = ToastNotification(xml);

        if (notification.duration.count() > 0) {
            toast.ExpirationTime(winrt::clock::now() +
                std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(notification.duration));
        }

        auto id = generate_notification_id();
        setup_notification_handlers(toast, id);

        m_notifier.Show(toast);
        m_active_notifications[id] = toast;

        PLEX_LOG_INFO(m_component_name,
            std::format("Showed toast notification {}: {}", id, notification.title));

        return id;
    }
    catch (const std::exception& e) {
        PLEX_LOG_ERROR(m_component_name,
            std::format("Failed to show notification: {}", e.what()));
        return std::unexpected(UiError::OperationFailed);
    }
    catch (...) {
        PLEX_LOG_ERROR(m_component_name, "Failed to show notification: unknown error");
        return std::unexpected(UiError::OperationFailed);
    }
}

std::expected<void, UiError> WindowsToastNotificationManager::update_notification(
    const NotificationId& id,
    const Notification& notification) {
    std::lock_guard lock(m_mutex);

    if (!m_initialized) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto it = m_active_notifications.find(id);
    if (it == m_active_notifications.end()) {
        PLEX_LOG_WARNING(m_component_name, std::format("Notification {} not found", id));
        return std::unexpected(UiError::ResourceNotFound);
    }

    try {
        m_notifier.Hide(it->second);
        m_active_notifications.erase(it);

        auto result = show_notification(notification);
        if (!result) {
            return std::unexpected(result.error());
        }

        PLEX_LOG_DEBUG(m_component_name, std::format("Updated notification {}", id));
        return {};
    }
    catch (const std::exception& e) {
        PLEX_LOG_ERROR(m_component_name,
            std::format("Failed to update notification {}: {}", id, e.what()));
        return std::unexpected(UiError::OperationFailed);
    }
}

std::expected<void, UiError> WindowsToastNotificationManager::hide_notification(
    const NotificationId& id) {
    std::lock_guard lock(m_mutex);

    if (!m_initialized) {
        return std::unexpected(UiError::InitializationFailed);
    }

    auto it = m_active_notifications.find(id);
    if (it == m_active_notifications.end()) {
        PLEX_LOG_WARNING(m_component_name, std::format("Notification {} not found", id));
        return std::unexpected(UiError::ResourceNotFound);
    }

    try {
        m_notifier.Hide(it->second);
        m_active_notifications.erase(it);

        PLEX_LOG_DEBUG(m_component_name, std::format("Hid notification {}", id));
        return {};
    }
    catch (const std::exception& e) {
        PLEX_LOG_ERROR(m_component_name,
            std::format("Failed to hide notification {}: {}", id, e.what()));
        return std::unexpected(UiError::OperationFailed);
    }
}

void WindowsToastNotificationManager::clear_all_notifications() {
    std::lock_guard lock(m_mutex);

    if (!m_initialized || !m_notifier) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Clearing all notifications");

    for (const auto& [id, toast] : m_active_notifications) {
        try {
            m_notifier.Hide(toast);
        }
        catch (const std::exception& e) {
            PLEX_LOG_WARNING(m_component_name,
                std::format("Failed to hide notification {}: {}", id, e.what()));
        }
    }

    m_active_notifications.clear();
}

void WindowsToastNotificationManager::set_click_callback(NotificationCallback callback) {
    std::lock_guard lock(m_mutex);
    m_click_callback = callback;
}

void WindowsToastNotificationManager::set_dismiss_callback(NotificationCallback callback) {
    std::lock_guard lock(m_mutex);
    m_dismiss_callback = callback;
}

void WindowsToastNotificationManager::on_notification_clicked(const NotificationId& id) {
    NotificationCallback callback;
    {
        std::lock_guard lock(m_mutex);
        callback = m_click_callback;
    }

    if (callback) {
        callback(id);
    }
}

void WindowsToastNotificationManager::on_notification_dismissed(const NotificationId& id) {
    NotificationCallback callback;
    {
        std::lock_guard lock(m_mutex);
        callback = m_dismiss_callback;
    }

    if (callback) {
        callback(id);
    }
}

} // namespace windows
} // namespace platform
} // namespace presence_for_plex

#endif // _WIN32