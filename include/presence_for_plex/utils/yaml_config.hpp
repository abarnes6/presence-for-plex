#pragma once

#include "presence_for_plex/core/models.hpp"
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <expected>

namespace presence_for_plex {
namespace utils {

class YamlConfigHelper {
public:
    // Load configuration from YAML file
    static std::expected<core::ApplicationConfig, core::ConfigError>
    load_from_file(const std::filesystem::path& path);

    // Save configuration to YAML file
    static std::expected<void, core::ConfigError>
    save_to_file(const core::ApplicationConfig& config, const std::filesystem::path& path);

    // Convert between YAML nodes and config structures
    static core::ApplicationConfig from_yaml(const YAML::Node& node);
    static YAML::Node to_yaml(const core::ApplicationConfig& config);

    // Partial updates
    static void merge_discord_config(YAML::Node& node, const core::DiscordConfig& config);
    static void merge_plex_config(YAML::Node& node, const core::PlexConfig& config);

private:
    static core::DiscordConfig parse_discord_config(const YAML::Node& node);
    static core::PlexConfig parse_plex_config(const YAML::Node& node);
};

} // namespace utils
} // namespace presence_for_plex