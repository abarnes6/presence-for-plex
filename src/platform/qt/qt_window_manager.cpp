#include "presence_for_plex/platform/qt/qt_window_manager.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <QGuiApplication>
#include <QWindowList>
#include <QScreen>

namespace presence_for_plex::platform::qt {

QtWindowManager::QtWindowManager() {
    PLEX_LOG_DEBUG(m_component_name, "QtWindowManager constructed");
}

QtWindowManager::~QtWindowManager() {
    PLEX_LOG_DEBUG(m_component_name, "QtWindowManager destructor called");
}

std::vector<std::string> QtWindowManager::find_windows_by_title(const std::string& title) const {
    std::vector<std::string> result;
    QString search_title = QString::fromStdString(title);

    for (QWindow* window : QGuiApplication::allWindows()) {
        if (window->title().contains(search_title, Qt::CaseInsensitive)) {
            result.push_back(window_to_id(window));
        }
    }

    return result;
}

std::vector<std::string> QtWindowManager::find_windows_by_class(const std::string& class_name) const {
    std::vector<std::string> result;

    for (QWindow* window : QGuiApplication::allWindows()) {
        if (window->objectName() == QString::fromStdString(class_name)) {
            result.push_back(window_to_id(window));
        }
    }

    return result;
}

std::optional<std::string> QtWindowManager::find_window_by_process(const std::string& process_name) const {
    if (QGuiApplication::applicationName().toStdString() == process_name) {
        auto windows = QGuiApplication::allWindows();
        if (!windows.isEmpty()) {
            return window_to_id(windows.first());
        }
    }

    return std::nullopt;
}

std::expected<void, UiError> QtWindowManager::bring_to_front(const std::string& window_id) {
    QWindow* window = find_window_from_id(window_id);
    if (!window) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    window->raise();
    window->requestActivate();

    PLEX_LOG_DEBUG(m_component_name, "Window brought to front: " + window_id);
    return {};
}

std::expected<void, UiError> QtWindowManager::minimize_window(const std::string& window_id) {
    QWindow* window = find_window_from_id(window_id);
    if (!window) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    window->showMinimized();

    PLEX_LOG_DEBUG(m_component_name, "Window minimized: " + window_id);
    return {};
}

std::expected<void, UiError> QtWindowManager::maximize_window(const std::string& window_id) {
    QWindow* window = find_window_from_id(window_id);
    if (!window) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    window->showMaximized();

    PLEX_LOG_DEBUG(m_component_name, "Window maximized: " + window_id);
    return {};
}

std::expected<void, UiError> QtWindowManager::close_window(const std::string& window_id) {
    QWindow* window = find_window_from_id(window_id);
    if (!window) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    window->close();

    PLEX_LOG_DEBUG(m_component_name, "Window closed: " + window_id);
    return {};
}

std::optional<std::string> QtWindowManager::get_window_title(const std::string& window_id) const {
    QWindow* window = find_window_from_id(window_id);
    if (!window) {
        return std::nullopt;
    }

    return window->title().toStdString();
}

std::expected<bool, UiError> QtWindowManager::is_window_visible(const std::string& window_id) const {
    QWindow* window = find_window_from_id(window_id);
    if (!window) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    return window->isVisible();
}

std::expected<bool, UiError> QtWindowManager::is_window_minimized(const std::string& window_id) const {
    QWindow* window = find_window_from_id(window_id);
    if (!window) {
        return std::unexpected(UiError::ResourceNotFound);
    }

    return window->windowState() == Qt::WindowMinimized;
}

QWindow* QtWindowManager::find_window_from_id(const std::string& window_id) const {
    for (QWindow* window : QGuiApplication::allWindows()) {
        if (window_to_id(window) == window_id) {
            return window;
        }
    }
    return nullptr;
}

std::string QtWindowManager::window_to_id(QWindow* window) const {
    return std::to_string(reinterpret_cast<std::uintptr_t>(window));
}

} // namespace presence_for_plex::platform::qt