#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <shared_mutex>
#include "yaml-cpp/yaml.h" // Replace TOML++ with yaml-cpp
#include "models.h"        // Include models.h to get PlexServer definition

struct PlexServerConfig
{
    std::string name;
    std::string clientIdentifier;
    std::string localUri;
    std::string publicUri;
    std::string accessToken;
};

class Config
{
public:
    static Config &getInstance();
    static std::filesystem::path getConfigDirectory();

    // Load/save config
    bool loadConfig();
    bool saveConfig();

    // General settings
    int getLogLevel() const;
    void setLogLevel(int level);

    // Plex settings
    std::string getPlexAuthToken() const;
    void setPlexAuthToken(const std::string &token);

    std::string getPlexClientIdentifier() const;
    void setPlexClientIdentifier(const std::string &id);

    // Plex server management
    const std::vector<PlexServerConfig> &getPlexServers() const;
    void addPlexServer(const std::string &name, const std::string &clientId,
                       const std::string &localUri, const std::string &publicUri,
                       const std::string &accessToken);
    void clearPlexServers();

    // Discord settings
    uint64_t getDiscordClientId() const;
    void setDiscordClientId(const uint64_t &id);

private:
    Config();
    ~Config();

    // Disable copy
    Config(const Config &) = delete;
    Config &operator=(const Config &) = delete;

    std::filesystem::path configPath;

    // Config values
    int logLevel;
    uint64_t discordClientId;
    std::string plexAuthToken;
    std::string plexClientIdentifier;
    std::vector<PlexServerConfig> plexServers;

    // Connection settings
    std::string serverIp;
    int port;
    bool forceHttps;
    bool serverIpConfigured;

    // Plex update settings
    std::string plexToken;

    // Logging settings
    std::string logLevelString;

    // YAML configuration
    YAML::Node config; // Change from toml::table to YAML::Node
    long long clientId;
    std::string authToken;

    // Thread safety mutex
    mutable std::shared_mutex mutex;

    // Helper methods for YAML conversion
    void loadFromYaml(const YAML::Node &config);
    YAML::Node saveToYaml() const;
};
