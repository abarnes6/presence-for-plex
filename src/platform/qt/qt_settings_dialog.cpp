#include "presence_for_plex/platform/qt/qt_settings_dialog.hpp"
#include "presence_for_plex/platform/system_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFont>
#include <QDesktopServices>
#include <QUrl>
#include <filesystem>

namespace presence_for_plex::platform::qt {

QtSettingsDialog::QtSettingsDialog(const core::ApplicationConfig& config, QWidget* parent)
    : QDialog(parent), m_original_config(config) {
    setWindowTitle("Settings - Presence for Plex");
    setMinimumSize(600, 500);
    setup_ui();
    load_config(config);
}

void QtSettingsDialog::setup_ui() {
    auto* main_layout = new QVBoxLayout(this);

    auto* tab_widget = new QTabWidget(this);

    create_general_tab();
    create_discord_tab();
    create_plex_tab();
    create_services_tab();
    create_format_tab();

    auto* general_widget = new QWidget();
    auto* general_layout = new QVBoxLayout(general_widget);

    auto* general_group = new QGroupBox("General Settings");
    auto* general_form = new QFormLayout(general_group);

    m_log_level_combo = new QComboBox();
    m_log_level_combo->addItems({"Debug", "Info", "Warning", "Error"});
    general_form->addRow("Log Level:", m_log_level_combo);

    m_start_at_boot_check = new QCheckBox("Start at boot");
    general_form->addRow(m_start_at_boot_check);

    auto* logs_button = new QPushButton("Open Logs Folder");
    connect(logs_button, &QPushButton::clicked, this, &QtSettingsDialog::open_logs_folder);
    general_form->addRow("Logs:", logs_button);

    general_layout->addWidget(general_group);
    general_layout->addStretch();

    tab_widget->addTab(general_widget, "General");

    auto* discord_widget = new QWidget();
    auto* discord_layout = new QVBoxLayout(discord_widget);

    auto* discord_group = new QGroupBox("Discord Rich Presence");
    auto* discord_form = new QFormLayout(discord_group);

    m_discord_enabled_check = new QCheckBox("Enable Discord Rich Presence");
    discord_form->addRow(m_discord_enabled_check);

    m_discord_client_id_edit = new QLineEdit();
    m_discord_client_id_edit->setPlaceholderText("Discord Application Client ID");
    discord_form->addRow("Client ID:", m_discord_client_id_edit);

    m_show_buttons_check = new QCheckBox("Show action buttons");
    discord_form->addRow(m_show_buttons_check);

    m_show_progress_check = new QCheckBox("Show playback progress");
    discord_form->addRow(m_show_progress_check);

    m_show_artwork_check = new QCheckBox("Show movie/TV artwork as image");
    discord_form->addRow(m_show_artwork_check);

    discord_layout->addWidget(discord_group);
    discord_layout->addStretch();

    tab_widget->addTab(discord_widget, "Discord");

    auto* plex_widget = new QWidget();
    auto* plex_layout = new QVBoxLayout(plex_widget);

    auto* plex_group = new QGroupBox("Plex Media Server");
    auto* plex_form = new QFormLayout(plex_group);

    m_plex_enabled_check = new QCheckBox("Enable Plex Media Service");
    plex_form->addRow(m_plex_enabled_check);

    m_auto_discover_check = new QCheckBox("Auto-discover local servers");
    plex_form->addRow(m_auto_discover_check);

    m_server_urls_edit = new QTextEdit();
    m_server_urls_edit->setPlaceholderText("One URL per line\nExample: http://192.168.1.100:32400");
    m_server_urls_edit->setMaximumHeight(100);
    plex_form->addRow("Manual Server URLs:", m_server_urls_edit);

    plex_layout->addWidget(plex_group);

    auto* filter_group = new QGroupBox("Media Type Filters");
    auto* filter_layout = new QVBoxLayout(filter_group);

    auto* filter_help = new QLabel("Select which media types to show in Discord Rich Presence:");
    filter_help->setWordWrap(true);
    QFont help_font = filter_help->font();
    help_font.setPointSize(help_font.pointSize() - 1);
    filter_help->setFont(help_font);
    filter_help->setStyleSheet("color: gray;");
    filter_layout->addWidget(filter_help);

    m_enable_movies_check = new QCheckBox("Show Movies");
    filter_layout->addWidget(m_enable_movies_check);

    m_enable_tv_shows_check = new QCheckBox("Show TV Shows");
    filter_layout->addWidget(m_enable_tv_shows_check);

    m_enable_music_check = new QCheckBox("Show Music");
    filter_layout->addWidget(m_enable_music_check);

    plex_layout->addWidget(filter_group);
    plex_layout->addStretch();

    tab_widget->addTab(plex_widget, "Plex");

    auto* services_widget = new QWidget();
    auto* services_layout = new QVBoxLayout(services_widget);

    auto* tmdb_group = new QGroupBox("The Movie Database (TMDB)");
    auto* tmdb_layout = new QVBoxLayout(tmdb_group);

    m_enable_tmdb_check = new QCheckBox("Enable TMDB integration");
    tmdb_layout->addWidget(m_enable_tmdb_check);

    auto* tmdb_form = new QFormLayout();
    m_tmdb_token_edit = new QLineEdit();
    m_tmdb_token_edit->setPlaceholderText("Enter TMDB API Access Token");
    tmdb_form->addRow("API Token:", m_tmdb_token_edit);
    tmdb_layout->addLayout(tmdb_form);

    auto* tmdb_help = new QLabel("TMDB provides enhanced metadata and artwork for movies and TV shows.");
    tmdb_help->setWordWrap(true);
    QFont tmdb_help_font = tmdb_help->font();
    tmdb_help_font.setPointSize(tmdb_help_font.pointSize() - 1);
    tmdb_help->setFont(tmdb_help_font);
    tmdb_help->setStyleSheet("color: gray;");
    tmdb_layout->addWidget(tmdb_help);

    services_layout->addWidget(tmdb_group);

    auto* jikan_group = new QGroupBox("Jikan API (MyAnimeList)");
    auto* jikan_layout = new QVBoxLayout(jikan_group);

    m_enable_jikan_check = new QCheckBox("Enable Jikan/MyAnimeList integration");
    jikan_layout->addWidget(m_enable_jikan_check);

    auto* jikan_help = new QLabel("Jikan provides anime metadata from MyAnimeList. No API key required.");
    jikan_help->setWordWrap(true);
    QFont jikan_help_font = jikan_help->font();
    jikan_help_font.setPointSize(jikan_help_font.pointSize() - 1);
    jikan_help->setFont(jikan_help_font);
    jikan_help->setStyleSheet("color: gray;");
    jikan_layout->addWidget(jikan_help);

    services_layout->addWidget(jikan_group);
    services_layout->addStretch();

    tab_widget->addTab(services_widget, "External Services");

    auto* format_widget = new QWidget();
    auto* format_layout = new QVBoxLayout(format_widget);

    // TV Shows format group
    auto* tv_format_group = new QGroupBox("TV Shows Format");
    auto* tv_format_form = new QFormLayout(tv_format_group);

    m_tv_details_format_edit = new QLineEdit();
    m_tv_details_format_edit->setPlaceholderText("{show}");
    tv_format_form->addRow("Details Line:", m_tv_details_format_edit);

    m_tv_state_format_edit = new QLineEdit();
    m_tv_state_format_edit->setPlaceholderText("{se} - {title}");
    tv_format_form->addRow("State Line:", m_tv_state_format_edit);

    m_tv_large_image_text_format_edit = new QLineEdit();
    m_tv_large_image_text_format_edit->setPlaceholderText("{title}");
    tv_format_form->addRow("Image Hover Text:", m_tv_large_image_text_format_edit);

    format_layout->addWidget(tv_format_group);

    // Movies format group
    auto* movie_format_group = new QGroupBox("Movies Format");
    auto* movie_format_form = new QFormLayout(movie_format_group);

    m_movie_details_format_edit = new QLineEdit();
    m_movie_details_format_edit->setPlaceholderText("{title} ({year})");
    movie_format_form->addRow("Details Line:", m_movie_details_format_edit);

    m_movie_state_format_edit = new QLineEdit();
    m_movie_state_format_edit->setPlaceholderText("{genres}");
    movie_format_form->addRow("State Line:", m_movie_state_format_edit);

    m_movie_large_image_text_format_edit = new QLineEdit();
    m_movie_large_image_text_format_edit->setPlaceholderText("{title}");
    movie_format_form->addRow("Image Hover Text:", m_movie_large_image_text_format_edit);

    format_layout->addWidget(movie_format_group);

    // Music format group
    auto* music_format_group = new QGroupBox("Music Format");
    auto* music_format_form = new QFormLayout(music_format_group);

    m_music_details_format_edit = new QLineEdit();
    m_music_details_format_edit->setPlaceholderText("{title}");
    music_format_form->addRow("Details Line:", m_music_details_format_edit);

    m_music_state_format_edit = new QLineEdit();
    m_music_state_format_edit->setPlaceholderText("{artist} - {album}");
    music_format_form->addRow("State Line:", m_music_state_format_edit);

    m_music_large_image_text_format_edit = new QLineEdit();
    m_music_large_image_text_format_edit->setPlaceholderText("{title}");
    music_format_form->addRow("Image Hover Text:", m_music_large_image_text_format_edit);

    format_layout->addWidget(music_format_group);

    // Help text
    m_format_help_text = new QTextEdit();
    m_format_help_text->setReadOnly(true);
    m_format_help_text->setMaximumHeight(200);
    m_format_help_text->setPlainText(
        "Basic: {title} {original_title} {year} {studio} {type} {summary}\n"
        "TV Shows: {show} {season} {episode} {season_padded} {episode_padded} {se} {SxE}\n"
        "Music: {artist} {album} {track}\n"
        "Playback: {state} {progress} {duration} {remaining} {progress_percentage}\n"
        "Other: {username} {genre} {genres} {rating}\n\n"
        "Defaults:\n"
        "• TV: \"{show}\" + \"{se} - {title}\"\n"
        "• Movie: \"{title} ({year})\" + \"{genres}\"\n"
        "• Music: \"{title}\" + \"{artist} - {album}\""
    );
    format_layout->addWidget(new QLabel("Available Placeholders:"));
    format_layout->addWidget(m_format_help_text);
    format_layout->addStretch();

    tab_widget->addTab(format_widget, "Format");

    main_layout->addWidget(tab_widget);

    auto* button_layout = new QHBoxLayout();

    auto* reset_button = new QPushButton("Reset to Defaults");
    connect(reset_button, &QPushButton::clicked, this, &QtSettingsDialog::on_reset_defaults);
    button_layout->addWidget(reset_button);

    button_layout->addStretch();

    auto* button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(button_box, &QDialogButtonBox::accepted, this, &QtSettingsDialog::on_accept);
    connect(button_box, &QDialogButtonBox::rejected, this, &QtSettingsDialog::on_reject);
    button_layout->addWidget(button_box);

    main_layout->addLayout(button_layout);
}

void QtSettingsDialog::load_config(const core::ApplicationConfig& config) {
    int log_level_index = static_cast<int>(config.log_level);
    m_log_level_combo->setCurrentIndex(log_level_index);

    // Check actual autostart status from system
    auto autostart_manager = platform::AutostartManager::create("PresenceForPlex");
    auto autostart_status = autostart_manager->is_autostart_enabled();
    if (autostart_status) {
        m_start_at_boot_check->setChecked(*autostart_status);
    } else {
        m_start_at_boot_check->setChecked(config.start_at_boot);
    }

    m_discord_enabled_check->setChecked(config.presence.enabled);
    m_discord_client_id_edit->setText(QString::fromStdString(config.presence.discord.client_id));
    m_show_buttons_check->setChecked(config.presence.discord.show_buttons);
    m_show_progress_check->setChecked(config.presence.discord.show_progress);
    m_show_artwork_check->setChecked(config.presence.discord.show_artwork);

    // TV Shows format
    m_tv_details_format_edit->setText(QString::fromStdString(config.presence.discord.tv_details_format));
    m_tv_state_format_edit->setText(QString::fromStdString(config.presence.discord.tv_state_format));
    m_tv_large_image_text_format_edit->setText(QString::fromStdString(config.presence.discord.tv_large_image_text_format));

    // Movies format
    m_movie_details_format_edit->setText(QString::fromStdString(config.presence.discord.movie_details_format));
    m_movie_state_format_edit->setText(QString::fromStdString(config.presence.discord.movie_state_format));
    m_movie_large_image_text_format_edit->setText(QString::fromStdString(config.presence.discord.movie_large_image_text_format));

    // Music format
    m_music_details_format_edit->setText(QString::fromStdString(config.presence.discord.music_details_format));
    m_music_state_format_edit->setText(QString::fromStdString(config.presence.discord.music_state_format));
    m_music_large_image_text_format_edit->setText(QString::fromStdString(config.presence.discord.music_large_image_text_format));

    m_plex_enabled_check->setChecked(config.media_services.plex.enabled);
    m_auto_discover_check->setChecked(config.media_services.plex.auto_discover);

    m_enable_movies_check->setChecked(config.media_services.plex.enable_movies);
    m_enable_tv_shows_check->setChecked(config.media_services.plex.enable_tv_shows);
    m_enable_music_check->setChecked(config.media_services.plex.enable_music);

    if (!config.media_services.plex.server_urls.empty()) {
        QString urls;
        for (const auto& url : config.media_services.plex.server_urls) {
            urls += QString::fromStdString(url) + "\n";
        }
        m_server_urls_edit->setPlainText(urls.trimmed());
    }

    m_tmdb_token_edit->setText(QString::fromStdString(config.tmdb_access_token));
    m_enable_tmdb_check->setChecked(config.enable_tmdb);
    m_enable_jikan_check->setChecked(config.enable_jikan);
}

core::ApplicationConfig QtSettingsDialog::get_config() const {
    core::ApplicationConfig config = m_original_config;

    config.log_level = static_cast<utils::LogLevel>(m_log_level_combo->currentIndex());
    config.start_at_boot = m_start_at_boot_check->isChecked();

    config.presence.enabled = m_discord_enabled_check->isChecked();
    config.presence.discord.client_id = m_discord_client_id_edit->text().toStdString();
    config.presence.discord.show_buttons = m_show_buttons_check->isChecked();
    config.presence.discord.show_progress = m_show_progress_check->isChecked();
    config.presence.discord.show_artwork = m_show_artwork_check->isChecked();
    // TV Shows format
    config.presence.discord.tv_details_format = m_tv_details_format_edit->text().toStdString();
    config.presence.discord.tv_state_format = m_tv_state_format_edit->text().toStdString();
    config.presence.discord.tv_large_image_text_format = m_tv_large_image_text_format_edit->text().toStdString();

    // Movies format
    config.presence.discord.movie_details_format = m_movie_details_format_edit->text().toStdString();
    config.presence.discord.movie_state_format = m_movie_state_format_edit->text().toStdString();
    config.presence.discord.movie_large_image_text_format = m_movie_large_image_text_format_edit->text().toStdString();

    // Music format
    config.presence.discord.music_details_format = m_music_details_format_edit->text().toStdString();
    config.presence.discord.music_state_format = m_music_state_format_edit->text().toStdString();
    config.presence.discord.music_large_image_text_format = m_music_large_image_text_format_edit->text().toStdString();

    config.media_services.plex.enabled = m_plex_enabled_check->isChecked();
    config.media_services.plex.auto_discover = m_auto_discover_check->isChecked();

    config.media_services.plex.enable_movies = m_enable_movies_check->isChecked();
    config.media_services.plex.enable_tv_shows = m_enable_tv_shows_check->isChecked();
    config.media_services.plex.enable_music = m_enable_music_check->isChecked();

    config.media_services.plex.server_urls.clear();
    QString urls_text = m_server_urls_edit->toPlainText();
    if (!urls_text.isEmpty()) {
        QStringList urls = urls_text.split('\n', Qt::SkipEmptyParts);
        for (const auto& url : urls) {
            QString trimmed = url.trimmed();
            if (!trimmed.isEmpty()) {
                config.media_services.plex.server_urls.push_back(trimmed.toStdString());
            }
        }
    }

    config.tmdb_access_token = m_tmdb_token_edit->text().toStdString();
    config.enable_tmdb = m_enable_tmdb_check->isChecked();
    config.enable_jikan = m_enable_jikan_check->isChecked();

    return config;
}

void QtSettingsDialog::create_general_tab() {}
void QtSettingsDialog::create_discord_tab() {}
void QtSettingsDialog::create_plex_tab() {}
void QtSettingsDialog::create_services_tab() {}
void QtSettingsDialog::create_format_tab() {}

void QtSettingsDialog::on_accept() {
    validate_inputs();
    accept();
}

void QtSettingsDialog::on_reject() {
    reject();
}

void QtSettingsDialog::on_reset_defaults() {
    auto result = QMessageBox::question(
        this,
        "Reset to Defaults",
        "Are you sure you want to reset all settings to their default values?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (result == QMessageBox::Yes) {
        core::ApplicationConfig default_config;
        load_config(default_config);
    }
}

void QtSettingsDialog::validate_inputs() {
    QString client_id = m_discord_client_id_edit->text().trimmed();
    if (client_id.isEmpty()) {
        LOG_WARNING("SettingsDialog", "Discord client ID is empty, using default");
        m_discord_client_id_edit->setText("1359742002618564618");
    }
}

void QtSettingsDialog::open_logs_folder() {
    std::filesystem::path log_dir;

#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) {
        log_dir = std::filesystem::path(appdata) / "Presence For Plex";
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        log_dir = std::filesystem::path(xdg) / "presence-for-plex";
    } else if (const char* home = std::getenv("HOME")) {
        log_dir = std::filesystem::path(home) / ".config" / "presence-for-plex";
    }
#endif

    if (log_dir.empty()) {
        QMessageBox::warning(this, "Error", "Could not determine logs folder location.");
        return;
    }

    // Convert to QString and open with file explorer
    QString dir_path = QString::fromStdString(log_dir.string());
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir_path))) {
        QMessageBox::warning(this, "Error", "Could not open logs folder: " + dir_path);
    }
}

} // namespace presence_for_plex::platform::qt
