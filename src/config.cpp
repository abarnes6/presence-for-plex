#include "config.h"

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

Config::Config()
{
    configPath = getConfigDirectory() / "config.yaml";
    loadConfig();
}

Config::~Config()
{
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
        YAML::Node loadedConfig = YAML::LoadFile(configPath.string());

        // Thread-safe update of configuration
        {
            std::unique_lock lock(mutex);
            loadFromYaml(loadedConfig);
        }

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

        // Build YAML data with thread safety
        YAML::Node configToSave;
        {
            std::shared_lock lock(mutex);
            configToSave = saveToYaml();
        }

        // Write to file
        std::ofstream ofs(configPath);
        if (!ofs)
        {
            LOG_ERROR("Config", "Failed to open config file for writing");
            return false;
        }

        ofs << configToSave;
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
        plexUsername = plex["username"] ? plex["username"].as<std::string>() : "";
    }

    // Plex servers
    plexServers.clear();
    if (config["plex_servers"] && config["plex_servers"].IsSequence())
    {
        for (const auto &server : config["plex_servers"])
        {
            std::string name = server["name"] ? server["name"].as<std::string>() : "";
            std::string clientId = server["client_identifier"] ? server["client_identifier"].as<std::string>() : "";
            std::string localUri = server["local_uri"] ? server["local_uri"].as<std::string>() : "";
            std::string publicUri = server["public_uri"] ? server["public_uri"].as<std::string>() : "";
            std::string accessToken = server["access_token"] ? server["access_token"].as<std::string>() : "";
            bool owned = server["owned"] ? server["owned"].as<bool>() : false;

            auto serverPtr = std::make_shared<PlexServer>();
            serverPtr->name = name;
            serverPtr->clientIdentifier = clientId;
            serverPtr->localUri = localUri;
            serverPtr->publicUri = publicUri;
            serverPtr->accessToken = accessToken;
            serverPtr->owned = owned;

            plexServers[clientId] = serverPtr;
        }
    }

    // Discord settings
    if (config["discord"])
    {
        const auto &discord = config["discord"];
        discordClientId = discord["client_id"] ? discord["client_id"].as<uint64_t>() : discordClientId.load();
    }

    // TMDB API key
    if (config["tmdb_access_token"])
    {
        tmdbAccessToken = config["tmdb_access_token"].as<std::string>();
    }
}

// Version information
std::string Config::getVersionString() const
{
    return VERSION_STRING;
}

int Config::getVersionMajor() const
{
    return VERSION_MAJOR;
}

int Config::getVersionMinor() const
{
    return VERSION_MINOR;
}

int Config::getVersionPatch() const
{
    return VERSION_PATCH;
}

YAML::Node Config::saveToYaml() const
{
    YAML::Node config;

    // General settings
    config["log_level"] = logLevel.load();

    // Plex auth
    YAML::Node plex;
    plex["auth_token"] = plexAuthToken;
    plex["client_identifier"] = plexClientIdentifier;
    plex["username"] = plexUsername;
    config["plex"] = plex;

    // Plex servers
    YAML::Node servers;
    for (const auto &[id, server] : plexServers)
    {
        YAML::Node serverNode;
        serverNode["name"] = server->name;
        serverNode["client_identifier"] = server->clientIdentifier;
        serverNode["local_uri"] = server->localUri;
        serverNode["public_uri"] = server->publicUri;
        serverNode["access_token"] = server->accessToken;
        serverNode["owned"] = server->owned;
        servers.push_back(serverNode);
    }
    config["plex_servers"] = servers;

    // Discord settings
    YAML::Node discord;
    discord["client_id"] = discordClientId.load();
    config["discord"] = discord;

    // Version information
    YAML::Node version;
    version["major"] = VERSION_MAJOR;
    version["minor"] = VERSION_MINOR;
    version["patch"] = VERSION_PATCH;
    version["string"] = VERSION_STRING;
    config["version"] = version;

    // TMDB API key
    config["tmdb_access_token"] = tmdbAccessToken;

    return config;
}

// General settings
int Config::getLogLevel() const
{
    return logLevel.load();
}

void Config::setLogLevel(int level)
{
    logLevel.store(level);
}

// Plex settings
std::string Config::getPlexAuthToken() const
{
    std::shared_lock lock(mutex);
    return plexAuthToken;
}

void Config::setPlexAuthToken(const std::string &token)
{
    std::unique_lock lock(mutex);
    plexAuthToken = token;
}

std::string Config::getPlexClientIdentifier() const
{
    std::shared_lock lock(mutex);
    return plexClientIdentifier;
}

void Config::setPlexClientIdentifier(const std::string &id)
{
    std::unique_lock lock(mutex);
    plexClientIdentifier = id;
}

std::string Config::getPlexUsername() const
{
    std::shared_lock lock(mutex);
    return plexUsername;
}

void Config::setPlexUsername(const std::string &username)
{
    std::unique_lock lock(mutex);
    plexUsername = username;
}

const std::map<std::string, std::shared_ptr<PlexServer>> &Config::getPlexServers() const
{
    std::shared_lock lock(mutex);
    return plexServers;
}

void Config::addPlexServer(const std::string &name, const std::string &clientId,
                           const std::string &localUri, const std::string &publicUri,
                           const std::string &accessToken, bool owned)
{
    std::unique_lock lock(mutex);

    // Check if this server already exists
    auto it = plexServers.find(clientId);
    if (it != plexServers.end())
    {
        // Update existing server
        it->second->name = name;
        it->second->localUri = localUri;
        it->second->publicUri = publicUri;
        it->second->accessToken = accessToken;
        it->second->owned = owned;
        return;
    }

    // Add new server
    auto server = std::make_shared<PlexServer>();
    server->name = name;
    server->clientIdentifier = clientId;
    server->localUri = localUri;
    server->publicUri = publicUri;
    server->accessToken = accessToken;
    server->owned = owned;
    plexServers[clientId] = server;
}

void Config::clearPlexServers()
{
    std::unique_lock lock(mutex);
    plexServers.clear();
}

// Discord settings
uint64_t Config::getDiscordClientId() const
{
    return discordClientId.load();
}

void Config::setDiscordClientId(uint64_t id)
{
    discordClientId.store(id);
}

std::string Config::getTMDBAccessToken() const
{
    std::shared_lock lock(mutex);
    return tmdbAccessToken;
}

void Config::setTMDBAccessToken(const std::string &token)
{
    std::unique_lock lock(mutex);
    tmdbAccessToken = token;
}