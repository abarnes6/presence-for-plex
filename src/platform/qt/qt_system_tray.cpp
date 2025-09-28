#include "presence_for_plex/platform/qt/qt_system_tray.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QPixmap>
#include <QOperatingSystemVersion>
#include <QStandardPaths>

namespace presence_for_plex::platform::qt {

QtSystemTray::QtSystemTray() {
    PLEX_LOG_DEBUG(m_component_name, "QtSystemTray constructed");
}

QtSystemTray::~QtSystemTray() {
    PLEX_LOG_DEBUG(m_component_name, "QtSystemTray destructor called");
    shutdown();
}

std::expected<void, UiError> QtSystemTray::initialize() {
    std::lock_guard lock(m_mutex);

    if (m_initialized) {
        PLEX_LOG_DEBUG(m_component_name, "Already initialized");
        return {};
    }

    PLEX_LOG_INFO(m_component_name, "Initializing Qt system tray");

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        PLEX_LOG_ERROR(m_component_name, "System tray is not available on this system");
        return std::unexpected(UiError::NotSupported);
    }

    m_tray_icon = std::make_unique<QSystemTrayIcon>();
    m_context_menu = std::make_unique<QMenu>();

    QObject::connect(m_tray_icon.get(), &QSystemTrayIcon::activated,
                     [this](QSystemTrayIcon::ActivationReason reason) {
                         handle_activation(reason);
                     });

    m_tray_icon->setContextMenu(m_context_menu.get());

    auto icon_path = ":/icons/tray_icon";
    QIcon icon = load_icon_for_platform(icon_path);
    if (!icon.isNull()) {
        m_tray_icon->setIcon(icon);
    } else {
        QApplication::setWindowIcon(QIcon(":/icons/app_icon"));
        m_tray_icon->setIcon(QApplication::windowIcon());
    }

    m_initialized = true;
    PLEX_LOG_INFO(m_component_name, "Qt system tray initialized successfully");
    return {};
}

void QtSystemTray::shutdown() {
    std::lock_guard lock(m_mutex);

    if (!m_initialized) {
        return;
    }

    PLEX_LOG_INFO(m_component_name, "Shutting down Qt system tray");

    // Disconnect all signals first to prevent any callbacks during cleanup
    if (m_tray_icon) {
        QObject::disconnect(m_tray_icon.get(), nullptr, nullptr, nullptr);
        m_tray_icon->hide();
    }

    // Disconnect all action signals before clearing
    for (auto& [id, action] : m_action_map) {
        if (action) {
            QObject::disconnect(action, nullptr, nullptr, nullptr);
        }
    }

    // Clear the menu and actions before destroying the tray icon
    if (m_context_menu) {
        m_context_menu->clear();
    }
    m_action_map.clear();
    m_action_id_map.clear();

    // Now safely destroy the Qt objects
    m_context_menu.reset();
    m_tray_icon.reset();

    m_initialized = false;
    PLEX_LOG_INFO(m_component_name, "Qt system tray shut down");
}

bool QtSystemTray::is_initialized() const {
    std::lock_guard lock(m_mutex);
    return m_initialized;
}

std::expected<void, UiError> QtSystemTray::set_icon(const std::string& icon_path) {
    std::lock_guard lock(m_mutex);

    if (!m_initialized || !m_tray_icon) {
        return std::unexpected(UiError::NotSupported);
    }

    QString path = QString::fromStdString(icon_path);
    QIcon icon = load_icon_for_platform(icon_path);

    if (icon.isNull()) {
        if (!QFile::exists(path)) {
            PLEX_LOG_ERROR(m_component_name, "Icon file not found: " + icon_path);
            return std::unexpected(UiError::ResourceNotFound);
        }

        QPixmap pixmap(path);
        if (pixmap.isNull()) {
            PLEX_LOG_ERROR(m_component_name, "Failed to load icon: " + icon_path);
            return std::unexpected(UiError::ResourceNotFound);
        }
        icon = QIcon(pixmap);
    }

    m_tray_icon->setIcon(icon);
    PLEX_LOG_DEBUG(m_component_name, "Icon set to: " + icon_path);
    return {};
}

std::expected<void, UiError> QtSystemTray::set_icon_from_resource(int resource_id) {
    std::lock_guard lock(m_mutex);

    if (!m_initialized || !m_tray_icon) {
        return std::unexpected(UiError::NotSupported);
    }

    QString resource_path = QString(":/icons/icon_%1").arg(resource_id);
    return set_icon(resource_path.toStdString());
}

