#include "presence_for_plex/services/presence_service.hpp"
#include "presence_for_plex/services/discord_ipc.hpp"
#include "presence_for_plex/services/discord_presence_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>

namespace presence_for_plex {
namespace services {

using json = nlohmann::json;

class SimpleDiscordPresenceService : public PresenceService {
public:
    explicit SimpleDiscordPresenceService(const core::ApplicationConfig& config)
        : m_config(config)
        , m_client_id(config.discord.application_id)
        , m_update_interval(config.discord.update_interval)
        , m_connected(false)
        , m_shutting_down(false) {

        m_ipc = std::make_unique<DiscordIPC>(m_client_id);
        m_formatter = PresenceFormatter::create_default_formatter();
        m_asset_manager = AssetManager::create_default_manager();
    }

    ~SimpleDiscordPresenceService() override {
        shutdown();
    }

    std::expected<void, core::DiscordError> initialize() override {
        PLEX_LOG_INFO("DiscordPresence", "Initializing Discord Rich Presence");

        if (!m_ipc->connect()) {
            std::string error_msg = "Failed to connect to Discord";
            PLEX_LOG_ERROR("DiscordPresence", error_msg);
            on_error_occurred(core::DiscordError::IpcError, error_msg);
            return std::unexpected<core::DiscordError>(core::DiscordError::IpcError);
        }

        m_connected = true;
        on_connection_state_changed(true);

        // Start the update thread
        m_update_thread = std::thread([this]() {
            this->update_loop();
        });

        PLEX_LOG_INFO("DiscordPresence", "Discord Rich Presence initialized successfully");
        return {};
    }

    void shutdown() override {
        if (m_shutting_down) {
            return;
        }

        PLEX_LOG_INFO("DiscordPresence", "Shutting down Discord Rich Presence");

        m_shutting_down = true;
        m_connected = false;

        if (m_update_thread.joinable()) {
            m_update_thread.join();
        }

        if (m_ipc) {
            m_ipc->clear_presence();
            m_ipc->disconnect();
        }

        on_connection_state_changed(false);
    }

    bool is_connected() const override {
        return m_connected && m_ipc && m_ipc->is_connected();
    }

    std::expected<void, core::DiscordError> update_presence(const PresenceData& data) override {
        if (!is_connected()) {
            return std::unexpected<core::DiscordError>(core::DiscordError::NotConnected);
        }

        if (!data.is_valid()) {
            return std::unexpected<core::DiscordError>(core::DiscordError::InvalidPayload);
        }

        {
            std::lock_guard<std::mutex> lock(m_presence_mutex);
            m_current_presence = data;
            m_presence_updated = true;
        }

        return {};
    }

    std::expected<void, core::DiscordError> clear_presence() override {
        if (!is_connected()) {
            return std::unexpected<core::DiscordError>(core::DiscordError::NotConnected);
        }

        {
            std::lock_guard<std::mutex> lock(m_presence_mutex);
            m_current_presence = {};
            m_presence_updated = true;
        }

        return {};
    }

    std::expected<void, core::DiscordError> update_from_media(const core::MediaInfo& media) override {
        if (!m_formatter) {
            return std::unexpected<core::DiscordError>(core::DiscordError::ServiceUnavailable);
        }

        PresenceData presence = m_formatter->format_media(media);
        return update_presence(presence);
    }

    void set_update_callback(PresenceUpdateCallback callback) override {
        m_update_callback = callback;
    }

    void set_error_callback(PresenceErrorCallback callback) override {
        m_error_callback = callback;
    }

    void set_connection_callback(PresenceConnectionStateCallback callback) override {
        m_connection_callback = callback;
    }

    void set_update_interval(std::chrono::seconds interval) override {
        m_update_interval = interval;
    }

    std::chrono::seconds get_update_interval() const override {
        return m_update_interval;
    }

protected:
    void on_presence_updated(const PresenceData& data) override {
        if (m_update_callback) {
            m_update_callback(data);
        }
    }

