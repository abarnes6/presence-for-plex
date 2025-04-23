#include "config.h"
#include "logger.h"
#include <fstream>
#include <yaml-cpp/yaml.h>

Config &Config::getInstance()
{
    static Config instance;
    return instance;
}

std::filesystem::path Config::getConfigDirectory()
{
    std::filesystem::path configDir;

#ifdef _WIN32
    // Windows: %APPDATA%\Plex Presence
    char *appData = nullptr;
    size_t appDataSize = 0;
    _dupenv_s(&appData, &appDataSize, "APPDATA");
    if (appData)
    {
        configDir = std::filesystem::path(appData) / "Plex Presence";
        free(appData);
    }
#else
    // Linux/macOS: ~/.config/plex-presence
    char *home = getenv("HOME");
    if (home)
    {
        configDir = std::filesystem::path(home) / ".config" / "plex-presence";
    }
#endif

    // Create directory if it doesn't exist
    if (!std::filesystem::exists(configDir))
    {
        std::filesystem::create_directories(configDir);
    }

    return configDir;
}

Config::Config() : logLevel(1), discordClientId(1359742002618564618), tmdbAccessToken("eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiIzNmMxOTI3ZjllMTlkMzUxZWFmMjAxNGViN2JmYjNkZiIsIm5iZiI6MTc0NTQzMTA3NC4yMjcsInN1YiI6IjY4MDkyYTIyNmUxYTc2OWU4MWVmMGJhOSIsInNjb3BlcyI6WyJhcGlfcmVhZCJdLCJ2ZXJzaW9uIjoxfQ.Td6eAbW7SgQOMmQpRDwVM-_3KIMybGRqWNK8Yqw1Zzs")
{
    configPath = getConfigDirectory() / "config.yaml";
    loadConfig();
}

Config::~Config()
{
    // Ensure config is saved
    saveConfig();
}

bool Config::loadConfig()
{
    if (!std::filesystem::exists(configPath))
    {
        LOG_INFO("Config", "Config file does not exist, creating default");
        return saveConfig();
    }

    try
    {
        YAML::Node config = YAML::LoadFile(configPath.string());
        loadFromYaml(config);
        LOG_INFO("Config", "Config loaded successfully");
        LOG_DEBUG("Config", "Found " + std::to_string(plexServers.size()) + " Plex servers in config");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Config", "Error loading config: " + std::string(e.what()));
        return false;
    }
}

bool Config::saveConfig()
{
    try
    {
        // Create the config directory if it doesn't exist
        std::filesystem::path configDir = configPath.parent_path();
        if (!std::filesystem::exists(configDir))
        {
            std::filesystem::create_directories(configDir);
        }

        // Build YAML data
        YAML::Node config = saveToYaml();

        // Write to file
        std::ofstream ofs(configPath);
        if (!ofs)
        {
            LOG_ERROR("Config", "Failed to open config file for writing");
            return false;
        }

        ofs << config;
        ofs.close();

        LOG_INFO("Config", "Config saved successfully");
        return true;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Config", "Error saving config: " + std::string(e.what()));
        return false;
    }
}

void Config::loadFromYaml(const YAML::Node &config)
{
    // General settings
    logLevel = config["log_level"] ? config["log_level"].as<int>() : 1;

    // Plex auth
    if (config["plex"])
    {
        const auto &plex = config["plex"];
        plexAuthToken = plex["auth_token"] ? plex["auth_token"].as<std::string>() : "";
        plexClientIdentifier = plex["client_identifier"] ? plex["client_identifier"].as<std::string>() : "";
    }

    // Plex servers
    plexServers.clear();
    if (config["plex_servers"] && config["plex_servers"].IsSequence())
    {
        for (const auto &server : config["plex_servers"])
        {
            PlexServerConfig cfg;
            cfg.name = server["name"] ? server["name"].as<std::string>() : "";
            cfg.clientIdentifier = server["client_identifier"] ? server["client_identifier"].as<std::string>() : "";
            cfg.localUri = server["local_uri"] ? server["local_uri"].as<std::string>() : "";
            cfg.publicUri = server["public_uri"] ? server["public_uri"].as<std::string>() : "";
            cfg.accessToken = server["access_token"] ? server["access_token"].as<std::string>() : "";
            plexServers.push_back(cfg);
        }
    }

    // Discord settings
    if (config["discord"])
    {
        const auto &discord = config["discord"];
        discordClientId = discord["client_id"] ? discord["client_id"].as<uint64_t>() : discordClientId;
    }

    // TMDB API key
    if (config["tmdb_access_token"])
    {
        tmdbAccessToken = config["tmdb_access_token"].as<std::string>();
    }
}

YAML::Node Config::saveToYaml() const
{
    YAML::Node config;

    // General settings
    config["log_level"] = logLevel;

    // Plex auth
    YAML::Node plex;
    plex["auth_token"] = plexAuthToken;
    plex["client_identifier"] = plexClientIdentifier;
    config["plex"] = plex;

    // Plex servers
    YAML::Node servers;
    for (const auto &server : plexServers)
    {
        YAML::Node serverNode;
        serverNode["name"] = server.name;
        serverNode["client_identifier"] = server.clientIdentifier;
        serverNode["local_uri"] = server.localUri;
        serverNode["public_uri"] = server.publicUri;
        serverNode["access_token"] = server.accessToken;
        servers.push_back(serverNode);
    }
    config["plex_servers"] = servers;

    // Discord settings
    YAML::Node discord;
    discord["client_id"] = discordClientId;
    config["discord"] = discord;

    // TMDB API key
    config["tmdb_access_token"] = tmdbAccessToken;

    return config;
}

int Config::getLogLevel() const
{
    return logLevel;
}

void Config::setLogLevel(int level)
{
    logLevel = level;
}

std::string Config::getPlexAuthToken() const
{
    return plexAuthToken;
}

void Config::setPlexAuthToken(const std::string &token)
{
    plexAuthToken = token;
}

std::string Config::getPlexClientIdentifier() const
{
    return plexClientIdentifier;
}

void Config::setPlexClientIdentifier(const std::string &id)
{
    plexClientIdentifier = id;
}

const std::vector<PlexServerConfig> &Config::getPlexServers() const
{
    return plexServers;
}

void Config::addPlexServer(const std::string &name, const std::string &clientId,
                           const std::string &localUri, const std::string &publicUri,
                           const std::string &accessToken)
{
    // Check if this server already exists
    for (auto &server : plexServers)
    {
        if (server.clientIdentifier == clientId)
        {
            // Update existing server
            server.name = name;
            server.localUri = localUri;
            server.publicUri = publicUri;
            server.accessToken = accessToken;
            return;
        }
    }

    // Add new server
    PlexServerConfig cfg;
    cfg.name = name;
    cfg.clientIdentifier = clientId;
    cfg.localUri = localUri;
    cfg.publicUri = publicUri;
    cfg.accessToken = accessToken;
    plexServers.push_back(cfg);
}

void Config::clearPlexServers()
{
    plexServers.clear();
}

uint64_t Config::getDiscordClientId() const
{
    return discordClientId;
}

void Config::setDiscordClientId(const uint64_t &id)
{
    discordClientId = id;
}

std::string Config::getTMDBAccessToken() const
{
    return tmdbAccessToken;
}

void Config::setTMDBAccessToken(const std::string &token)
{
    tmdbAccessToken = token;
}