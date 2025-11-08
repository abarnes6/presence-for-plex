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
        LOG_WARNING("YamlConfig", "File not found: " + path.string());
        return std::unexpected(core::ConfigError::FileNotFound);
    }

    try {
        YAML::Node node = YAML::LoadFile(path.string());
        return from_yaml(node);
    } catch (const std::exception& e) {
        LOG_ERROR("YamlConfig", "Parse error: " + std::string(e.what()));
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
            LOG_ERROR("YamlConfig", "Cannot open file for writing: " + path.string());
            return std::unexpected(core::ConfigError::PermissionDenied);
        }

        file << node;
        return {};
    } catch (const std::exception& e) {
        LOG_ERROR("YamlConfig", "Save error: " + std::string(e.what()));
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

    if (node["media_services"]) {
        config.media_services = parse_media_services_config(node["media_services"]);
    } else if (node["plex"]) {
        config.media_services.plex = parse_plex_config(node["plex"]);
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
    merge_media_services_config(node, config.media_services);

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

    // TV Show formats
    node["presence"]["discord"]["tv_details_format"] = config.discord.tv_details_format;
    node["presence"]["discord"]["tv_state_format"] = config.discord.tv_state_format;
    node["presence"]["discord"]["tv_large_image_text_format"] = config.discord.tv_large_image_text_format;

    // Movie formats
    node["presence"]["discord"]["movie_details_format"] = config.discord.movie_details_format;
    node["presence"]["discord"]["movie_state_format"] = config.discord.movie_state_format;
    node["presence"]["discord"]["movie_large_image_text_format"] = config.discord.movie_large_image_text_format;

    // Music formats
    node["presence"]["discord"]["music_details_format"] = config.discord.music_details_format;
    node["presence"]["discord"]["music_state_format"] = config.discord.music_state_format;
    node["presence"]["discord"]["music_large_image_text_format"] = config.discord.music_large_image_text_format;
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

    // TV Show formats
    if (discord_node["tv_details_format"]) {
        config.discord.tv_details_format = discord_node["tv_details_format"].as<std::string>();
    }
    if (discord_node["tv_state_format"]) {
        config.discord.tv_state_format = discord_node["tv_state_format"].as<std::string>();
    }
    if (discord_node["tv_large_image_text_format"]) {
        config.discord.tv_large_image_text_format = discord_node["tv_large_image_text_format"].as<std::string>();
    }

    // Movie formats
    if (discord_node["movie_details_format"]) {
        config.discord.movie_details_format = discord_node["movie_details_format"].as<std::string>();
    }
    if (discord_node["movie_state_format"]) {
        config.discord.movie_state_format = discord_node["movie_state_format"].as<std::string>();
    }
    if (discord_node["movie_large_image_text_format"]) {
        config.discord.movie_large_image_text_format = discord_node["movie_large_image_text_format"].as<std::string>();
    }

    // Music formats
    if (discord_node["music_details_format"]) {
        config.discord.music_details_format = discord_node["music_details_format"].as<std::string>();
    }
    if (discord_node["music_state_format"]) {
        config.discord.music_state_format = discord_node["music_state_format"].as<std::string>();
    }
    if (discord_node["music_large_image_text_format"]) {
        config.discord.music_large_image_text_format = discord_node["music_large_image_text_format"].as<std::string>();
    }

    // Backward compatibility: if old format fields exist, use them as defaults for all media types
    if (discord_node["details_format"]) {
        auto details = discord_node["details_format"].as<std::string>();
        if (config.discord.tv_details_format.empty()) config.discord.tv_details_format = details;
        if (config.discord.movie_details_format.empty()) config.discord.movie_details_format = details;
        if (config.discord.music_details_format.empty()) config.discord.music_details_format = details;
    }
    if (discord_node["state_format"]) {
        auto state = discord_node["state_format"].as<std::string>();
        if (config.discord.tv_state_format.empty()) config.discord.tv_state_format = state;
        if (config.discord.movie_state_format.empty()) config.discord.movie_state_format = state;
        if (config.discord.music_state_format.empty()) config.discord.music_state_format = state;
    }
    if (discord_node["large_image_text_format"]) {
        auto large_image_text = discord_node["large_image_text_format"].as<std::string>();
        if (config.discord.tv_large_image_text_format.empty()) config.discord.tv_large_image_text_format = large_image_text;
        if (config.discord.movie_large_image_text_format.empty()) config.discord.movie_large_image_text_format = large_image_text;
        if (config.discord.music_large_image_text_format.empty()) config.discord.music_large_image_text_format = large_image_text;
    }

    return config;
}

core::PlexServiceConfig YamlConfigHelper::parse_plex_config(const YAML::Node& node) {
    core::PlexServiceConfig config;

    if (node["enabled"]) {
        config.enabled = node["enabled"].as<bool>();
    }

    if (node["auto_discover"]) {
        config.auto_discover = node["auto_discover"].as<bool>();
    }

    if (node["enable_movies"]) {
        config.enable_movies = node["enable_movies"].as<bool>();
    }

    if (node["enable_tv_shows"]) {
        config.enable_tv_shows = node["enable_tv_shows"].as<bool>();
    }

    if (node["enable_music"]) {
        config.enable_music = node["enable_music"].as<bool>();
    }

    if (node["server_urls"]) {
        for (const auto& url : node["server_urls"]) {
            config.server_urls.push_back(url.as<std::string>());
        }
    }

    return config;
}

core::MediaServicesConfig YamlConfigHelper::parse_media_services_config(const YAML::Node& node) {
    core::MediaServicesConfig config;

    if (node["plex"]) {
        config.plex = parse_plex_config(node["plex"]);
    }

    // Future: Parse other service configs here
    // if (node["jellyfin"]) { config.jellyfin = parse_jellyfin_config(node["jellyfin"]); }

    return config;
}

void YamlConfigHelper::merge_plex_config(YAML::Node& node, const core::PlexServiceConfig& config) {
    node["enabled"] = config.enabled;
    node["auto_discover"] = config.auto_discover;

    node["enable_movies"] = config.enable_movies;
    node["enable_tv_shows"] = config.enable_tv_shows;
    node["enable_music"] = config.enable_music;

    if (!config.server_urls.empty()) {
        node["server_urls"] = config.server_urls;
    }
}

void YamlConfigHelper::merge_media_services_config(YAML::Node& node, const core::MediaServicesConfig& config) {
    YAML::Node plex_node = node["media_services"]["plex"];
    merge_plex_config(plex_node, config.plex);
    node["media_services"]["plex"] = plex_node;
}

} // namespace utils
} // namespace presence_for_plex