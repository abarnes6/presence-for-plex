#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/utils/yaml_config.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <shared_mutex>
#include <fstream>

namespace presence_for_plex {
namespace core {

// Simplified configuration service implementation

class ConfigurationServiceImpl : public ConfigurationService {
public:
    explicit ConfigurationServiceImpl(const std::filesystem::path& config_path)
        : m_config_path(config_path.empty() ? get_default_config_path() : config_path) {
        PLEX_LOG_INFO("ConfigService", "Initializing with path: " + m_config_path.string());
        ensure_config_directory();
        m_config_exists = std::filesystem::exists(m_config_path);
    }

    std::expected<void, ConfigError> load() override {
        PLEX_LOG_INFO("ConfigService", "Loading configuration");

        if (!std::filesystem::exists(m_config_path)) {
            PLEX_LOG_INFO("ConfigService", "Using default configuration");
            std::unique_lock lock(m_mutex);
            m_config = ApplicationConfig{};
            lock.unlock();
            return save();
        }

        auto result = utils::YamlConfigHelper::load_from_file(m_config_path);
        if (result) {
            std::unique_lock lock(m_mutex);
            m_config = *result;
            PLEX_LOG_INFO("ConfigService", "Configuration loaded");
            return {};
        }
        return std::unexpected(result.error());
    }

    std::expected<void, ConfigError> save() override {
        ApplicationConfig config_copy;
        {
            std::shared_lock lock(m_mutex);
            config_copy = m_config;
        }

        PLEX_LOG_DEBUG("ConfigService", "Saving configuration");

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
            content << "# Plex Media Server Settings\n";
            content << "# auto_discover: Automatically find local Plex servers\n";
            content << "# poll_interval: Seconds between server status checks (1-60)\n";
            content << "# timeout: Connection timeout in seconds\n";
            content << "# server_urls: Manual server URLs (optional)\n\n";
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
            PLEX_LOG_INFO("ConfigService", "Added documentation to configuration file");
            return {};
        } catch (const std::exception& e) {
            PLEX_LOG_WARNING("ConfigService", "Could not add documentation: " + std::string(e.what()));
            return {};
        }
    }


    const ApplicationConfig& get() const override {
        std::shared_lock lock(m_mutex);
        return m_config;
    }


    std::expected<void, ConfigError> update(const ApplicationConfig& config) override {
        PLEX_LOG_INFO("ConfigService", "Updating configuration");

        // Validate configuration first
        if (!config.is_valid()) {
            PLEX_LOG_ERROR("ConfigService", "Invalid configuration provided");
            return std::unexpected(ConfigError::ValidationError);
        }

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


    void set_event_bus(std::shared_ptr<EventBus> bus) override {
        PLEX_LOG_DEBUG("ConfigService", "Setting event bus");
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
            PLEX_LOG_DEBUG("ConfigService", "Created directory: " + dir.string());
        }
    }

private:
    mutable std::shared_mutex m_mutex;
    std::filesystem::path m_config_path;
    ApplicationConfig m_config;
    std::shared_ptr<EventBus> m_event_bus;
    mutable bool m_config_exists = false;
};

std::unique_ptr<ConfigurationService> ConfigurationService::create(
    const std::filesystem::path& config_path,
    std::shared_ptr<EventBus> event_bus) {
    auto service = std::make_unique<ConfigurationServiceImpl>(config_path);
    if (event_bus) {
        service->set_event_bus(event_bus);
    }
    return service;
}

} // namespace core
} // namespace presence_for_plex
