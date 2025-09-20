#pragma once

#include "presence_for_plex/platform/ui_service.hpp"
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <memory>
#include <mutex>

class QAction;

namespace presence_for_plex::platform::qt {

class QtSystemTray : public SystemTray {
public:
    QtSystemTray();
    ~QtSystemTray() override;

    std::expected<void, UiError> initialize() override;
    void shutdown() override;
    bool is_initialized() const override;

    std::expected<void, UiError> set_icon(const std::string& icon_path) override;
    std::expected<void, UiError> set_icon_from_resource(int resource_id) override;
    std::expected<void, UiError> set_tooltip(const std::string& tooltip) override;

    std::expected<void, UiError> set_menu(const std::vector<MenuItem>& items) override;
    std::expected<void, UiError> update_menu_item(const std::string& id, const MenuItem& item) override;
    std::expected<void, UiError> enable_menu_item(const std::string& id, bool enabled) override;
    std::expected<void, UiError> check_menu_item(const std::string& id, bool checked) override;

    void set_click_callback(ClickCallback callback) override;
    void set_double_click_callback(ClickCallback callback) override;
    void set_menu_callback(MenuCallback callback) override;

    void show() override;
    void hide() override;
    bool is_visible() const override;

protected:
    void on_click() override;
    void on_double_click() override;
    void on_menu_item_selected(const std::string& item_id) override;

private:
    void handle_activation(QSystemTrayIcon::ActivationReason reason);
    void handle_menu_triggered();
    QMenu* create_menu_from_items(const std::vector<MenuItem>& items, QWidget* parent = nullptr);
    QAction* find_action_by_id(const std::string& id);
    QIcon load_icon_for_platform(const std::string& base_path);

    std::unique_ptr<QSystemTrayIcon> m_tray_icon;
    std::unique_ptr<QMenu> m_context_menu;
    std::map<std::string, QAction*> m_action_map;
    std::map<QAction*, std::string> m_action_id_map;

    ClickCallback m_click_callback;
    ClickCallback m_double_click_callback;
    MenuCallback m_menu_callback;

    bool m_initialized = false;
    mutable std::mutex m_mutex;
    std::string m_component_name = "QtSystemTray";
};

} // namespace presence_for_plex::platform::qt