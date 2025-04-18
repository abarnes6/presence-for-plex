#pragma once
#include <string>
#include <toml++/toml.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>

class Config
{
public:
	static Config &getInstance();
	bool loadConfig();
	const std::string &getAuthToken() const;
	std::string serverUrl;
	int pollInterval;
	int64_t clientId; // Changed from uint64_t to int64_t
	bool setConfigValue(const std::string &key, const std::string &value);

private:
	Config();
	bool configExists();
	bool generateConfig();
	std::string authToken;
	toml::v3::ex::parse_result config;

public:
	Config(Config const &) = delete;
	Config &operator=(Config const &) = delete;
};