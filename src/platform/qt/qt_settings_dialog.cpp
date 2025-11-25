#include "presence_for_plex/platform/qt/qt_settings_dialog.hpp"
#include "presence_for_plex/platform/system_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/format_utils.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QGroupBox>
#include <QFrame>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFont>
#include <QDesktopServices>
#include <QUrl>
#include <QEvent>
#include <QToolTip>
#include <QCursor>
#include <QListWidget>
#include <QScrollBar>
#include <filesystem>
#include <string>

namespace {

// Sample media data for preview
const presence_for_plex::core::MediaInfo g_tv_sample = []() {
    presence_for_plex::core::MediaInfo media;
    media.type = presence_for_plex::core::MediaType::TVShow;
    media.grandparent_title = "Breaking Bad";
    media.season = 5;
    media.episode = 14;
    media.title = "Ozymandias";
    media.year = 2013;
    media.genres = {"Crime", "Drama", "Thriller"};
    media.rating = 9.9;
    media.username = "JohnDoe";
    return media;
}();

const presence_for_plex::core::MediaInfo g_movie_sample = []() {
    presence_for_plex::core::MediaInfo media;
    media.type = presence_for_plex::core::MediaType::Movie;
    media.title = "The Shawshank Redemption";
    media.original_title = "The Shawshank Redemption";
    media.year = 1994;
    media.studio = "Castle Rock Entertainment";
    media.genres = {"Drama"};
    media.rating = 9.3;
    media.username = "JohnDoe";
    media.summary = "Two imprisoned men bond over a number of years...";
    return media;
}();

const presence_for_plex::core::MediaInfo g_music_sample = []() {
    presence_for_plex::core::MediaInfo media;
    media.type = presence_for_plex::core::MediaType::Music;
    media.title = "Bohemian Rhapsody";
    media.artist = "Queen";
    media.album = "A Night at the Opera";
    media.track = 11;
    media.year = 1975;
    media.genres = {"Rock", "Progressive Rock"};
    media.username = "JohnDoe";
    return media;
}();

} // anonymous namespace

