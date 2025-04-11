#pragma once
#include "models.h"
#include "config.h"
#include <string>
#include <memory>
#include <iostream>
#include <discordpp.h>

class DiscordClient {
public:
	DiscordClient();
	void updatePresence(const PlaybackInfo& info);
	void runDiscordCallbacks();
private:
	std::shared_ptr<discordpp::Client> client;
	std::string token;
};