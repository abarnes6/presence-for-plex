#pragma once
#include "models.h"
#include "config.h"
#include <string>
#include <memory>
#include <iostream>

class DiscordClient {
public:
	DiscordClient();
	void updatePresence(const PlaybackInfo& info);
	void runDiscordCallbacks();
private:
	std::string token;
};