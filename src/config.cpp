#include "config.h"

const std::string filename = "config.toml";

Config &Config::getInstance()
{
	static Config instance;
	return instance;
}

Config::Config()
{
	// Load the configuration
	if (!loadConfig())
	{
		std::cerr << "Failed to load configuration." << std::endl;
	}
	else
	{
		std::cout << "Configuration loaded successfully." << std::endl;
	}
}

bool Config::loadConfig()
{
	try
	{
		// Check if the configuration file exists
		if (!configExists())
		{
			std::cerr << "Configuration file not found. Generating default configuration..." << std::endl;
			if (!generateConfig())
			{
				std::cerr << "Failed to generate configuration file." << std::endl;
				return false;
			}
		}
		config = toml::parse_file(filename);
		serverUrl = config["plex"]["server_url"].value_or("http://localhost:32400");
		pollInterval = config["plex"]["poll_interval"].value_or(5);
		clientId = config["discord"]["client_id"].value_or(1359742002618564618);
		authToken = config["plex"]["auth_token"].value_or(std::string{});
		return true;
	}
	catch (const toml::parse_error &err)
	{
		std::cerr << "Error parsing configuration file: " << err.what() << std::endl;
		return false;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error loading configuration: " << e.what() << std::endl;
		return false;
	}
}

bool Config::configExists()
{
	try
	{
		return std::filesystem::exists(std::filesystem::path{filename});
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error checking configuration file: " << e.what() << std::endl;
		return false;
	}
}

bool Config::generateConfig()
{
	try
	{
		std::ofstream configFile;
		configFile.open(filename);
		if (!configFile.is_open())
		{
			std::cerr << "Error creating configuration file: " << filename << std::endl;
			return false;
		}
		configFile << "[plex]\n";
		configFile << "server_url = \"http://localhost:32400\"\n";
		configFile << "poll_interval = 5\n";
		configFile << "auth_token = \"\"\n\n";
		configFile << "[discord]\n";
		configFile << "client_id = 1359742002618564618\n";
		configFile.close();
		std::cout << "Configuration file created: " << filename << std::endl;
		return true;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error generating configuration file: " << e.what() << std::endl;
		return false;
	}
}

bool Config::setConfigValue(const std::string &key, const std::string &value)
{
	try
	{
		if (key == "server_url")
		{
			serverUrl = value;
			config["plex"].as_table()->insert_or_assign("server_url", value);
		}
		else if (key == "poll_interval")
		{
			pollInterval = std::stoi(value);
			config["plex"].as_table()->insert_or_assign("poll_interval", pollInterval);
		}
		else if (key == "auth_token")
		{
			authToken = value;
			config["plex"].as_table()->insert_or_assign("auth_token", value);
		}
		else if (key == "client_id")
		{
			clientId = std::stoull(value);
			config["discord"].as_table()->insert_or_assign("client_id", clientId);
		}
		else
		{
			std::cerr << "Invalid configuration key: " << key << std::endl;
			return false;
		}

		std::ofstream configFile(filename);
		if (!configFile.is_open())
		{
			std::cerr << "Error opening configuration file for writing: " << filename << std::endl;
			return false;
		}
		configFile << config;
		configFile.close();
		std::cout << "Configuration updated: " << key << " = " << value << std::endl;
		return true;
	}
	catch (const std::exception &e)
	{
		std::cerr << "Error setting configuration value: " << e.what() << std::endl;
		return false;
	}
}

// Accessor for auth token
const std::string &Config::getAuthToken() const
{
	return authToken;
}