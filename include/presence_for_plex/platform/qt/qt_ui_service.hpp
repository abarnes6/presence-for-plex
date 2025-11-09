#pragma once

#include "presence_for_plex/platform/ui_service.hpp"
#include <QApplication>
#include <memory>

namespace presence_for_plex::platform::qt {

class QtUiService : public UiService {
public:
    QtUiService();
    ~QtUiService() override;

    std::expected<void, UiError> initialize() override;
    void shutdown() override;
    bool is_initialized() const override;

    std::unique_ptr<SystemTray> create_system_tray() override;

    bool supports_system_tray() const override;

    void process_events() override;
    void quit_event_loop() override;

    QApplication* get_application() { return m_app; }

private:
    QApplication* m_app = nullptr;
    bool m_initialized = false;
    std::string m_component_name = "QtUiService";
};

} // namespace presence_for_plex::platform::qt