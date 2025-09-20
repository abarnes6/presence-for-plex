#pragma once

#ifdef _WIN32

#include "presence_for_plex/platform/ui_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <memory>
#include <functional>

namespace presence_for_plex {
namespace platform {
namespace windows {

class TrayIntegration {
public:
    using ExitCallback = std::function<void()>;
    using ShowConfigCallback = std::function<void()>;

    TrayIntegration(UiService& ui_service);
    ~TrayIntegration();

    std::expected<void, UiError> initialize();
    void shutdown();

    void set_status(const std::string& status);
    void show_notification(const std::string& title, const std::string& message, bool is_error = false);
    void show_update_notification(const std::string& title, const std::string& message, const std::string& download_url);

    void set_exit_callback(ExitCallback callback);
    void set_show_config_callback(ShowConfigCallback callback);
    void set_update_check_callback(std::function<void()> callback);

private:
    void setup_tray_menu();
    void on_exit_clicked();
    void on_show_config_clicked();
    void on_update_check_clicked();

    UiService& m_ui_service;
    std::unique_ptr<SystemTray> m_tray_icon;
    std::string m_component_name;
    std::string m_current_status;

    ExitCallback m_exit_callback;
    ShowConfigCallback m_show_config_callback;
    std::function<void()> m_update_check_callback;
};

} // namespace windows
} // namespace platform
} // namespace presence_for_plex

#endif // _WIN32