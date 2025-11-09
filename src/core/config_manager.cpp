#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/core/event_bus.hpp"
#include "presence_for_plex/core/events.hpp"
#include "presence_for_plex/utils/yaml_config.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <shared_mutex>
#include <fstream>

namespace presence_for_plex {
namespace core {

// Configuration manager implementation

class ConfigManager::Impl {
public:
    explicit Impl(const std::filesystem::path& config_path)
        : m_config_path(config_path.empty() ? get_default_config_path() : config_path) {
        LOG_DEBUG("ConfigService", "Initializing with path: " + m_config_path.string());
        ensure_config_directory();
        m_config_exists = std::filesystem::exists(m_config_path);
    }

    std::expected<void, ConfigError> load() {
        LOG_DEBUG("ConfigService", "Loading configuration");

        if (!std::filesystem::exists(m_config_path)) {
            LOG_INFO("ConfigService", "Using default configuration");
            std::unique_lock lock(m_mutex);
            m_config = ApplicationConfig{};
            lock.unlock();
            return save();
        }

        auto result = utils::YamlConfigHelper::load_from_file(m_config_path);
        if (result) {
            std::unique_lock lock(m_mutex);
            m_config = *result;
            LOG_DEBUG("ConfigService", "Configuration loaded");
            return {};
        }
        return std::unexpected(result.error());
    }

    std::expected<void, ConfigError> save() {
        ApplicationConfig config_copy;
        {
            std::shared_lock lock(m_mutex);
            config_copy = m_config;
        }

        LOG_DEBUG("ConfigService", "Saving configuration");

        auto result = utils::YamlConfigHelper::save_to_file(config_copy, m_config_path);

        if (result && !m_config_exists) {
            m_config_exists = true;
            (void)add_documentation_comments();
        }

        return result;
    }

    std::expected<void, ConfigError> add_documentation_comments() const {
        try {
            std::ifstream in_file(m_config_path);
            if (!in_file) {
                return std::unexpected(ConfigError::FileNotFound);
            }

            std::stringstream content;
            content << "# Presence for Plex Configuration\n";
            content << "# This file was automatically generated on first run\n";
            content << "# Edit values below to customize your experience\n\n";
            content << "# General Settings\n";
            content << "# log_level: Options are debug, info, warning, error, critical\n";
            content << "# start_at_boot: Start application at system boot\n\n";
            content << "# Discord Rich Presence Settings\n";
            content << "# client_id: Discord application ID for rich presence\n";
            content << "# show_buttons: Display action buttons in Discord\n";
            content << "# show_progress: Show media playback progress\n";
            content << "# show_artwork: Show movie/TV artwork as Discord image\n";
            content << "# update_interval: Seconds between presence updates (1-300)\n";
            content << "# details_format: Custom format for details line (use tokens like {title})\n";
            content << "# state_format: Custom format for state line (use tokens like {state})\n\n";
            content << "# Media Services Configuration\n";
            content << "# Configure each media service separately under media_services:\n";
            content << "#\n";
            content << "# Plex Media Server (media_services.plex)\n";
            content << "# enabled: Enable/disable Plex media service\n";
            content << "# auto_discover: Automatically find local Plex servers\n";
            content << "# poll_interval: Seconds between server status checks (1-60)\n";
            content << "# timeout: Connection timeout in seconds\n";
            content << "# server_urls: Manual server URLs (optional)\n";
            content << "# enable_movies: Show presence for movies (default: true)\n";
            content << "# enable_tv_shows: Show presence for TV shows (default: true)\n";
            content << "# enable_music: Show presence for music (default: true)\n";
            content << "#\n";
            content << "# Future services (Jellyfin, Emby, etc.) will be added here\n\n";
            content << "# External Services\n";
            content << "# tmdb.access_token: TMDB API key for enhanced metadata\n";
            content << "# tmdb.enabled: Enable/disable TMDB integration\n";
            content << "# jikan.enabled: Enable/disable Jikan/MyAnimeList integration\n\n";

            content << in_file.rdbuf();
            in_file.close();

            std::ofstream out_file(m_config_path);
            if (!out_file) {
                return std::unexpected(ConfigError::PermissionDenied);
            }

            out_file << content.str();
            LOG_INFO("ConfigService", "Added documentation to configuration file");
            return {};
        } catch (const std::exception& e) {
            LOG_WARNING("ConfigService", "Could not add documentation: " + std::string(e.what()));
            return {};
        }
    }


    const ApplicationConfig& get() const {
        std::shared_lock lock(m_mutex);
        return m_config;
    }


    std::expected<void, ConfigError> update(const ApplicationConfig& config) {
        LOG_INFO("ConfigService", "Updating configuration");

        ApplicationConfig old_config;
        {
            std::unique_lock lock(m_mutex);
            old_config = m_config;
            m_config = config;
        }

        // Save and publish event outside of lock
        auto result = save();
        if (result && m_event_bus) {
            m_event_bus->publish(core::events::ConfigurationUpdated{
                std::move(old_config), config
            });
        }
        return result;
    }


    void set_event_bus(std::shared_ptr<EventBus> bus) {
        LOG_DEBUG("ConfigService", "Setting event bus");
        m_event_bus = std::move(bus);
    }

private:
    static std::filesystem::path get_default_config_path() {
        std::filesystem::path config_dir;

#ifdef _WIN32
        // Windows: %APPDATA%/Presence For Plex
        if (const char* app_data = std::getenv("APPDATA")) {
            config_dir = std::filesystem::path(app_data) / "Presence For Plex";
        }
#else
        // Unix/Linux/macOS: $XDG_CONFIG_HOME/presence-for-plex or ~/.config/presence-for-plex
        if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
            config_dir = std::filesystem::path(xdg_config) / "presence-for-plex";
        } else if (const char* home = std::getenv("HOME")) {
            config_dir = std::filesystem::path(home) / ".config" / "presence-for-plex";
        }
#endif

        return config_dir / "config.yaml";
    }

    void ensure_config_directory() {
        auto dir = m_config_path.parent_path();
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
            LOG_DEBUG("ConfigService", "Created directory: " + dir.string());
        }
    }

private:
    mutable std::shared_mutex m_mutex;
    std::filesystem::path m_config_path;
    ApplicationConfig m_config;
    std::shared_ptr<EventBus> m_event_bus;
    mutable bool m_config_exists = false;
};

// ConfigManager implementation

ConfigManager::ConfigManager(const std::filesystem::path& config_path)
    : m_impl(std::make_unique<Impl>(config_path)) {}

ConfigManager::~ConfigManager() = default;

std::expected<void, ConfigError> ConfigManager::load() {
    return m_impl->load();
}

std::expected<void, ConfigError> ConfigManager::save() {
    return m_impl->save();
}

const ApplicationConfig& ConfigManager::get() const {
    return m_impl->get();
}

std::expected<void, ConfigError> ConfigManager::update(const ApplicationConfig& config) {
    return m_impl->update(config);
}

void ConfigManager::set_event_bus(std::shared_ptr<EventBus> bus) {
    m_impl->set_event_bus(std::move(bus));
}

} // namespace core
} // namespace presence_for_plex
