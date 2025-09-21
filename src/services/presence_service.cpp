#include "presence_for_plex/services/presence_service.hpp"
#include "presence_for_plex/services/discord_presence_service.hpp"

namespace presence_for_plex::services {

bool PresenceData::is_valid() const {
    return !state.empty() || !details.empty() || !large_image_key.empty();
}

class DefaultPresenceFormatter : public PresenceFormatter {
public:
    PresenceData format_media(const MediaInfo& media) const override {
        PresenceData data;
        data.details = media.title;
        data.state = "Playing on Plex";
        data.large_image_key = "plex";
        return data;
    }

    void set_show_progress(bool show) override {
        m_show_progress = show;
    }

    void set_show_buttons(bool show) override {
        m_show_buttons = show;
    }

    void set_custom_format(const std::string& format) override {
        m_custom_format = format;
    }

    bool is_progress_shown() const override {
        return m_show_progress;
    }

    bool are_buttons_shown() const override {
        return m_show_buttons;
    }

private:
    bool m_show_progress = true;
    bool m_show_buttons = true;
    std::string m_custom_format;
};

std::unique_ptr<PresenceFormatter> PresenceFormatter::create_default_formatter() {
    return std::make_unique<DefaultPresenceFormatter>();
}

class DefaultPresenceServiceFactory : public PresenceServiceFactory {
public:
    std::unique_ptr<PresenceService> create_service(
        ServiceType type,
        const core::ApplicationConfig& config
    ) override {
        if (type == ServiceType::Discord) {
            DiscordPresenceService::Config discord_config;
            discord_config.client_id = config.discord.application_id;
            return std::make_unique<DiscordPresenceService>(std::move(discord_config));
        }
        return nullptr;
    }
};

std::unique_ptr<PresenceServiceFactory> PresenceServiceFactory::create_default_factory() {
    return std::make_unique<DefaultPresenceServiceFactory>();
}

} // namespace presence_for_plex::services