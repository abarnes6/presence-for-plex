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
	std::string serverUrl;
	int pollInterval;
	uint64_t clientId;

private:
	Config();
	bool configExists();
	bool generateConfig();
	toml::v3::ex::parse_result config;

public:
	Config(Config const &) = delete;
	Config &operator=(Config const &) = delete;
};