std::expected<void, UiError> QtSystemTray::set_tooltip(const std::string& tooltip) {
    std::lock_guard lock(m_mutex);

    if (!m_initialized || !m_tray_icon) {
        return std::unexpected(UiError::NotSupported);
    }

    m_tray_icon->setToolTip(QString::fromStdString(tooltip));
    PLEX_LOG_DEBUG(m_component_name, "Tooltip set to: " + tooltip);
    return {};
}

std::expected<void, UiError> QtSystemTray::set_menu(const std::vector<MenuItem>& items) {
    std::lock_guard lock(m_mutex);

    if (!m_initialized || !m_tray_icon) {
        return std::unexpected(UiError::NotSupported);
    }

    m_context_menu->clear();
    m_action_map.clear();
    m_action_id_map.clear();

    for (const auto& item : items) {
        if (item.type == MenuItemType::Separator) {
            m_context_menu->addSeparator();
        } else if (item.type == MenuItemType::Submenu) {
            QMenu* submenu = create_menu_from_items(item.submenu, m_context_menu.get());
            submenu->setTitle(QString::fromStdString(item.label));
            if (!item.icon_path.empty()) {
                submenu->setIcon(QIcon(QString::fromStdString(item.icon_path)));
            }
            m_context_menu->addMenu(submenu);
        } else {
            QAction* action = new QAction(QString::fromStdString(item.label), m_context_menu.get());

            if (!item.tooltip.empty()) {
                action->setToolTip(QString::fromStdString(item.tooltip));
            }

            if (!item.icon_path.empty()) {
                action->setIcon(QIcon(QString::fromStdString(item.icon_path)));
            }

            action->setEnabled(item.enabled);

            if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
                action->setCheckable(true);
                action->setChecked(item.checked);
            }

            QObject::connect(action, &QAction::triggered, [this, action]() {
                handle_menu_triggered(action);
            });

            m_context_menu->addAction(action);
            m_action_map[item.id] = action;
            m_action_id_map[action] = item.id;

            if (item.action) {
                QObject::connect(action, &QAction::triggered, item.action);
            }
        }
    }

    PLEX_LOG_DEBUG(m_component_name, "Menu updated with " + std::to_string(items.size()) + " items");
    return {};
}

std::expected<void, UiError> QtSystemTray::update_menu_item(const std::string& id, const MenuItem& item) {
    std::lock_guard lock(m_mutex);

    QAction* action = find_action_by_id(id);
    if (!action) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    action->setText(QString::fromStdString(item.label));

    if (!item.tooltip.empty()) {
        action->setToolTip(QString::fromStdString(item.tooltip));
    }

    if (!item.icon_path.empty()) {
        action->setIcon(QIcon(QString::fromStdString(item.icon_path)));
    }

    action->setEnabled(item.enabled);

    if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
        action->setCheckable(true);
        action->setChecked(item.checked);
    }

    PLEX_LOG_DEBUG(m_component_name, "Menu item updated: " + id);
    return {};
}

std::expected<void, UiError> QtSystemTray::enable_menu_item(const std::string& id, bool enabled) {
    std::lock_guard lock(m_mutex);

    QAction* action = find_action_by_id(id);
    if (!action) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    action->setEnabled(enabled);
    PLEX_LOG_DEBUG(m_component_name, "Menu item " + id + " enabled: " + std::to_string(enabled));
    return {};
}

std::expected<void, UiError> QtSystemTray::check_menu_item(const std::string& id, bool checked) {
    std::lock_guard lock(m_mutex);

    QAction* action = find_action_by_id(id);
    if (!action) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    action->setChecked(checked);
    PLEX_LOG_DEBUG(m_component_name, "Menu item " + id + " checked: " + std::to_string(checked));
    return {};
}

void QtSystemTray::set_click_callback(ClickCallback callback) {
    std::lock_guard lock(m_mutex);
    m_click_callback = callback;
}

void QtSystemTray::set_double_click_callback(ClickCallback callback) {
    std::lock_guard lock(m_mutex);
    m_double_click_callback = callback;
}

void QtSystemTray::set_menu_callback(MenuCallback callback) {
    std::lock_guard lock(m_mutex);
    m_menu_callback = callback;
}

void QtSystemTray::show() {
    std::lock_guard lock(m_mutex);

    if (m_initialized && m_tray_icon) {
        m_tray_icon->show();
        PLEX_LOG_DEBUG(m_component_name, "System tray shown");
    }
}

