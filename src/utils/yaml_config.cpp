#include "presence_for_plex/utils/yaml_config.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <fstream>
#include <algorithm>

namespace presence_for_plex {
namespace utils {

namespace {
    template<typename T>
    T clamp_to_limits(T value, T min, T max) {
        return std::clamp(value, min, max);
    }
}

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

    if (node["log_level"]) {
        config.log_level = log_level_from_string(node["log_level"].as<std::string>());
    }
    if (node["start_at_boot"]) {
        config.start_at_boot = node["start_at_boot"].as<bool>();
    }

    if (node["discord"]) {
        config.discord = parse_discord_config(node["discord"]);
    }

    if (node["plex"]) {
        config.plex = parse_plex_config(node["plex"]);
    }

    if (node["tmdb"]) {
        if (node["tmdb"]["access_token"]) {
            config.tmdb_access_token = node["tmdb"]["access_token"].as<std::string>();
        }
        if (node["tmdb"]["enabled"]) {
            config.enable_tmdb = node["tmdb"]["enabled"].as<bool>();
        }
    }

    if (node["jikan"] && node["jikan"]["enabled"]) {
        config.enable_jikan = node["jikan"]["enabled"].as<bool>();
    }

    return config;
}

YAML::Node YamlConfigHelper::to_yaml(const core::ApplicationConfig& config) {
    YAML::Node node;

    node["log_level"] = to_string(config.log_level);
    node["start_at_boot"] = config.start_at_boot;

    merge_discord_config(node, config.discord);
    merge_plex_config(node, config.plex);

    node["tmdb"]["access_token"] = config.tmdb_access_token;
    node["tmdb"]["enabled"] = config.enable_tmdb;

    node["jikan"]["enabled"] = config.enable_jikan;

    return node;
}

void YamlConfigHelper::merge_discord_config(YAML::Node& node, const core::DiscordConfig& config) {
    node["discord"]["client_id"] = config.client_id;
    node["discord"]["show_buttons"] = config.show_buttons;
    node["discord"]["show_progress"] = config.show_progress;
    node["discord"]["show_artwork"] = config.show_artwork;
    node["discord"]["update_interval"] = config.update_interval.count();
    node["discord"]["details_format"] = config.details_format;
    node["discord"]["state_format"] = config.state_format;
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

    if (node["client_id"]) {
        config.client_id = node["client_id"].as<std::string>();
    }

    if (node["show_buttons"]) {
        config.show_buttons = node["show_buttons"].as<bool>();
    }
    if (node["show_progress"]) {
        config.show_progress = node["show_progress"].as<bool>();
    }
    if (node["show_artwork"]) {
        config.show_artwork = node["show_artwork"].as<bool>();
    }

    if (node["update_interval"]) {
        auto seconds = clamp_to_limits(
            node["update_interval"].as<int>(),
            static_cast<int>(core::ConfigLimits::MIN_UPDATE_INTERVAL.count()),
            static_cast<int>(core::ConfigLimits::MAX_UPDATE_INTERVAL.count()));
        config.update_interval = std::chrono::seconds(seconds);
    }

    if (node["details_format"]) {
        config.details_format = node["details_format"].as<std::string>();
    }
    if (node["state_format"]) {
        config.state_format = node["state_format"].as<std::string>();
    }

    return config;
}

core::PlexConfig YamlConfigHelper::parse_plex_config(const YAML::Node& node) {
    core::PlexConfig config;

    if (node["auto_discover"]) {
        config.auto_discover = node["auto_discover"].as<bool>();
    }

    if (node["poll_interval"]) {
        auto seconds = clamp_to_limits(
            node["poll_interval"].as<int>(),
            static_cast<int>(core::ConfigLimits::MIN_POLL_INTERVAL.count()),
            static_cast<int>(core::ConfigLimits::MAX_POLL_INTERVAL.count()));
        config.poll_interval = std::chrono::seconds(seconds);
    }

    if (node["timeout"]) {
        auto seconds = clamp_to_limits(
            node["timeout"].as<int>(),
            static_cast<int>(core::ConfigLimits::MIN_TIMEOUT.count()),
            static_cast<int>(core::ConfigLimits::MAX_TIMEOUT.count()));
        config.timeout = std::chrono::seconds(seconds);
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