#include "presence_for_plex/utils/yaml_config.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <fstream>

namespace presence_for_plex {
namespace utils {

std::expected<core::ApplicationConfig, core::ConfigError>
YamlConfigHelper::load_from_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        PLEX_LOG_WARNING("YamlConfig", "File not found: " + path.string());
        return std::unexpected(core::ConfigError::FileNotFound);
    }

    try {
        YAML::Node node = YAML::LoadFile(path.string());
        return from_yaml(node);
    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("YamlConfig", "Parse error: " + std::string(e.what()));
        return std::unexpected(core::ConfigError::InvalidFormat);
    }
}

std::expected<void, core::ConfigError>
YamlConfigHelper::save_to_file(const core::ApplicationConfig& config, const std::filesystem::path& path) {
    try {
        // Ensure directory exists
        auto dir = path.parent_path();
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }

        // Write YAML
        YAML::Node node = to_yaml(config);
        std::ofstream file(path);
        if (!file) {
            PLEX_LOG_ERROR("YamlConfig", "Cannot open file for writing: " + path.string());
            return std::unexpected(core::ConfigError::PermissionDenied);
        }

        file << node;
        return {};
    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("YamlConfig", "Save error: " + std::string(e.what()));
        return std::unexpected(core::ConfigError::InvalidFormat);
    }
}

core::ApplicationConfig YamlConfigHelper::from_yaml(const YAML::Node& node) {
    core::ApplicationConfig config;

    // General settings
    if (node["log_level"]) {
        config.log_level = node["log_level"].as<std::string>();
    }
    if (node["start_minimized"]) {
        config.start_minimized = node["start_minimized"].as<bool>();
    }

    // Discord settings
    if (node["discord"]) {
        config.discord = parse_discord_config(node["discord"]);
    }

    // Plex settings
    if (node["plex"]) {
        config.plex = parse_plex_config(node["plex"]);
    }

    // TMDB settings
    if (node["tmdb"] && node["tmdb"]["access_token"]) {
        config.tmdb_access_token = node["tmdb"]["access_token"].as<std::string>();
    }

    return config;
}

YAML::Node YamlConfigHelper::to_yaml(const core::ApplicationConfig& config) {
    YAML::Node node;

    // General settings
    node["log_level"] = config.log_level;
    node["start_minimized"] = config.start_minimized;

    // Discord settings
    merge_discord_config(node, config.discord);

    // Plex settings
    merge_plex_config(node, config.plex);

    // TMDB settings (only save if not default)
    if (config.tmdb_access_token != core::ApplicationConfig{}.tmdb_access_token) {
        node["tmdb"]["access_token"] = config.tmdb_access_token;
    }

    return node;
}

void YamlConfigHelper::merge_discord_config(YAML::Node& node, const core::DiscordConfig& config) {
    if (!config.application_id.empty()) {
        // Parse as number if it's the default Discord client ID
        if (config.application_id == "1359742002618564618") {
            node["discord"]["client_id"] = 1359742002618564618ULL;
        } else {
            try {
                node["discord"]["client_id"] = std::stoull(config.application_id);
            } catch (...) {
                node["discord"]["application_id"] = config.application_id;
            }
        }
    }
    node["discord"]["show_buttons"] = config.show_buttons;
    node["discord"]["show_progress"] = config.show_progress;
    node["discord"]["update_interval"] = config.update_interval.count();
}

void YamlConfigHelper::merge_plex_config(YAML::Node& node, const core::PlexConfig& config) {
    node["plex"]["auto_discover"] = config.auto_discover;
    node["plex"]["poll_interval"] = config.poll_interval.count();
    node["plex"]["timeout"] = config.timeout.count();

    if (!config.server_urls.empty()) {
        node["plex"]["server_urls"] = config.server_urls;
    }
}

core::DiscordConfig YamlConfigHelper::parse_discord_config(const YAML::Node& node) {
    core::DiscordConfig config;

    // Handle both client_id and application_id fields
    if (node["client_id"]) {
        config.application_id = std::to_string(node["client_id"].as<uint64_t>());
    } else if (node["application_id"]) {
        config.application_id = node["application_id"].as<std::string>();
    } else {
        // Use default Discord client ID
        config.application_id = "1359742002618564618";
    }

    if (node["show_buttons"]) {
        config.show_buttons = node["show_buttons"].as<bool>();
    }
    if (node["show_progress"]) {
        config.show_progress = node["show_progress"].as<bool>();
    }
    if (node["update_interval"]) {
        config.update_interval = std::chrono::seconds(node["update_interval"].as<int>());
    }

    return config;
}

core::PlexConfig YamlConfigHelper::parse_plex_config(const YAML::Node& node) {
    core::PlexConfig config;

    if (node["auto_discover"]) {
        config.auto_discover = node["auto_discover"].as<bool>();
    }
    if (node["poll_interval"]) {
        config.poll_interval = std::chrono::seconds(node["poll_interval"].as<int>());
    }
    if (node["timeout"]) {
        config.timeout = std::chrono::seconds(node["timeout"].as<int>());
    }
    if (node["server_urls"]) {
        for (const auto& url : node["server_urls"]) {
            config.server_urls.push_back(url.as<std::string>());
        }
    }

    return config;
}

} // namespace utils
} // namespace presence_for_plex