void QtSystemTray::hide() {
    std::lock_guard lock(m_mutex);

    if (m_initialized && m_tray_icon) {
        m_tray_icon->hide();
        PLEX_LOG_DEBUG(m_component_name, "System tray hidden");
    }
}

bool QtSystemTray::is_visible() const {
    std::lock_guard lock(m_mutex);
    return m_initialized && m_tray_icon && m_tray_icon->isVisible();
}

void QtSystemTray::on_click() {
    if (m_click_callback) {
        m_click_callback();
    }
}

void QtSystemTray::on_double_click() {
    if (m_double_click_callback) {
        m_double_click_callback();
    }
}

void QtSystemTray::on_menu_item_selected(const std::string& item_id) {
    if (m_menu_callback) {
        m_menu_callback(item_id);
    }
}

void QtSystemTray::handle_activation(QSystemTrayIcon::ActivationReason reason) {
    switch (reason) {
        case QSystemTrayIcon::Trigger:
            on_click();
            break;
        case QSystemTrayIcon::DoubleClick:
            on_double_click();
            break;
        case QSystemTrayIcon::Context:
        case QSystemTrayIcon::MiddleClick:
        default:
            break;
    }
}

void QtSystemTray::handle_menu_triggered(QAction* action) {
    if (action) {
        auto it = m_action_id_map.find(action);
        if (it != m_action_id_map.end()) {
            on_menu_item_selected(it->second);
        }
    }
}

QMenu* QtSystemTray::create_menu_from_items(const std::vector<MenuItem>& items, QWidget* parent) {
    QMenu* menu = new QMenu(parent);

    for (const auto& item : items) {
        if (item.type == MenuItemType::Separator) {
            menu->addSeparator();
        } else if (item.type == MenuItemType::Submenu) {
            QMenu* submenu = create_menu_from_items(item.submenu, menu);
            submenu->setTitle(QString::fromStdString(item.label));
            if (!item.icon_path.empty()) {
                submenu->setIcon(QIcon(QString::fromStdString(item.icon_path)));
            }
            menu->addMenu(submenu);
        } else {
            QAction* action = new QAction(QString::fromStdString(item.label), menu);

            if (!item.tooltip.empty()) {
                action->setToolTip(QString::fromStdString(item.tooltip));
            }

            if (!item.icon_path.empty()) {
                action->setIcon(QIcon(QString::fromStdString(item.icon_path)));
            }

            action->setEnabled(item.enabled);

            if (item.type == MenuItemType::Checkbox || item.type == MenuItemType::Radio) {
                action->setCheckable(true);
                action->setChecked(item.checked);
            }

            QObject::connect(action, &QAction::triggered, [this, action]() {
                handle_menu_triggered(action);
            });

            menu->addAction(action);
            m_action_map[item.id] = action;
            m_action_id_map[action] = item.id;

            if (item.action) {
                QObject::connect(action, &QAction::triggered, item.action);
            }
        }
    }

    return menu;
}

QAction* QtSystemTray::find_action_by_id(const std::string& id) {
    auto it = m_action_map.find(id);
    if (it != m_action_map.end()) {
        return it->second;
    }
    return nullptr;
}

QIcon QtSystemTray::load_icon_for_platform(const std::string& base_path) {
    QString path = QString::fromStdString(base_path);

    #ifdef Q_OS_MACOS
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion::MacOSBigSur) {
        QString template_path = path + "_template";
        if (QFile::exists(template_path + ".png") || QFile::exists(template_path + ".svg")) {
            return QIcon(template_path + ".png");
        }
    }
    #endif

    #ifdef Q_OS_LINUX
    QString theme_suffix = QIcon::themeName().contains("dark") ? "_dark" : "_light";
    QString themed_path = path + theme_suffix;
    if (QFile::exists(themed_path + ".svg")) {
        return QIcon(themed_path + ".svg");
    } else if (QFile::exists(themed_path + ".png")) {
        return QIcon(themed_path + ".png");
    }
    #endif

    if (QFile::exists(path + ".svg")) {
        return QIcon(path + ".svg");
    } else if (QFile::exists(path + ".png")) {
        return QIcon(path + ".png");
    } else if (QFile::exists(path + ".ico")) {
        return QIcon(path + ".ico");
    }

    return QIcon();
}

} // namespace presence_for_plex::platform::qt