    void on_connection_state_changed(bool connected) override {
        if (m_connection_callback) {
            m_connection_callback(connected);
        }
    }

    void on_error_occurred(core::DiscordError error, const std::string& message) override {
        PLEX_LOG_ERROR("DiscordPresence", message);
        if (m_error_callback) {
            m_error_callback(error, message);
        }
    }

private:
    core::ApplicationConfig m_config;
    std::string m_client_id;
    std::chrono::seconds m_update_interval;

    std::unique_ptr<DiscordIPC> m_ipc;
    std::unique_ptr<PresenceFormatter> m_formatter;
    std::unique_ptr<AssetManager> m_asset_manager;

    std::atomic<bool> m_connected;
    std::atomic<bool> m_shutting_down;
    std::atomic<bool> m_presence_updated{false};

    std::mutex m_presence_mutex;
    PresenceData m_current_presence;

    std::thread m_update_thread;

    PresenceUpdateCallback m_update_callback;
    PresenceErrorCallback m_error_callback;
    PresenceConnectionStateCallback m_connection_callback;

    void update_loop() {
        PLEX_LOG_DEBUG("DiscordPresence", "Starting update loop");

        while (!m_shutting_down) {
            try {
                // Check if we need to reconnect
                if (!m_ipc->is_connected()) {
                    PLEX_LOG_WARNING("DiscordPresence", "Lost connection to Discord, attempting to reconnect");
                    if (m_ipc->connect()) {
                        m_connected = true;
                        on_connection_state_changed(true);
                    } else {
                        m_connected = false;
                        on_connection_state_changed(false);
                    }
                }

                // Send presence update if needed
                if (m_presence_updated.exchange(false) && m_connected) {
                    std::lock_guard<std::mutex> lock(m_presence_mutex);
                    send_presence_to_discord(m_current_presence);
                }

                // Sleep until next update
                std::this_thread::sleep_for(m_update_interval);

            } catch (const std::exception& e) {
                PLEX_LOG_ERROR("DiscordPresence", "Error in update loop: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }

        PLEX_LOG_DEBUG("DiscordPresence", "Update loop terminated");
    }

    void send_presence_to_discord(const PresenceData& data) {
        json discord_activity;

        if (!data.details.empty() || !data.state.empty()) {
            if (!data.details.empty()) {
                discord_activity["details"] = data.details;
            }
            if (!data.state.empty()) {
                discord_activity["state"] = data.state;
            }

            // Assets
            if (!data.large_image_key.empty() || !data.small_image_key.empty()) {
                json assets;
                if (!data.large_image_key.empty()) {
                    assets["large_image"] = data.large_image_key;
                    if (!data.large_image_text.empty()) {
                        assets["large_text"] = data.large_image_text;
                    }
                }
                if (!data.small_image_key.empty()) {
                    assets["small_image"] = data.small_image_key;
                    if (!data.small_image_text.empty()) {
                        assets["small_text"] = data.small_image_text;
                    }
                }
                discord_activity["assets"] = assets;
            }

            // Timestamps
            if (data.start_timestamp || data.end_timestamp) {
                json timestamps;
                if (data.start_timestamp) {
                    auto epoch = data.start_timestamp->time_since_epoch();
                    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
                    timestamps["start"] = seconds.count();
                }
                if (data.end_timestamp) {
                    auto epoch = data.end_timestamp->time_since_epoch();
                    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
                    timestamps["end"] = seconds.count();
                }
                discord_activity["timestamps"] = timestamps;
            }

            // Buttons
            if (!data.buttons.empty()) {
                json buttons = json::array();
                for (const auto& button : data.buttons) {
                    if (buttons.size() >= 2) break; // Discord limit
                    buttons.push_back({
                        {"label", button.label},
                        {"url", button.url}
                    });
                }
                discord_activity["buttons"] = buttons;
            }

            // Party
            if (data.party) {
                json party;
                party["id"] = data.party->id;
                if (data.party->current_size > 0 && data.party->max_size > 0) {
                    party["size"] = {data.party->current_size, data.party->max_size};
                }
                discord_activity["party"] = party;
            }
        }

        bool success = false;
        if (discord_activity.empty()) {
            success = m_ipc->clear_presence();
        } else {
            success = m_ipc->send_presence(discord_activity);
        }

        if (success) {
            on_presence_updated(data);
            PLEX_LOG_DEBUG("DiscordPresence", "Successfully updated Discord presence");
        } else {
            on_error_occurred(core::DiscordError::IpcError, "Failed to update Discord presence");
        }
    }
};

// Default presence formatter implementation
class DefaultPresenceFormatter : public PresenceFormatter {
public:
    DefaultPresenceFormatter()
        : m_show_progress(true)
        , m_show_buttons(true)
        , m_custom_format() {}

    PresenceData format_media(const core::MediaInfo& media) const override {
        PresenceData data;

        switch (media.type) {
            case core::MediaType::Movie:
                format_movie(media, data);
                break;
            case core::MediaType::TVShow:
                format_tv_show(media, data);
                break;
            case core::MediaType::Music:
                format_music(media, data);
                break;
            default:
                format_generic(media, data);
                break;
        }

        // Add timestamps for progress
        if (m_show_progress && media.state == core::PlaybackState::Playing) {
            auto now = std::chrono::system_clock::now();
            data.start_timestamp = now - std::chrono::seconds(static_cast<int64_t>(media.progress));

            if (media.duration > 0) {
                auto remaining = media.duration - media.progress;
                data.end_timestamp = now + std::chrono::seconds(static_cast<int64_t>(remaining));
            }
        }

        // Add buttons - prioritize MAL for anime content
        if (m_show_buttons) {
            if (!media.mal_id.empty()) {
                data.buttons.push_back({
                    "View on MyAnimeList",
                    "https://myanimelist.net/anime/" + media.mal_id
                });
            } else if (!media.imdb_id.empty()) {
                data.buttons.push_back({
                    "View on IMDb",
                    "https://www.imdb.com/title/" + media.imdb_id
                });
            }
        }

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
    bool m_show_progress;
    bool m_show_buttons;
    std::string m_custom_format;

    void format_movie(const core::MediaInfo& media, PresenceData& data) const {
        data.details = media.title;
        if (media.year > 0) {
            data.details += " (" + std::to_string(media.year) + ")";
        }

        switch (media.state) {
            case core::PlaybackState::Playing:
                data.state = "Watching a movie";
                data.small_image_key = "play";
                data.small_image_text = "Playing";
                break;
            case core::PlaybackState::Paused:
                data.state = "Paused";
                data.small_image_key = "pause";
                data.small_image_text = "Paused";
                break;
            default:
                data.state = "Movie";
                break;
        }

        data.large_image_key = "plex";
        data.large_image_text = "Plex";
    }

    void format_tv_show(const core::MediaInfo& media, PresenceData& data) const {
        if (!media.grandparent_title.empty()) {
            data.details = media.grandparent_title;
        } else {
            data.details = media.title;
        }

        if (media.season > 0 && media.episode > 0) {
            data.state = "S" + std::to_string(media.season) + "E" + std::to_string(media.episode);
            if (!media.title.empty() && media.title != media.grandparent_title) {
                data.state += " - " + media.title;
            }
        } else {
            data.state = media.title;
        }

        switch (media.state) {
            case core::PlaybackState::Playing:
                data.small_image_key = "play";
                data.small_image_text = "Playing";
                break;
            case core::PlaybackState::Paused:
                data.small_image_key = "pause";
                data.small_image_text = "Paused";
                break;
            default:
                break;
        }

        data.large_image_key = "plex";
        data.large_image_text = "Plex";
    }

    void format_music(const core::MediaInfo& media, PresenceData& data) const {
        data.details = media.title;

        if (!media.artist.empty()) {
            data.state = "by " + media.artist;
            if (!media.album.empty()) {
                data.state += " - " + media.album;
            }
        } else if (!media.album.empty()) {
            data.state = media.album;
        }

        switch (media.state) {
            case core::PlaybackState::Playing:
                data.small_image_key = "play";
                data.small_image_text = "Playing";
                break;
            case core::PlaybackState::Paused:
                data.small_image_key = "pause";
                data.small_image_text = "Paused";
                break;
            default:
                break;
        }

        data.large_image_key = "plex";
        data.large_image_text = "Plex";
    }

    void format_generic(const core::MediaInfo& media, PresenceData& data) const {
        data.details = media.title;
        data.state = "Using Plex";
        data.large_image_key = "plex";
        data.large_image_text = "Plex";
    }
};

// Default asset manager implementation
class DefaultAssetManager : public AssetManager {
public:
    DefaultAssetManager() {
        // Register built-in assets
        m_assets["plex"] = "plex";
        m_assets["play"] = "play";
        m_assets["pause"] = "pause";
        m_assets["stop"] = "stop";
    }

    std::expected<std::string, core::DiscordError> get_asset_key(const std::string& asset_name) const override {
        auto it = m_assets.find(asset_name);
        if (it != m_assets.end()) {
            return it->second;
        }
        return std::unexpected<core::DiscordError>(core::DiscordError::ServiceUnavailable);
    }

    std::expected<void, core::DiscordError> register_asset(const std::string& name, const std::string& key) override {
        m_assets[name] = key;
        return {};
    }

    void clear_assets() override {
        m_assets.clear();
    }

    std::string get_default_large_image() const override {
        return "plex";
    }

    std::string get_play_icon() const override {
        return "play";
    }

    std::string get_pause_icon() const override {
        return "pause";
    }

    std::string get_stop_icon() const override {
        return "stop";
    }

private:
    std::map<std::string, std::string> m_assets;
};

// Factory implementations
class DefaultPresenceServiceFactory : public PresenceServiceFactory {
public:
    std::unique_ptr<PresenceService> create_service(
        ServiceType type,
        const core::ApplicationConfig& config) override {
        switch (type) {
            case ServiceType::Discord:
                return std::make_unique<SimpleDiscordPresenceService>(config);
            case ServiceType::Slack:
            case ServiceType::Teams:
                // Not implemented
                return nullptr;
            default:
                return nullptr;
        }
    }
};

std::unique_ptr<PresenceServiceFactory> PresenceServiceFactory::create_default_factory() {
    return std::make_unique<DefaultPresenceServiceFactory>();
}

class RobustPresenceServiceFactory : public PresenceServiceFactory {
public:
    std::unique_ptr<PresenceService> create_service(
        ServiceType type,
        const core::ApplicationConfig& config) override {
        switch (type) {
            case ServiceType::Discord:
                return DiscordPresenceServiceFactory::create_service(config);
            case ServiceType::Slack:
            case ServiceType::Teams:
                // Not implemented
                return nullptr;
            default:
                return nullptr;
        }
    }
};

std::unique_ptr<PresenceServiceFactory> PresenceServiceFactory::create_enhanced_factory() {
    return std::make_unique<RobustPresenceServiceFactory>();
}

std::unique_ptr<PresenceFormatter> PresenceFormatter::create_default_formatter() {
    return std::make_unique<DefaultPresenceFormatter>();
}

std::unique_ptr<AssetManager> AssetManager::create_default_manager() {
    return std::make_unique<DefaultAssetManager>();
}

// Implementation for PresenceData validation
bool PresenceData::is_valid() const {
    // At least one of these should be present for a valid presence
    return !details.empty() || !state.empty() || !large_image_key.empty();
}

} // namespace services
} // namespace presence_for_plex
