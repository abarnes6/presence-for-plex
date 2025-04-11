#include "discord.h"

DiscordClient::DiscordClient() {
	// Load configuration
    auto& config = Config::getInstance();
	auto applicationId = config.clientId;
    client = std::make_shared<discordpp::Client>();
    // Generate OAuth2 code verifier for authentication
    auto codeVerifier = client->CreateAuthorizationCodeVerifier();
    // Set up authentication arguments
    discordpp::AuthorizationArgs args{};
    args.SetClientId(applicationId);
    args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
    args.SetCodeChallenge(codeVerifier.Challenge());

    // Begin authentication process
    client->Authorize(args, [this, codeVerifier, applicationId](auto result, auto code, auto redirectUri) {
      if (!result.Successful()) {
        std::cerr << "❌ Authentication Error: " << result.Error() << std::endl;
        return;
      } else {
        std::cout << "✅ Authorization successful! Getting access token...\n";

        // Exchange auth code for access token
        this->client->GetToken(applicationId, code, codeVerifier.Verifier(), redirectUri,
          [this](discordpp::ClientResult result,
          std::string accessToken,
          std::string refreshToken,
          discordpp::AuthorizationTokenType tokenType,
          int32_t expiresIn,
          std::string scope) {
            std::cout << "🔓 Access token received! Establishing connection...\n";
            // Next Step: Update the token and connect
            this->client->UpdateToken(discordpp::AuthorizationTokenType::Bearer,  accessToken, [this](discordpp::ClientResult result) {
              if(result.Successful()) {
                std::cout << "🔑 Token updated, connecting to Discord...\n";
                this->client->Connect();
              }
            });
        });
      }
    });

}

// Update Discord Rich Presence
void DiscordClient::updatePresence(const PlaybackInfo& info) {
    try {
        // Create activity object for Discord Rich Presence
        discordpp::Activity activity;

        if (!info.isPlaying) {
            // If nothing is playing, just clear the rich presence
            discordpp::Activity emptyActivity;
            if (this->client) {
                this->client->UpdateRichPresence(std::move(emptyActivity), [](discordpp::ClientResult result) {
                    if (!result.Successful()) {
                        std::cerr << "Error clearing Discord activity: " << result.ToString() << std::endl;
                    }
                    });
            }
            return;
        }

        // Set activity type to Playing
        activity.SetType(discordpp::ActivityTypes::Playing);

        // Set state (what's playing)
        if (!info.subtitle.empty()) {
            activity.SetDetails(info.subtitle);
        }
        activity.SetState(info.title);

        // Set timestamps for progress bar
        discordpp::ActivityTimestamps timestamps;
        timestamps.SetStart(info.startTime);
        timestamps.SetEnd(info.startTime + info.duration);
        activity.SetTimestamps(timestamps);

        // Set assets (images)
        discordpp::ActivityAssets assets;

        // Set media type icon
        if (info.mediaType == "movie") {
            assets.SetLargeImage("movie_icon");
            assets.SetLargeText("Watching a Movie");
        }
        else if (info.mediaType == "episode") {
            assets.SetLargeImage("tv_icon");
            assets.SetLargeText("Watching a TV Show");
        }
        else {
            assets.SetLargeImage("plex_icon");
            assets.SetLargeText("Watching Plex");
        }

        // Set Plex logo as small icon
        assets.SetSmallImage("plex_logo");
        assets.SetSmallText("via Plex");

        activity.SetAssets(assets);

        // Update Discord presence
        if (this->client) {
            this->client->UpdateRichPresence(std::move(activity), [](discordpp::ClientResult result) {
                if (result.Successful()) {
                    std::cout << "🎮 Rich Presence updated successfully!\n";
                }
                else {
                    std::cerr << "❌ Rich Presence update failed: " << result.ToString() << std::endl;
                }
                });
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error updating Discord activity: " << e.what() << std::endl;
    }
}

// Run Discord callbacks - place this in the main loop
void DiscordClient::runDiscordCallbacks() {
    // In the new SDK, callbacks are handled by calling this function
    discordpp::RunCallbacks();
}