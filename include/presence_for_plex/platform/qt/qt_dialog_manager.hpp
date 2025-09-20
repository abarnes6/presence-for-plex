#pragma once

#include "presence_for_plex/platform/ui_service.hpp"
#include <QWidget>

namespace presence_for_plex::platform::qt {

class QtDialogManager : public DialogManager {
public:
    QtDialogManager(QWidget* parent = nullptr);
    ~QtDialogManager() override;

    std::expected<DialogResult, UiError> show_message(
        const std::string& title,
        const std::string& message,
        DialogType type = DialogType::Info
    ) override;

    std::expected<DialogResult, UiError> show_question(
        const std::string& title,
        const std::string& question,
        bool show_cancel = true
    ) override;

    std::expected<std::string, UiError> show_input_dialog(
        const std::string& title,
        const std::string& prompt,
        const std::string& default_value = ""
    ) override;

    std::expected<std::string, UiError> show_password_dialog(
        const std::string& title,
        const std::string& prompt
    ) override;

    std::expected<std::string, UiError> show_open_file_dialog(
        const std::string& title,
        const std::string& filter = "",
        const std::string& initial_dir = ""
    ) override;

    std::expected<std::string, UiError> show_save_file_dialog(
        const std::string& title,
        const std::string& filter = "",
        const std::string& initial_dir = "",
        const std::string& default_name = ""
    ) override;

    std::expected<std::string, UiError> show_folder_dialog(
        const std::string& title,
        const std::string& initial_dir = ""
    ) override;

private:
    QWidget* m_parent;
    std::string m_component_name = "QtDialogManager";
};

} // namespace presence_for_plex::platform::qt