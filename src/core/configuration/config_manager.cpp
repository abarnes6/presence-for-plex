#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/core/models.hpp"
#include "presence_for_plex/utils/uuid.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <shared_mutex>
#include <unordered_map>

namespace presence_for_plex {
namespace core {

// Extension of ApplicationConfig to include runtime data
struct FullConfig : public ApplicationConfig {
    // Plex authentication
    std::string plex_auth_token;
    std::string plex_client_identifier;
    std::string plex_username;

    // TMDB API key
    std::string tmdb_access_token =
        "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiIzNmMxOTI3ZjllMTlkMzUxZWFmMjAxNGViN2JmYjNkZiIsIm5iZiI6MTc0NTQzMTA3NC4yMjcsInN1YiI6IjY4MDkyYTIyNmUxYTc2OWU4MWVmMGJhOSIsInNjb3BlcyI6WyJhcGlfcmVhZCJdLCJ2ZXJzaW9uIjoxfQ.Td6eAbW7SgQOMmQpRDwVM-_3KIMybGRqWNK8Yqw1Zzs";

    // Plex servers (stored separately from basic config)
    std::unordered_map<std::string, std::unique_ptr<PlexServer>> plex_servers;

    // Discord client ID
    uint64_t discord_client_id = 1359742002618564618;
};

class ConfigurationServiceImpl : public ConfigurationService {
public:
    explicit ConfigurationServiceImpl(const std::filesystem::path& config_path)
        : m_config_path(config_path.empty() ? get_default_config_path() : config_path) {
        PLEX_LOG_INFO("ConfigService", "Initializing configuration service with path: " + m_config_path.string());
        ensure_config_directory();
    }

    std::expected<void, ConfigError> load_configuration() override {
        std::unique_lock lock(m_mutex);
        PLEX_LOG_INFO("ConfigService", "Loading configuration from: " + m_config_path.string());

        if (!std::filesystem::exists(m_config_path)) {
            PLEX_LOG_INFO("ConfigService", "Config file does not exist, creating default");
            return save_configuration_internal();
        }

        try {
            YAML::Node loaded = YAML::LoadFile(m_config_path.string());
            load_from_yaml(loaded);
            PLEX_LOG_INFO("ConfigService", "Configuration loaded successfully");
            return {};
        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("ConfigService", "Error loading config: " + std::string(e.what()));
            return std::unexpected<ConfigError>(ConfigError::InvalidFormat);
        }
    }

    std::expected<void, ConfigError> save_configuration() override {
        std::shared_lock lock(m_mutex);
        return save_configuration_internal();
    }

    std::expected<void, ConfigError> save_configuration_internal() {
        PLEX_LOG_INFO("ConfigService", "Saving configuration to: " + m_config_path.string());

        try {
            YAML::Node node = save_to_yaml();

            std::ofstream file(m_config_path);
            if (!file) {
                PLEX_LOG_ERROR("ConfigService", "Failed to open config file for writing");
                return std::unexpected<ConfigError>(ConfigError::PermissionDenied);
            }

            file << node;
            PLEX_LOG_INFO("ConfigService", "Configuration saved successfully");
            return {};
        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("ConfigService", "Error saving config: " + std::string(e.what()));
            return std::unexpected<ConfigError>(ConfigError::InvalidFormat);
        }
    }

    std::expected<void, ConfigError> reload_configuration() override {
        PLEX_LOG_INFO("ConfigService", "Reloading configuration");
        auto result = load_configuration();
        if (result && m_change_callback) {
            m_change_callback(m_full_config);
        }
        return result;
    }

    const ApplicationConfig& get_config() const override {
        std::shared_lock lock(m_mutex);
        return m_full_config;
    }

    // Extended accessor for full config (including runtime data)
    const FullConfig& get_full_config() const {
        std::shared_lock lock(m_mutex);
        return m_full_config;
    }

    std::expected<void, ConfigError> update_config(const ApplicationConfig& config) override {
        std::unique_lock lock(m_mutex);
        PLEX_LOG_INFO("ConfigService", "Updating configuration");

        // Copy application config fields (preserve auth-related fields)
        m_full_config.discord = config.discord;

        // Preserve auth token, username, and client identifier when updating plex config
        std::string preserved_auth_token = m_full_config.plex_auth_token;
        std::string preserved_username = m_full_config.plex_username;
        std::string preserved_client_id = m_full_config.plex_client_identifier;

        m_full_config.plex = config.plex;

        // Restore preserved values
        m_full_config.plex_auth_token = preserved_auth_token;
        m_full_config.plex_username = preserved_username;
        m_full_config.plex_client_identifier = preserved_client_id;

        m_full_config.log_level = config.log_level;
        m_full_config.log_file_path = config.log_file_path;
        m_full_config.start_minimized = config.start_minimized;

        // Save and notify
        auto result = save_configuration_internal();
        if (result && m_change_callback) {
            m_change_callback(m_full_config);
        }
        return result;
    }

    std::expected<void, ConfigError> validate_config(const ApplicationConfig& config) const override {
        PLEX_LOG_DEBUG("ConfigService", "Validating configuration");

        if (!config.is_valid()) {
            return std::unexpected<ConfigError>(ConfigError::ValidationError);
        }

        return {};
    }

    void set_change_callback(ConfigChangeCallback callback) override {
        PLEX_LOG_DEBUG("ConfigService", "Setting configuration change callback");
        m_change_callback = std::move(callback);
    }

    // Plex-specific config methods
    std::string get_plex_auth_token() const override {
        std::shared_lock lock(m_mutex);
        return m_full_config.plex_auth_token;
    }

