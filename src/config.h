#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <map>
#include <shared_mutex>
#include "yaml-cpp/yaml.h" // Replace TOML++ with yaml-cpp
#include "models.h"        // Include models.h to get PlexServer definition

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

    std::string getTMDBAccessToken() const;
    void setTMDBAccessToken(const std::string &token);

    std::string getPlexUsername() const;               // Add getter for username
    void setPlexUsername(const std::string &username); // Add setter for username

    // Plex server management
    const std::map<std::string, std::shared_ptr<PlexServer>> &getPlexServers() const;
    void addPlexServer(const std::string &name, const std::string &clientId,
                       const std::string &localUri, const std::string &publicUri,
                       const std::string &accessToken, bool owned = false); // Add owned parameter
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
    std::string plexUsername; // Add username storage
    std::map<std::string, std::shared_ptr<PlexServer>> plexServers;
    std::string tmdbAccessToken; // TMDB Access Token for fetching artwork

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
