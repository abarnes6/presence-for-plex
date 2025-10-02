#pragma once

#include "presence_for_plex/core/models.hpp"
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QTextEdit>
#include <memory>

namespace presence_for_plex::platform::qt {

class QtSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit QtSettingsDialog(const core::ApplicationConfig& config, QWidget* parent = nullptr);
    ~QtSettingsDialog() override = default;

    core::ApplicationConfig get_config() const;

private:
    void setup_ui();
    void load_config(const core::ApplicationConfig& config);
    void create_general_tab();
    void create_discord_tab();
    void create_plex_tab();
    void create_services_tab();
    void create_format_tab();

    void on_accept();
    void on_reject();
    void on_reset_defaults();
    void validate_inputs();

private:
    // General settings
    QComboBox* m_log_level_combo = nullptr;
    QCheckBox* m_start_minimized_check = nullptr;

    // Discord settings
    QLineEdit* m_discord_client_id_edit = nullptr;
    QCheckBox* m_show_buttons_check = nullptr;
    QCheckBox* m_show_progress_check = nullptr;
    QSpinBox* m_update_interval_spin = nullptr;
    QCheckBox* m_show_artwork_check = nullptr;

    // Plex settings
    QCheckBox* m_auto_discover_check = nullptr;
    QSpinBox* m_poll_interval_spin = nullptr;
    QSpinBox* m_timeout_spin = nullptr;
    QTextEdit* m_server_urls_edit = nullptr;

    // External services
    QLineEdit* m_tmdb_token_edit = nullptr;
    QCheckBox* m_enable_tmdb_check = nullptr;
    QCheckBox* m_enable_jikan_check = nullptr;

    // Rich presence format
    QLineEdit* m_details_format_edit = nullptr;
    QLineEdit* m_state_format_edit = nullptr;
    QTextEdit* m_format_help_text = nullptr;

    core::ApplicationConfig m_original_config;
};

} // namespace presence_for_plex::platform::qt
