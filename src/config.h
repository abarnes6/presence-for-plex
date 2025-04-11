#pragma once
#include <string>
#include <toml++/toml.hpp>
#include <iostream>
#include <fstream>

class Config {
public:
	static Config& getInstance();
	bool loadConfig();
	std::string serverUrl;
	std::string authToken;
	int pollInterval;
	uint64_t clientId;
private:
	Config() = default;
	bool configExists();
	bool generateConfig();
	toml::v3::ex::parse_result config;
public:
	Config(Config const&) = delete;
	Config& operator=(Config const&) = delete;
};