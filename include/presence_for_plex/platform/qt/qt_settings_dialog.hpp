#pragma once

#include "presence_for_plex/core/models.hpp"
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QTextEdit>
#include <QLabel>
#include <QScrollArea>
#include <QGroupBox>
#include <memory>
#include <string>
#include <vector>

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
    void update_preview();

    void on_accept();
    void on_reject();
    void on_reset_defaults();
    void validate_inputs();
    void open_logs_folder();
    void scroll_to_section(int index);

private:
    // General settings
    QComboBox* m_log_level_combo = nullptr;
    QCheckBox* m_start_at_boot_check = nullptr;

    // Discord settings
    QCheckBox* m_discord_enabled_check = nullptr;
    QLineEdit* m_discord_client_id_edit = nullptr;
    QCheckBox* m_show_buttons_check = nullptr;
    QCheckBox* m_show_progress_check = nullptr;
    QCheckBox* m_show_artwork_check = nullptr;

    // Plex settings
    QCheckBox* m_plex_enabled_check = nullptr;
    QCheckBox* m_auto_discover_check = nullptr;
    QTextEdit* m_server_urls_edit = nullptr;

    // Media type filters
    QCheckBox* m_enable_movies_check = nullptr;
    QCheckBox* m_enable_tv_shows_check = nullptr;
    QCheckBox* m_enable_music_check = nullptr;

    // External services
    QLineEdit* m_tmdb_token_edit = nullptr;
    QCheckBox* m_enable_tmdb_check = nullptr;
    QCheckBox* m_enable_jikan_check = nullptr;

    // Rich presence format - TV Shows
    QLineEdit* m_tv_details_format_edit = nullptr;
    QLineEdit* m_tv_state_format_edit = nullptr;
    QLineEdit* m_tv_large_image_text_format_edit = nullptr;

    // Rich presence format - Movies
    QLineEdit* m_movie_details_format_edit = nullptr;
    QLineEdit* m_movie_state_format_edit = nullptr;
    QLineEdit* m_movie_large_image_text_format_edit = nullptr;

    // Rich presence format - Music
    QLineEdit* m_music_details_format_edit = nullptr;
    QLineEdit* m_music_state_format_edit = nullptr;
    QLineEdit* m_music_large_image_text_format_edit = nullptr;

    // Help text
    QTextEdit* m_format_help_text = nullptr;

    // Preview widgets
    QLabel* m_tv_preview_details = nullptr;
    QLabel* m_tv_preview_state = nullptr;
    QLabel* m_tv_preview_image_text = nullptr;
    QLabel* m_tv_preview_image = nullptr;
    QLabel* m_movie_preview_details = nullptr;
    QLabel* m_movie_preview_state = nullptr;
    QLabel* m_movie_preview_image_text = nullptr;
    QLabel* m_movie_preview_image = nullptr;
    QLabel* m_music_preview_details = nullptr;
    QLabel* m_music_preview_state = nullptr;
    QLabel* m_music_preview_image_text = nullptr;
    QLabel* m_music_preview_image = nullptr;

    // Navigation
    QScrollArea* m_scroll_area = nullptr;
    std::vector<QGroupBox*> m_sections;

    core::ApplicationConfig m_original_config;
};

} // namespace presence_for_plex::platform::qt
