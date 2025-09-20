#pragma once

#include "presence_for_plex/platform/ui_service.hpp"
#include <QWindow>

namespace presence_for_plex::platform::qt {

class QtWindowManager : public WindowManager {
public:
    QtWindowManager();
    ~QtWindowManager() override;

    std::vector<std::string> find_windows_by_title(const std::string& title) const override;
    std::vector<std::string> find_windows_by_class(const std::string& class_name) const override;
    std::optional<std::string> find_window_by_process(const std::string& process_name) const override;

    std::expected<void, UiError> bring_to_front(const std::string& window_id) override;
    std::expected<void, UiError> minimize_window(const std::string& window_id) override;
    std::expected<void, UiError> maximize_window(const std::string& window_id) override;
    std::expected<void, UiError> close_window(const std::string& window_id) override;

    std::optional<std::string> get_window_title(const std::string& window_id) const override;
    std::expected<bool, UiError> is_window_visible(const std::string& window_id) const override;
    std::expected<bool, UiError> is_window_minimized(const std::string& window_id) const override;

private:
    QWindow* find_window_from_id(const std::string& window_id) const;
    std::string window_to_id(QWindow* window) const;

    std::string m_component_name = "QtWindowManager";
};

} // namespace presence_for_plex::platform::qt