#pragma once

#ifdef _WIN32

#include "presence_for_plex/platform/ui_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <memory>

namespace presence_for_plex {
namespace platform {
namespace windows {

class WindowsUiService : public UiService {
public:
    WindowsUiService();
    ~WindowsUiService() override;

    // UiService interface implementation
    std::expected<void, UiError> initialize() override;
    void shutdown() override;
    bool is_initialized() const override;

    std::unique_ptr<SystemTray> create_system_tray() override;
    std::unique_ptr<NotificationManager> create_notification_manager() override;
    std::unique_ptr<WindowManager> create_window_manager() override;
    std::unique_ptr<DialogManager> create_dialog_manager() override;

    bool supports_system_tray() const override;
    bool supports_notifications() const override;
    bool supports_window_management() const override;
    bool supports_dialogs() const override;

    void process_events() override;
    void run_event_loop() override;
    void quit_event_loop() override;

private:
    bool m_initialized{false};
    std::string m_component_name;
};

class WindowsUiServiceFactory : public UiServiceFactory {
public:
    WindowsUiServiceFactory() = default;
    ~WindowsUiServiceFactory() override = default;

    std::unique_ptr<UiService> create_service(PlatformType type = PlatformType::Auto) override;
};

} // namespace windows
} // namespace platform
} // namespace presence_for_plex

#endif // _WIN32