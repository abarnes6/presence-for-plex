#pragma once

// Standard library headers
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <shared_mutex>
#include <string>
#include <vector>

// Third-party headers
#include <yaml-cpp/yaml.h>

// Project headers
#include "logger.h"
#include "models.h"

/**
 * @class Config
 * @brief Singleton class to manage application configuration
 */
class Config
{
public:
    /**
     * @brief Get the singleton instance
     * @return Reference to the Config instance
     */
    static Config &getInstance();

    /**
     * @brief Get the configuration directory path
     * @return Path to the configuration directory
     */
    static std::filesystem::path getConfigDirectory();

    //
    // Configuration file operations
    //

    /**
     * @brief Load configuration from file
     * @return True if successful, false otherwise
     */
    bool loadConfig();

    /**
     * @brief Save configuration to file
     * @return True if successful, false otherwise
     */
    bool saveConfig();

    //
    // General settings
    //

    /**
     * @brief Get current log level
     * @return Current log level
     */
    int getLogLevel() const;

    /**
     * @brief Set log level
     * @param level New log level
     */
    void setLogLevel(int level);

    //
    // Plex settings
    //

    /**
     * @brief Get Plex authentication token
     * @return Plex auth token
     */
    std::string getPlexAuthToken() const;

    /**
     * @brief Set Plex authentication token
     * @param token New auth token
     */
    void setPlexAuthToken(const std::string &token);

    /**
     * @brief Get Plex client identifier
     * @return Client identifier
     */
    std::string getPlexClientIdentifier() const;

    /**
     * @brief Set Plex client identifier
     * @param id New client identifier
     */
    void setPlexClientIdentifier(const std::string &id);

    /**
     * @brief Get Plex username
     * @return Plex username
     */
    std::string getPlexUsername() const;

    /**
     * @brief Set Plex username
     * @param username New username
     */
    void setPlexUsername(const std::string &username);

    /**
     * @brief Get TMDB access token
     * @return TMDB access token
     */
    std::string getTMDBAccessToken() const;

    /**
     * @brief Set TMDB access token
     * @param token New TMDB access token
     */
    void setTMDBAccessToken(const std::string &token);

    //
    // Plex server management
    //

    /**
     * @brief Get all configured Plex servers
     * @return Map of server client ID to server object
     */
    const std::map<std::string, std::shared_ptr<PlexServer>> &getPlexServers() const;

    /**
     * @brief Add or update a Plex server
     * @param name Server name
     * @param clientId Server client identifier
     * @param localUri Local network URI
     * @param publicUri Public network URI
     * @param accessToken Access token for this server
     * @param owned Whether this server is owned by the user
     */
    void addPlexServer(const std::string &name, const std::string &clientId,
                       const std::string &localUri, const std::string &publicUri,
                       const std::string &accessToken, bool owned = false);

    /**
     * @brief Remove all configured Plex servers
     */
    void clearPlexServers();

    //
    // Discord settings
    //

    /**
     * @brief Get Discord client ID
     * @return Discord client ID
     */
    uint64_t getDiscordClientId() const;

    /**
     * @brief Set Discord client ID
     * @param id New Discord client ID
     */
    void setDiscordClientId(uint64_t id);

private:
    // Singleton pattern implementation
    Config();
    ~Config();
    Config(const Config &) = delete;
    Config &operator=(const Config &) = delete;

    // Helper methods for YAML conversion
    void loadFromYaml(const YAML::Node &config);
    YAML::Node saveToYaml() const;

    // Configuration path
    std::filesystem::path configPath;

    // Configuration values
    std::atomic<int> logLevel{1};
    std::atomic<uint64_t> discordClientId{1359742002618564618};

    // Complex types need mutex protection
    std::string plexAuthToken;
    std::string plexClientIdentifier;
    std::string plexUsername;
    std::map<std::string, std::shared_ptr<PlexServer>> plexServers;
    std::string tmdbAccessToken{
        "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiIzNmMxOTI3ZjllMTlkMzUxZWFmMjAxNGViN2JmYjNkZiIsIm5iZiI6MTc0NTQzMTA3NC4yMjcsInN1YiI6IjY4MDkyYTIyNmUxYTc2OWU4MWVmMGJhOSIsInNjb3BlcyI6WyJhcGlfcmVhZCJdLCJ2ZXJzaW9uIjoxfQ.Td6eAbW7SgQOMmQpRDwVM-_3KIMybGRqWNK8Yqw1Zzs"};

    // Thread safety mutex
    mutable std::shared_mutex mutex;
};
