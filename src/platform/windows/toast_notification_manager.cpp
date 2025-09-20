#ifdef _WIN32

#include "presence_for_plex/platform/windows/toast_notification_manager.hpp"
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <format>
#include <sstream>
#include <ShlObj.h>
#include <propkey.h>
#include <propvarutil.h>
#include <filesystem>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::UI::Notifications;

namespace presence_for_plex {
namespace platform {
namespace windows {

// Helper function to ensure app is registered for toast notifications
static bool EnsureAppRegistration(const std::wstring& app_id, const std::wstring& display_name) {
    HRESULT hr = CoInitialize(nullptr);
    bool com_initialized = SUCCEEDED(hr);

    // Get the path to the current executable
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);

    // Get the Start Menu path
    wchar_t start_menu_path[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, start_menu_path))) {
        if (com_initialized) CoUninitialize();
        return false;
    }

    // Create shortcut path
    std::filesystem::path shortcut_path = std::filesystem::path(start_menu_path) / (display_name + L".lnk");

    // Create or update the shortcut
    IShellLinkW* shell_link = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&shell_link);
    if (FAILED(hr)) {
        if (com_initialized) CoUninitialize();
        return false;
    }

    // Set shortcut properties
    shell_link->SetPath(exe_path);
    shell_link->SetWorkingDirectory(std::filesystem::path(exe_path).parent_path().c_str());
    shell_link->SetDescription(display_name.c_str());

    // Set the AppUserModelID
    IPropertyStore* property_store = nullptr;
    hr = shell_link->QueryInterface(IID_IPropertyStore, (void**)&property_store);
    if (SUCCEEDED(hr)) {
        PROPVARIANT app_id_prop;
        hr = InitPropVariantFromString(app_id.c_str(), &app_id_prop);
        if (SUCCEEDED(hr)) {
            hr = property_store->SetValue(PKEY_AppUserModel_ID, app_id_prop);
            PropVariantClear(&app_id_prop);
            property_store->Commit();
        }
        property_store->Release();
    }

    // Save the shortcut
    IPersistFile* persist_file = nullptr;
    hr = shell_link->QueryInterface(IID_IPersistFile, (void**)&persist_file);
    if (SUCCEEDED(hr)) {
        hr = persist_file->Save(shortcut_path.c_str(), TRUE);
        persist_file->Release();
    }

    shell_link->Release();
    if (com_initialized) CoUninitialize();

    return SUCCEEDED(hr);
}

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

    // First, ensure the app is registered with Windows
    std::wstring wide_app_id(m_app_id.begin(), m_app_id.end());
    std::wstring display_name = L"Presence For Plex";

    if (EnsureAppRegistration(wide_app_id, display_name)) {
        PLEX_LOG_INFO(m_component_name, "App registration ensured");
    } else {
        PLEX_LOG_WARNING(m_component_name, "Failed to ensure app registration, notifications may not work");
    }

    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        m_com_initialized = true;

        // Try with app ID first since we registered it
        try {
            m_notifier = ToastNotificationManager::CreateToastNotifier(winrt::to_hstring(m_app_id));
            PLEX_LOG_INFO(m_component_name, "Created toast notifier with app ID");
        }
        catch (const winrt::hresult_error& e) {
            PLEX_LOG_WARNING(m_component_name,
                std::format("Failed to create notifier with app ID (0x{:08X}), trying without app ID",
                    static_cast<uint32_t>(e.code())));

            // Fall back to creating without app ID
            try {
                m_notifier = ToastNotificationManager::CreateToastNotifier();
                PLEX_LOG_INFO(m_component_name, "Created toast notifier without app ID");
            }
            catch (const winrt::hresult_error& e2) {
                PLEX_LOG_ERROR(m_component_name,
                    std::format("Failed to create notifier without app ID: 0x{:08X}",
                        static_cast<uint32_t>(e2.code())));
                return std::unexpected(UiError::InitializationFailed);
            }
        }

        m_initialized = true;
        PLEX_LOG_INFO(m_component_name, "Windows toast notification manager initialized successfully");
        return {};
    }
    catch (const winrt::hresult_error& e) {
        PLEX_LOG_ERROR(m_component_name,
            std::format("WinRT error during initialization: 0x{:08X}",
                static_cast<uint32_t>(e.code())));
        return std::unexpected(UiError::InitializationFailed);
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
        m_active_notifications.emplace(id, toast);

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