#ifdef _WIN32

#include "presence_for_plex/platform/windows/tray_integration.hpp"
#include "presence_for_plex/platform/windows/tray_icon_win.hpp"

namespace presence_for_plex {
namespace platform {
namespace windows {

TrayIntegration::TrayIntegration(UiService& ui_service)
    : m_ui_service(ui_service),
      m_component_name("TrayIntegration"),
      m_current_status("Status: Initializing...")
{
    PLEX_LOG_DEBUG(m_component_name, "TrayIntegration constructed");
}

TrayIntegration::~TrayIntegration() {
    shutdown();
    PLEX_LOG_DEBUG(m_component_name, "TrayIntegration destroyed");
}

std::expected<void, UiError> TrayIntegration::initialize() {
    PLEX_LOG_INFO(m_component_name, "Initializing tray integration");

    if (!m_ui_service.supports_system_tray()) {
        PLEX_LOG_WARNING(m_component_name, "System tray not supported");
        return std::unexpected(UiError::NotSupported);
    }

    m_tray_icon = m_ui_service.create_system_tray();
    if (!m_tray_icon) {
        PLEX_LOG_ERROR(m_component_name, "Failed to create system tray");
        return std::unexpected(UiError::InitializationFailed);
    }

    auto init_result = m_tray_icon->initialize();
    if (!init_result) {
        PLEX_LOG_ERROR(m_component_name, "Failed to initialize system tray");
        return init_result;
    }

    // Set the application icon
    auto icon_result = m_tray_icon->set_icon_from_resource(101); // IDI_APPICON
    if (!icon_result) {
        PLEX_LOG_WARNING(m_component_name, "Failed to set tray icon, using default");
    }

    // Set initial tooltip
    auto tooltip_result = m_tray_icon->set_tooltip("Presence For Plex");
    if (!tooltip_result) {
        PLEX_LOG_WARNING(m_component_name, "Failed to set tooltip");
    }

    // Setup the context menu
    setup_tray_menu();

    // Set click callbacks
    m_tray_icon->set_click_callback([this]() {
        PLEX_LOG_DEBUG(m_component_name, "Tray icon clicked");
    });

    m_tray_icon->set_double_click_callback([this]() {
        PLEX_LOG_DEBUG(m_component_name, "Tray icon double clicked");
        if (m_show_config_callback) {
            m_show_config_callback();
        }
    });

    m_tray_icon->set_menu_callback([this](const std::string& item_id) {
        PLEX_LOG_DEBUG(m_component_name, "Menu item selected: " + item_id);
        if (item_id == "exit") {
            on_exit_clicked();
        } else if (item_id == "show_config") {
            on_show_config_clicked();
        } else if (item_id == "check_updates") {
            on_update_check_clicked();
        }
    });

    // Show the tray icon
    m_tray_icon->show();

    PLEX_LOG_INFO(m_component_name, "Tray integration initialized successfully");
    return {};
}

void TrayIntegration::shutdown() {
    if (m_tray_icon) {
        PLEX_LOG_INFO(m_component_name, "Shutting down tray integration");
        m_tray_icon->hide();
        m_tray_icon->shutdown();
        m_tray_icon.reset();
    }
}

void TrayIntegration::set_status(const std::string& status) {
    if (status == m_current_status) {
        return;
    }

    m_current_status = status;
    PLEX_LOG_DEBUG(m_component_name, "Setting status: " + status);

    // Update the menu with the new status
    setup_tray_menu();
}

void TrayIntegration::show_notification(const std::string& title, const std::string& message, bool is_error) {
    if (!m_tray_icon) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Showing notification: " + title + " - " + message);

    // Try to show as balloon notification first
    auto* windows_tray = dynamic_cast<windows::WindowsTrayIcon*>(m_tray_icon.get());
    if (windows_tray) {
        auto result = windows_tray->show_balloon_notification(title, message, is_error);
        if (result) {
            return; // Success
        }
        PLEX_LOG_WARNING(m_component_name, "Failed to show balloon notification, falling back to logging");
    }

    // Fallback to logging
    if (is_error) {
        PLEX_LOG_ERROR(m_component_name, "Notification: " + title + " - " + message);
    } else {
        PLEX_LOG_INFO(m_component_name, "Notification: " + title + " - " + message);
    }
}

void TrayIntegration::set_exit_callback(ExitCallback callback) {
    m_exit_callback = std::move(callback);
}

void TrayIntegration::set_show_config_callback(ShowConfigCallback callback) {
    m_show_config_callback = std::move(callback);
}

void TrayIntegration::set_update_check_callback(std::function<void()> callback) {
    m_update_check_callback = std::move(callback);
}

void TrayIntegration::show_update_notification(const std::string& title, const std::string& message, const std::string& download_url) {
    if (!m_tray_icon) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Showing update notification: " + title + " - " + message);

    auto* windows_tray = dynamic_cast<windows::WindowsTrayIcon*>(m_tray_icon.get());
    if (windows_tray) {
        auto result = windows_tray->show_update_notification(title, message, download_url);
        if (result) {
            return; // Success
        }
        PLEX_LOG_WARNING(m_component_name, "Failed to show update notification");
    }

    // Fallback to regular notification
    show_notification(title, message, false);
}

void TrayIntegration::setup_tray_menu() {
    if (!m_tray_icon) {
        return;
    }

    std::vector<MenuItem> menu_items;

    // Status item (disabled)
    MenuItem status_item;
    status_item.id = "status";
    status_item.label = m_current_status;
    status_item.type = MenuItemType::Action;
    status_item.enabled = false;
    menu_items.push_back(status_item);

    // Separator
    MenuItem separator1;
    separator1.type = MenuItemType::Separator;
    menu_items.push_back(separator1);

    // Show config item
    MenuItem show_config_item;
    show_config_item.id = "show_config";
    show_config_item.label = "Show Configuration";
    show_config_item.type = MenuItemType::Action;
    show_config_item.enabled = true;
    menu_items.push_back(show_config_item);

    // Separator
    MenuItem separator2;
    separator2.type = MenuItemType::Separator;
    menu_items.push_back(separator2);

    // Check for updates item
    MenuItem update_check_item;
    update_check_item.id = "check_updates";
    update_check_item.label = "Check for Updates";
    update_check_item.type = MenuItemType::Action;
    update_check_item.enabled = true;
    menu_items.push_back(update_check_item);

    // Separator
    MenuItem separator3;
    separator3.type = MenuItemType::Separator;
    menu_items.push_back(separator3);

    // Exit item
    MenuItem exit_item;
    exit_item.id = "exit";
    exit_item.label = "Exit";
    exit_item.type = MenuItemType::Action;
    exit_item.enabled = true;
    menu_items.push_back(exit_item);

    auto menu_result = m_tray_icon->set_menu(menu_items);
    if (!menu_result) {
        PLEX_LOG_WARNING(m_component_name, "Failed to set menu");
    }
}

void TrayIntegration::on_exit_clicked() {
    PLEX_LOG_INFO(m_component_name, "Exit selected from tray menu");
    if (m_exit_callback) {
        m_exit_callback();
    }
}

void TrayIntegration::on_show_config_clicked() {
    PLEX_LOG_INFO(m_component_name, "Show configuration selected from tray menu");
    if (m_show_config_callback) {
        m_show_config_callback();
    }
}

void TrayIntegration::on_update_check_clicked() {
    PLEX_LOG_INFO(m_component_name, "Check for updates selected from tray menu");
    if (m_update_check_callback) {
        m_update_check_callback();
    }
}

} // namespace windows
} // namespace platform
} // namespace presence_for_plex

#endif // _WIN32