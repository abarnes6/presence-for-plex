#include "discord.h"

DiscordClient::DiscordClient() {
	// Load configuration
    auto& config = Config::getInstance();
	auto applicationId = config.clientId;


}

// Update Discord Rich Presence
void DiscordClient::updatePresence(const PlaybackInfo& info) {

}
