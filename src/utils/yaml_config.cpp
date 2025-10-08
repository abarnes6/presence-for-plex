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

    if (node["presence"] || node["discord"]) {
        config.presence = parse_presence_config(node["presence"] ? node["presence"] : node["discord"]);
    }

    if (node["media"] || node["plex"]) {
        config.media = parse_media_config(node["media"] ? node["media"] : node["plex"]);
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

    merge_presence_config(node, config.presence);
    merge_media_config(node, config.media);

    node["tmdb"]["access_token"] = config.tmdb_access_token;
    node["tmdb"]["enabled"] = config.enable_tmdb;

    node["jikan"]["enabled"] = config.enable_jikan;

    return node;
}

void YamlConfigHelper::merge_presence_config(YAML::Node& node, const core::PresenceServiceConfig& config) {
    node["presence"]["enabled"] = config.enabled;
    node["presence"]["discord"]["client_id"] = config.discord.client_id;
    node["presence"]["discord"]["show_buttons"] = config.discord.show_buttons;
    node["presence"]["discord"]["show_progress"] = config.discord.show_progress;
    node["presence"]["discord"]["show_artwork"] = config.discord.show_artwork;
    node["presence"]["discord"]["update_interval"] = config.discord.update_interval.count();
    node["presence"]["discord"]["details_format"] = config.discord.details_format;
    node["presence"]["discord"]["state_format"] = config.discord.state_format;
}

void YamlConfigHelper::merge_media_config(YAML::Node& node, const core::MediaServiceConfig& config) {
    node["media"]["enabled"] = config.enabled;
    node["media"]["auto_discover"] = config.auto_discover;
    node["media"]["poll_interval"] = config.poll_interval.count();
    node["media"]["timeout"] = config.timeout.count();

    if (!config.server_urls.empty()) {
        node["media"]["server_urls"] = config.server_urls;
    }
}

core::PresenceServiceConfig YamlConfigHelper::parse_presence_config(const YAML::Node& node) {
    core::PresenceServiceConfig config;

    if (node["enabled"]) {
        config.enabled = node["enabled"].as<bool>();
    }

    // Support both old flat format and new nested discord format
    const YAML::Node discord_node = node["discord"] ? node["discord"] : node;

    if (discord_node["client_id"]) {
        config.discord.client_id = discord_node["client_id"].as<std::string>();
    }

    if (discord_node["show_buttons"]) {
        config.discord.show_buttons = discord_node["show_buttons"].as<bool>();
    }
    if (discord_node["show_progress"]) {
        config.discord.show_progress = discord_node["show_progress"].as<bool>();
    }
    if (discord_node["show_artwork"]) {
        config.discord.show_artwork = discord_node["show_artwork"].as<bool>();
    }

    if (discord_node["update_interval"]) {
        auto seconds = clamp_to_limits(
            discord_node["update_interval"].as<int>(),
            static_cast<int>(core::ConfigLimits::MIN_UPDATE_INTERVAL.count()),
            static_cast<int>(core::ConfigLimits::MAX_UPDATE_INTERVAL.count()));
        config.discord.update_interval = std::chrono::seconds(seconds);
    }

    if (discord_node["details_format"]) {
        config.discord.details_format = discord_node["details_format"].as<std::string>();
    }
    if (discord_node["state_format"]) {
        config.discord.state_format = discord_node["state_format"].as<std::string>();
    }

    return config;
}

core::MediaServiceConfig YamlConfigHelper::parse_media_config(const YAML::Node& node) {
    core::MediaServiceConfig config;

    if (node["enabled"]) {
        config.enabled = node["enabled"].as<bool>();
    }

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