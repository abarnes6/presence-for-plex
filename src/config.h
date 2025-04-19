#pragma once

#include <string>
#include <filesystem>
#include <shared_mutex>
#include "toml++/toml.h"

class Config {
private:
    // Connection settings
    std::string serverIp;
    int port;
    bool forceHttps;

	// Plex update settings
    std::string plexToken;
    uint32_t pollInterval;

	// Logging settings
    std::string logLevelString;
    int logLevel;
    
    // TOML configuration
    toml::table config;
    long long clientId;
    std::string authToken;
    
    // Thread safety mutex
    mutable std::shared_mutex mutex;
    
    // Constructor is private for singleton
    Config();
    
    // Get configuration directory
    std::filesystem::path getConfigDirectory() const;
    bool setConfigValue(const std::string &key, const std::string &value);
    bool generateConfig();
    bool loadConfig();

public:
    // Singleton pattern
    static Config& getInstance();
    
    // Prevent copying and assignment
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    // Configuration methods
    std::filesystem::path getConfigFilePath() const;
    bool configExists();
    
    // Getters/Setters
    const std::string& getAuthToken() const;
    
    std::string getServerIp() const;
    void setServerIp(const std::string &url);
    
    int getPort() const;
    void setPort(int port);
    
    bool isForceHttps() const;
    void setForceHttps(bool forceHttps);
    
    std::string getPlexToken() const;
    void setPlexToken(const std::string &token);
    
    uint32_t getPollInterval() const;
    void setPollInterval(const uint32_t &interval);

	long long getClientId() const;
    void setClientId(long long id);

    int getLogLevel() const;
    void setLogLevel(int logLevel);
};