    void set_plex_auth_token(const std::string& token) override {
        std::unique_lock lock(m_mutex);
        m_full_config.plex_auth_token = token;
        (void)save_configuration_internal();
    }

    std::string get_plex_client_identifier() const override {
        std::shared_lock lock(m_mutex);
        if (m_full_config.plex_client_identifier.empty()) {
            lock.unlock();
            // Generate new UUID if not set
            const_cast<ConfigurationServiceImpl*>(this)->generate_client_identifier();
            lock.lock();
        }
        return m_full_config.plex_client_identifier;
    }

    std::string get_plex_username() const override {
        std::shared_lock lock(m_mutex);
        return m_full_config.plex_username;
    }

    void set_plex_username(const std::string& username) override {
        std::unique_lock lock(m_mutex);
        m_full_config.plex_username = username;
        (void)save_configuration_internal();
    }

    std::string get_tmdb_access_token() const {
        std::shared_lock lock(m_mutex);
        return m_full_config.tmdb_access_token;
    }

    const std::unordered_map<std::string, std::unique_ptr<PlexServer>>& get_plex_servers() const override {
        static std::unordered_map<std::string, std::unique_ptr<PlexServer>> empty_servers;
        // Server loading functionality has been removed - return empty map
        return empty_servers;
    }

    uint64_t get_discord_client_id() const {
        std::shared_lock lock(m_mutex);
        return m_full_config.discord_client_id;
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
            PLEX_LOG_INFO("ConfigService", "Created config directory: " + dir.string());
        }
    }

    void generate_client_identifier() {
        PLEX_LOG_INFO("ConfigService", "Generating new Plex client identifier");
        utils::Uuid id = utils::Uuid::generate_v4();
        std::unique_lock lock(m_mutex);
        m_full_config.plex_client_identifier = id.to_string();
        PLEX_LOG_INFO("ConfigService", "Generated Plex client identifier: " + m_full_config.plex_client_identifier);
        (void)save_configuration_internal();
    }

    void load_from_yaml(const YAML::Node& node) {
        // General settings
        if (node["log_level"]) {
            m_full_config.log_level = node["log_level"].as<std::string>();
        }
        if (node["log_file_path"]) {
            m_full_config.log_file_path = node["log_file_path"].as<std::string>();
        }
        if (node["start_minimized"]) {
            m_full_config.start_minimized = node["start_minimized"].as<bool>();
        }

        // Discord settings
        if (node["discord"]) {
            auto discord = node["discord"];
            if (discord["client_id"]) {
                m_full_config.discord_client_id = discord["client_id"].as<uint64_t>();
            }
            if (discord["show_buttons"]) {
                m_full_config.discord.show_buttons = discord["show_buttons"].as<bool>();
            }
            if (discord["show_progress"]) {
                m_full_config.discord.show_progress = discord["show_progress"].as<bool>();
            }
        }

        // Plex settings
        if (node["plex"]) {
            auto plex = node["plex"];
            if (plex["auth_token"]) {
                m_full_config.plex_auth_token = plex["auth_token"].as<std::string>();
            }
            if (plex["client_identifier"]) {
                m_full_config.plex_client_identifier = plex["client_identifier"].as<std::string>();
            }
            if (plex["username"]) {
                m_full_config.plex_username = plex["username"].as<std::string>();
            }
            if (plex["auto_discover"]) {
                m_full_config.plex.auto_discover = plex["auto_discover"].as<bool>();
            }

            // Server loading has been disabled - servers are discovered fresh on each run
            if (plex["servers"]) {
                PLEX_LOG_DEBUG("ConfigService", "Ignoring saved servers - will discover fresh from Plex API");
            }
        }

        // TMDB settings
        if (node["tmdb"] && node["tmdb"]["access_token"]) {
            m_full_config.tmdb_access_token = node["tmdb"]["access_token"].as<std::string>();
        }
    }

    YAML::Node save_to_yaml() const {
        YAML::Node node;

        // General settings
        node["log_level"] = m_full_config.log_level;
        if (!m_full_config.log_file_path.empty()) {
            node["log_file_path"] = m_full_config.log_file_path;
        }
        node["start_minimized"] = m_full_config.start_minimized;

        // Discord settings
        node["discord"]["client_id"] = m_full_config.discord_client_id;
        node["discord"]["show_buttons"] = m_full_config.discord.show_buttons;
        node["discord"]["show_progress"] = m_full_config.discord.show_progress;

        // Plex settings
        if (!m_full_config.plex_auth_token.empty()) {
            node["plex"]["auth_token"] = m_full_config.plex_auth_token;
        }
        if (!m_full_config.plex_client_identifier.empty()) {
            node["plex"]["client_identifier"] = m_full_config.plex_client_identifier;
        }
        if (!m_full_config.plex_username.empty()) {
            node["plex"]["username"] = m_full_config.plex_username;
        }
        node["plex"]["auto_discover"] = m_full_config.plex.auto_discover;

        // Server saving has been disabled - servers are discovered fresh on each run

        // TMDB settings
        if (!m_full_config.tmdb_access_token.empty()) {
            node["tmdb"]["access_token"] = m_full_config.tmdb_access_token;
        }

        return node;
    }

private:
    mutable std::shared_mutex m_mutex;
    std::filesystem::path m_config_path;
    FullConfig m_full_config;
    ConfigChangeCallback m_change_callback;
};

std::unique_ptr<ConfigurationService> ConfigurationService::create(const std::filesystem::path& config_path) {
    return std::make_unique<ConfigurationServiceImpl>(config_path);
}

} // namespace core
} // namespace presence_for_plex