namespace presence_for_plex::platform::qt {

QtSettingsDialog::QtSettingsDialog(const core::ApplicationConfig& config, QWidget* parent)
    : QDialog(parent), m_original_config(config) {
    setWindowTitle("Settings - Presence for Plex");
    setMinimumSize(600, 400);
    resize(800, 500);
    setup_ui();
    load_config(config);
}

void QtSettingsDialog::setup_ui() {
    auto* main_layout = new QVBoxLayout(this);

    // Create horizontal layout for sidebar and content
    auto* content_wrapper = new QHBoxLayout();

    // Create sidebar
    auto* sidebar = new QListWidget();
    sidebar->setFixedWidth(150);
    sidebar->addItem("General");
    sidebar->addItem("Discord");
    sidebar->addItem("Plex");
    sidebar->addItem("Media Filters");
    sidebar->addItem("TMDB");
    sidebar->addItem("Jikan");
    sidebar->addItem("TV Format");
    sidebar->addItem("Movie Format");
    sidebar->addItem("Music Format");
    sidebar->addItem("Preview");
    connect(sidebar, &QListWidget::currentRowChanged, this, &QtSettingsDialog::scroll_to_section);
    content_wrapper->addWidget(sidebar);

    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFrameShape(QFrame::NoFrame);

    auto* scroll_content = new QWidget();
    auto* content_layout = new QVBoxLayout(scroll_content);
    content_layout->setContentsMargins(0, 0, 0, 0);

    // General Settings
    auto* general_group = new QGroupBox("General");
    auto* general_form = new QFormLayout(general_group);

    m_log_level_combo = new QComboBox();
    m_log_level_combo->addItems({"Debug", "Info", "Warning", "Error"});
    general_form->addRow("Log Level:", m_log_level_combo);

    m_start_at_boot_check = new QCheckBox("Start at boot");
    general_form->addRow(m_start_at_boot_check);

    auto* logs_button = new QPushButton("Open Logs Folder");
    connect(logs_button, &QPushButton::clicked, this, &QtSettingsDialog::open_logs_folder);
    general_form->addRow("Logs:", logs_button);

    content_layout->addWidget(general_group);
    m_sections.push_back(general_group);

    // Discord Rich Presence
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

    content_layout->addWidget(discord_group);
    m_sections.push_back(discord_group);

    // Plex Media Server
    auto* plex_group = new QGroupBox("Plex Media Server");
    auto* plex_form = new QFormLayout(plex_group);

    m_plex_enabled_check = new QCheckBox("Enable Plex");
    plex_form->addRow(m_plex_enabled_check);

    m_auto_discover_check = new QCheckBox("Auto-discover local servers");
    plex_form->addRow(m_auto_discover_check);

    m_server_urls_edit = new QTextEdit();
    m_server_urls_edit->setPlaceholderText("One URL per line\nExample: http://192.168.1.100:32400");
    m_server_urls_edit->setMaximumHeight(80);
    plex_form->addRow("Manual Server URLs:", m_server_urls_edit);

    content_layout->addWidget(plex_group);
    m_sections.push_back(plex_group);

    // Media Type Filters
    auto* filter_group = new QGroupBox("Media Type Filters");
    auto* filter_form = new QFormLayout(filter_group);

    m_enable_movies_check = new QCheckBox("Show Movies");
    filter_form->addRow(m_enable_movies_check);

    m_enable_tv_shows_check = new QCheckBox("Show TV Shows");
    filter_form->addRow(m_enable_tv_shows_check);

    m_enable_music_check = new QCheckBox("Show Music");
    filter_form->addRow(m_enable_music_check);

    content_layout->addWidget(filter_group);
    m_sections.push_back(filter_group);

    // External Services
    auto* tmdb_group = new QGroupBox("The Movie Database (TMDB)");
    auto* tmdb_form = new QFormLayout(tmdb_group);

    m_enable_tmdb_check = new QCheckBox("Enable TMDB integration");
    tmdb_form->addRow(m_enable_tmdb_check);

    m_tmdb_token_edit = new QLineEdit();
    m_tmdb_token_edit->setPlaceholderText("Enter TMDB API Access Token");
    tmdb_form->addRow("API Token:", m_tmdb_token_edit);

    auto* tmdb_help = new QLabel("Provides enhanced metadata and artwork for movies and TV shows.");
    tmdb_help->setWordWrap(true);
    QFont tmdb_font = tmdb_help->font();
    tmdb_font.setPointSize(tmdb_font.pointSize() - 1);
    tmdb_help->setFont(tmdb_font);
    tmdb_help->setStyleSheet("color: gray;");
    tmdb_form->addRow("", tmdb_help);

    content_layout->addWidget(tmdb_group);
    m_sections.push_back(tmdb_group);

    auto* jikan_group = new QGroupBox("Jikan API (MyAnimeList)");
    auto* jikan_form = new QFormLayout(jikan_group);

    m_enable_jikan_check = new QCheckBox("Enable Jikan/MyAnimeList integration");
    jikan_form->addRow(m_enable_jikan_check);

    auto* jikan_help = new QLabel("Provides anime metadata from MyAnimeList. No API key required.");
    jikan_help->setWordWrap(true);
    QFont jikan_font = jikan_help->font();
    jikan_font.setPointSize(jikan_font.pointSize() - 1);
    jikan_help->setFont(jikan_font);
    jikan_help->setStyleSheet("color: gray;");
    jikan_form->addRow("", jikan_help);

    content_layout->addWidget(jikan_group);
    m_sections.push_back(jikan_group);

    // Rich Presence Format - TV Shows
    auto* tv_format_group = new QGroupBox("TV Shows Format");
    auto* tv_format_form = new QFormLayout(tv_format_group);

    m_tv_details_format_edit = new QLineEdit();
    m_tv_details_format_edit->setPlaceholderText("{show}");
    tv_format_form->addRow("Details:", m_tv_details_format_edit);

    m_tv_state_format_edit = new QLineEdit();
    m_tv_state_format_edit->setPlaceholderText("{se} - {title}");
    tv_format_form->addRow("State:", m_tv_state_format_edit);

    m_tv_large_image_text_format_edit = new QLineEdit();
    m_tv_large_image_text_format_edit->setPlaceholderText("{title}");
    tv_format_form->addRow("Hover Text:", m_tv_large_image_text_format_edit);

    content_layout->addWidget(tv_format_group);
    m_sections.push_back(tv_format_group);

    // Rich Presence Format - Movies
    auto* movie_format_group = new QGroupBox("Movies Format");
    auto* movie_format_form = new QFormLayout(movie_format_group);

    m_movie_details_format_edit = new QLineEdit();
    m_movie_details_format_edit->setPlaceholderText("{title} ({year})");
    movie_format_form->addRow("Details:", m_movie_details_format_edit);

    m_movie_state_format_edit = new QLineEdit();
    m_movie_state_format_edit->setPlaceholderText("{genres}");
    movie_format_form->addRow("State:", m_movie_state_format_edit);

    m_movie_large_image_text_format_edit = new QLineEdit();
    m_movie_large_image_text_format_edit->setPlaceholderText("{title}");
    movie_format_form->addRow("Hover Text:", m_movie_large_image_text_format_edit);

    content_layout->addWidget(movie_format_group);
    m_sections.push_back(movie_format_group);

    // Rich Presence Format - Music
    auto* music_format_group = new QGroupBox("Music Format");
    auto* music_format_form = new QFormLayout(music_format_group);

    m_music_details_format_edit = new QLineEdit();
    m_music_details_format_edit->setPlaceholderText("{title}");
    music_format_form->addRow("Details:", m_music_details_format_edit);

    m_music_state_format_edit = new QLineEdit();
    m_music_state_format_edit->setPlaceholderText("{artist} - {album}");
    music_format_form->addRow("State:", m_music_state_format_edit);

    m_music_large_image_text_format_edit = new QLineEdit();
    m_music_large_image_text_format_edit->setPlaceholderText("{title}");
    music_format_form->addRow("Hover Text:", m_music_large_image_text_format_edit);

    content_layout->addWidget(music_format_group);
    m_sections.push_back(music_format_group);

    // Preview Section
    auto* preview_group = new QGroupBox("Preview");
    auto* preview_layout = new QVBoxLayout(preview_group);
    preview_layout->setSpacing(4);
    preview_layout->setContentsMargins(6, 6, 6, 6);

    // TV Show Preview
    auto* tv_preview_card = new QFrame();
    tv_preview_card->setFrameShape(QFrame::NoFrame);
    auto* tv_preview_layout = new QHBoxLayout(tv_preview_card);
    tv_preview_layout->setSpacing(6);
    tv_preview_layout->setContentsMargins(0, 0, 0, 0);
    tv_preview_layout->setAlignment(Qt::AlignTop);

    m_tv_preview_image = new QLabel();
    m_tv_preview_image->setFixedSize(40, 40);
    m_tv_preview_image->setAlignment(Qt::AlignCenter);
    m_tv_preview_image->setText("📺");
    QFont tv_icon_font = m_tv_preview_image->font();
    tv_icon_font.setPointSize(16);
    m_tv_preview_image->setFont(tv_icon_font);
    tv_preview_layout->addWidget(m_tv_preview_image, 0, Qt::AlignTop);

    auto* tv_text_container = new QWidget();
    tv_text_container->setFixedHeight(40);
    auto* tv_text_layout = new QVBoxLayout(tv_text_container);
    tv_text_layout->setSpacing(0);
    tv_text_layout->setContentsMargins(0, 0, 0, 0);

    m_tv_preview_details = new QLabel();
    m_tv_preview_details->setStyleSheet("color: #FFFFFF; font-weight: bold; font-size: 11px; padding: 0; margin: 0;");
    m_tv_preview_details->setContentsMargins(0, 0, 0, 0);
    m_tv_preview_details->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tv_text_layout->addWidget(m_tv_preview_details);

    m_tv_preview_state = new QLabel();
    m_tv_preview_state->setStyleSheet("color: #B9BBBE; font-size: 10px; padding: 0; margin: 0;");
    m_tv_preview_state->setContentsMargins(0, 0, 0, 0);
    m_tv_preview_state->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tv_text_layout->addWidget(m_tv_preview_state);

    m_tv_preview_image_text = new QLabel();
    m_tv_preview_image_text->setStyleSheet("color: #72767D; font-size: 9px; padding: 0; margin: 0;");
    m_tv_preview_image_text->setContentsMargins(0, 0, 0, 0);
    m_tv_preview_image_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tv_text_layout->addWidget(m_tv_preview_image_text);

    tv_text_layout->addStretch();
    tv_preview_layout->addWidget(tv_text_container);
    tv_preview_layout->addStretch();

    preview_layout->addWidget(tv_preview_card);

    // Movie Preview
    auto* movie_preview_card = new QFrame();
    movie_preview_card->setFrameShape(QFrame::NoFrame);
    auto* movie_preview_layout = new QHBoxLayout(movie_preview_card);
    movie_preview_layout->setSpacing(6);
    movie_preview_layout->setContentsMargins(0, 0, 0, 0);
    movie_preview_layout->setAlignment(Qt::AlignTop);

    m_movie_preview_image = new QLabel();
    m_movie_preview_image->setFixedSize(40, 40);
    m_movie_preview_image->setAlignment(Qt::AlignCenter);
    m_movie_preview_image->setText("🎬");
    QFont movie_icon_font = m_movie_preview_image->font();
    movie_icon_font.setPointSize(16);
    m_movie_preview_image->setFont(movie_icon_font);
    movie_preview_layout->addWidget(m_movie_preview_image, 0, Qt::AlignTop);

    auto* movie_text_container = new QWidget();
    movie_text_container->setFixedHeight(40);
    auto* movie_text_layout = new QVBoxLayout(movie_text_container);
    movie_text_layout->setSpacing(0);
    movie_text_layout->setContentsMargins(0, 0, 0, 0);

    m_movie_preview_details = new QLabel();
    m_movie_preview_details->setStyleSheet("color: #FFFFFF; font-weight: bold; font-size: 11px; padding: 0; margin: 0;");
    m_movie_preview_details->setContentsMargins(0, 0, 0, 0);
    m_movie_preview_details->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    movie_text_layout->addWidget(m_movie_preview_details);

    m_movie_preview_state = new QLabel();
    m_movie_preview_state->setStyleSheet("color: #B9BBBE; font-size: 10px; padding: 0; margin: 0;");
    m_movie_preview_state->setContentsMargins(0, 0, 0, 0);
    m_movie_preview_state->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    movie_text_layout->addWidget(m_movie_preview_state);

    m_movie_preview_image_text = new QLabel();
    m_movie_preview_image_text->setStyleSheet("color: #72767D; font-size: 9px; padding: 0; margin: 0;");
    m_movie_preview_image_text->setContentsMargins(0, 0, 0, 0);
    m_movie_preview_image_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    movie_text_layout->addWidget(m_movie_preview_image_text);

    movie_text_layout->addStretch();
    movie_preview_layout->addWidget(movie_text_container);
    movie_preview_layout->addStretch();

    preview_layout->addWidget(movie_preview_card);

    // Music Preview
    auto* music_preview_card = new QFrame();
    music_preview_card->setFrameShape(QFrame::NoFrame);
    auto* music_preview_layout = new QHBoxLayout(music_preview_card);
    music_preview_layout->setSpacing(6);
    music_preview_layout->setContentsMargins(0, 0, 0, 0);
    music_preview_layout->setAlignment(Qt::AlignTop);

    m_music_preview_image = new QLabel();
    m_music_preview_image->setFixedSize(40, 40);
    m_music_preview_image->setAlignment(Qt::AlignCenter);
    m_music_preview_image->setText("🎵");
    QFont music_icon_font = m_music_preview_image->font();
    music_icon_font.setPointSize(16);
    m_music_preview_image->setFont(music_icon_font);
    music_preview_layout->addWidget(m_music_preview_image, 0, Qt::AlignTop);

    auto* music_text_container = new QWidget();
    music_text_container->setFixedHeight(40);
    auto* music_text_layout = new QVBoxLayout(music_text_container);
    music_text_layout->setSpacing(0);
    music_text_layout->setContentsMargins(0, 0, 0, 0);

    m_music_preview_details = new QLabel();
    m_music_preview_details->setStyleSheet("color: #FFFFFF; font-weight: bold; font-size: 11px; padding: 0; margin: 0;");
    m_music_preview_details->setContentsMargins(0, 0, 0, 0);
    m_music_preview_details->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    music_text_layout->addWidget(m_music_preview_details);

    m_music_preview_state = new QLabel();
    m_music_preview_state->setStyleSheet("color: #B9BBBE; font-size: 10px; padding: 0; margin: 0;");
    m_music_preview_state->setContentsMargins(0, 0, 0, 0);
    m_music_preview_state->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    music_text_layout->addWidget(m_music_preview_state);

    m_music_preview_image_text = new QLabel();
    m_music_preview_image_text->setStyleSheet("color: #72767D; font-size: 9px; padding: 0; margin: 0;");
    m_music_preview_image_text->setContentsMargins(0, 0, 0, 0);
    m_music_preview_image_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    music_text_layout->addWidget(m_music_preview_image_text);

    music_text_layout->addStretch();
    music_preview_layout->addWidget(music_text_container);
    music_preview_layout->addStretch();

    preview_layout->addWidget(music_preview_card);

    content_layout->addWidget(preview_group);
    m_sections.push_back(preview_group);

    // Format Help
    auto* help_label = new QLabel("Format Placeholders");
    QFont help_font = help_label->font();
    help_font.setBold(true);
    help_label->setFont(help_font);
    content_layout->addWidget(help_label);

    m_format_help_text = new QTextEdit();
    m_format_help_text->setReadOnly(true);
    m_format_help_text->setFrameShape(QFrame::NoFrame);
    m_format_help_text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_format_help_text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_format_help_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_format_help_text->setPlainText(
        "Basic: {title} {original_title} {year} {studio} {type} {summary}\n"
        "TV Shows: {show} {season} {episode} {season_padded} {episode_padded} {se} {SxE}\n"
        "Music: {artist} {album} {track}\n"
        "Playback: {state} {progress} {duration} {remaining} {progress_percentage}\n"
        "Other: {username} {genre} {genres} {rating}"
    );

    QFontMetrics fm(m_format_help_text->font());
    int line_height = fm.lineSpacing();
    int num_lines = 5;
    m_format_help_text->setFixedHeight(line_height * num_lines + 10);

    content_layout->addWidget(m_format_help_text);

    scroll_content->setLayout(content_layout);
    m_scroll_area->setWidget(scroll_content);

    content_wrapper->addWidget(m_scroll_area);
    main_layout->addLayout(content_wrapper);

    // Connect format fields to update preview in real-time
    connect(m_tv_details_format_edit, &QLineEdit::textChanged, this, &QtSettingsDialog::update_preview);
    connect(m_tv_state_format_edit, &QLineEdit::textChanged, this, &QtSettingsDialog::update_preview);
    connect(m_tv_large_image_text_format_edit, &QLineEdit::textChanged, this, &QtSettingsDialog::update_preview);
    connect(m_movie_details_format_edit, &QLineEdit::textChanged, this, &QtSettingsDialog::update_preview);
    connect(m_movie_state_format_edit, &QLineEdit::textChanged, this, &QtSettingsDialog::update_preview);
    connect(m_movie_large_image_text_format_edit, &QLineEdit::textChanged, this, &QtSettingsDialog::update_preview);
    connect(m_music_details_format_edit, &QLineEdit::textChanged, this, &QtSettingsDialog::update_preview);
    connect(m_music_state_format_edit, &QLineEdit::textChanged, this, &QtSettingsDialog::update_preview);
    connect(m_music_large_image_text_format_edit, &QLineEdit::textChanged, this, &QtSettingsDialog::update_preview);

    // Buttons
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

    m_discord_enabled_check->setChecked(config.discord.enabled);
    m_discord_client_id_edit->setText(QString::fromStdString(config.discord.discord.client_id));
    m_show_buttons_check->setChecked(config.discord.discord.show_buttons);
    m_show_progress_check->setChecked(config.discord.discord.show_progress);
    m_show_artwork_check->setChecked(config.discord.discord.show_artwork);

    // TV Shows format
    m_tv_details_format_edit->setText(QString::fromStdString(config.discord.discord.tv_details_format));
    m_tv_state_format_edit->setText(QString::fromStdString(config.discord.discord.tv_state_format));
    m_tv_large_image_text_format_edit->setText(QString::fromStdString(config.discord.discord.tv_large_image_text_format));

    // Movies format
    m_movie_details_format_edit->setText(QString::fromStdString(config.discord.discord.movie_details_format));
    m_movie_state_format_edit->setText(QString::fromStdString(config.discord.discord.movie_state_format));
    m_movie_large_image_text_format_edit->setText(QString::fromStdString(config.discord.discord.movie_large_image_text_format));

    // Music format
    m_music_details_format_edit->setText(QString::fromStdString(config.discord.discord.music_details_format));
    m_music_state_format_edit->setText(QString::fromStdString(config.discord.discord.music_state_format));
    m_music_large_image_text_format_edit->setText(QString::fromStdString(config.discord.discord.music_large_image_text_format));

    m_plex_enabled_check->setChecked(config.plex.enabled);
    m_auto_discover_check->setChecked(config.plex.auto_discover);

    m_enable_movies_check->setChecked(config.plex.enable_movies);
    m_enable_tv_shows_check->setChecked(config.plex.enable_tv_shows);
    m_enable_music_check->setChecked(config.plex.enable_music);

    if (!config.plex.server_urls.empty()) {
        QString urls;
        for (const auto& url : config.plex.server_urls) {
            urls += QString::fromStdString(url) + "\n";
        }
        m_server_urls_edit->setPlainText(urls.trimmed());
    }

    m_tmdb_token_edit->setText(QString::fromStdString(config.tmdb_access_token));
    m_enable_tmdb_check->setChecked(config.enable_tmdb);
    m_enable_jikan_check->setChecked(config.enable_jikan);

    // Update preview with loaded config
    update_preview();
}

core::ApplicationConfig QtSettingsDialog::get_config() const {
    core::ApplicationConfig config = m_original_config;

    config.log_level = static_cast<utils::LogLevel>(m_log_level_combo->currentIndex());
    config.start_at_boot = m_start_at_boot_check->isChecked();

    config.discord.enabled = m_discord_enabled_check->isChecked();
    config.discord.discord.client_id = m_discord_client_id_edit->text().toStdString();
    config.discord.discord.show_buttons = m_show_buttons_check->isChecked();
    config.discord.discord.show_progress = m_show_progress_check->isChecked();
    config.discord.discord.show_artwork = m_show_artwork_check->isChecked();
    // TV Shows format
    config.discord.discord.tv_details_format = m_tv_details_format_edit->text().toStdString();
    config.discord.discord.tv_state_format = m_tv_state_format_edit->text().toStdString();
    config.discord.discord.tv_large_image_text_format = m_tv_large_image_text_format_edit->text().toStdString();

    // Movies format
    config.discord.discord.movie_details_format = m_movie_details_format_edit->text().toStdString();
    config.discord.discord.movie_state_format = m_movie_state_format_edit->text().toStdString();
    config.discord.discord.movie_large_image_text_format = m_movie_large_image_text_format_edit->text().toStdString();

    // Music format
    config.discord.discord.music_details_format = m_music_details_format_edit->text().toStdString();
    config.discord.discord.music_state_format = m_music_state_format_edit->text().toStdString();
    config.discord.discord.music_large_image_text_format = m_music_large_image_text_format_edit->text().toStdString();

    config.plex.enabled = m_plex_enabled_check->isChecked();
    config.plex.auto_discover = m_auto_discover_check->isChecked();

    config.plex.enable_movies = m_enable_movies_check->isChecked();
    config.plex.enable_tv_shows = m_enable_tv_shows_check->isChecked();
    config.plex.enable_music = m_enable_music_check->isChecked();

    config.plex.server_urls.clear();
    QString urls_text = m_server_urls_edit->toPlainText();
    if (!urls_text.isEmpty()) {
        QStringList urls = urls_text.split('\n', Qt::SkipEmptyParts);
        for (const auto& url : urls) {
            QString trimmed = url.trimmed();
            if (!trimmed.isEmpty()) {
                config.plex.server_urls.push_back(trimmed.toStdString());
            }
        }
    }

    config.tmdb_access_token = m_tmdb_token_edit->text().toStdString();
    config.enable_tmdb = m_enable_tmdb_check->isChecked();
    config.enable_jikan = m_enable_jikan_check->isChecked();

    return config;
}

void QtSettingsDialog::update_preview() {
    // Update TV preview
    QString tv_details = m_tv_details_format_edit->text();
    if (tv_details.isEmpty()) tv_details = "{show}";
    m_tv_preview_details->setText(QString::fromStdString(utils::replace_placeholders(tv_details.toStdString(), g_tv_sample)));

    QString tv_state = m_tv_state_format_edit->text();
    if (tv_state.isEmpty()) tv_state = "{se} - {title}";
    m_tv_preview_state->setText(QString::fromStdString(utils::replace_placeholders(tv_state.toStdString(), g_tv_sample)));

    QString tv_image_text = m_tv_large_image_text_format_edit->text();
    if (tv_image_text.isEmpty()) tv_image_text = "{title}";
    m_tv_preview_image_text->setText(QString::fromStdString(utils::replace_placeholders(tv_image_text.toStdString(), g_tv_sample)));

    // Update Movie preview
    QString movie_details = m_movie_details_format_edit->text();
    if (movie_details.isEmpty()) movie_details = "{title} ({year})";
    m_movie_preview_details->setText(QString::fromStdString(utils::replace_placeholders(movie_details.toStdString(), g_movie_sample)));

    QString movie_state = m_movie_state_format_edit->text();
    if (movie_state.isEmpty()) movie_state = "{genres}";
    m_movie_preview_state->setText(QString::fromStdString(utils::replace_placeholders(movie_state.toStdString(), g_movie_sample)));

    QString movie_image_text = m_movie_large_image_text_format_edit->text();
    if (movie_image_text.isEmpty()) movie_image_text = "{title}";
    m_movie_preview_image_text->setText(QString::fromStdString(utils::replace_placeholders(movie_image_text.toStdString(), g_movie_sample)));

    // Update Music preview
    QString music_details = m_music_details_format_edit->text();
    if (music_details.isEmpty()) music_details = "{title}";
    m_music_preview_details->setText(QString::fromStdString(utils::replace_placeholders(music_details.toStdString(), g_music_sample)));

    QString music_state = m_music_state_format_edit->text();
    if (music_state.isEmpty()) music_state = "{artist} - {album}";
    m_music_preview_state->setText(QString::fromStdString(utils::replace_placeholders(music_state.toStdString(), g_music_sample)));

    QString music_image_text = m_music_large_image_text_format_edit->text();
    if (music_image_text.isEmpty()) music_image_text = "{title}";
    m_music_preview_image_text->setText(QString::fromStdString(utils::replace_placeholders(music_image_text.toStdString(), g_music_sample)));
}

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

void QtSettingsDialog::scroll_to_section(int index) {
    if (index >= 0 && index < static_cast<int>(m_sections.size())) {
        QWidget* section = m_sections[index];
        int y_position = section->y();
        m_scroll_area->verticalScrollBar()->setValue(y_position);
    }
}

} // namespace presence_for_plex::platform::qt
