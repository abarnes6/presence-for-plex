#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/utils/yaml_config.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <shared_mutex>

namespace presence_for_plex {
namespace core {

// Simplified configuration service implementation

class ConfigurationServiceImpl : public ConfigurationService {
public:
    explicit ConfigurationServiceImpl(const std::filesystem::path& config_path)
        : m_config_path(config_path.empty() ? get_default_config_path() : config_path) {
        PLEX_LOG_INFO("ConfigService", "Initializing with path: " + m_config_path.string());
        ensure_config_directory();
    }

    std::expected<void, ConfigError> load() override {
        std::unique_lock lock(m_mutex);
        PLEX_LOG_INFO("ConfigService", "Loading configuration");

        if (!std::filesystem::exists(m_config_path)) {
            PLEX_LOG_INFO("ConfigService", "Using default configuration");
            m_config = ApplicationConfig{};
            return save_internal();
        }

        auto result = utils::YamlConfigHelper::load_from_file(m_config_path);
        if (result) {
            m_config = *result;
            PLEX_LOG_INFO("ConfigService", "Configuration loaded");
            return {};
        }
        return std::unexpected(result.error());
    }

    std::expected<void, ConfigError> save() override {
        std::shared_lock lock(m_mutex);
        return save_internal();
    }

    std::expected<void, ConfigError> save_internal() const {
        PLEX_LOG_DEBUG("ConfigService", "Saving configuration");
        return utils::YamlConfigHelper::save_to_file(m_config, m_config_path);
    }

    std::expected<void, ConfigError> reload() override {
        PLEX_LOG_INFO("ConfigService", "Reloading configuration");
        ApplicationConfig old_config = m_config;
        auto result = load();
        if (result && m_event_bus) {
            m_event_bus->publish(core::events::ConfigurationUpdated{
                std::move(old_config), m_config
            });
        }
        return result;
    }

    const ApplicationConfig& get() const override {
        std::shared_lock lock(m_mutex);
        return m_config;
    }


    std::expected<void, ConfigError> update(const ApplicationConfig& config) override {
        ApplicationConfig old_config;
        {
            std::unique_lock lock(m_mutex);
            PLEX_LOG_INFO("ConfigService", "Updating configuration");
            old_config = m_config;
            m_config = config;
        }

        auto result = save();
        if (result && m_event_bus) {
            m_event_bus->publish(core::events::ConfigurationUpdated{
                std::move(old_config), config
            });
        }
        return result;
    }

    std::expected<void, ConfigError> validate(const ApplicationConfig& config) const override {
        PLEX_LOG_DEBUG("ConfigService", "Validating configuration");
        return config.is_valid() ?
            std::expected<void, ConfigError>{} :
            std::unexpected(ConfigError::ValidationError);
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
