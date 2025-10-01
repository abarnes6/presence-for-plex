#include "presence_for_plex/core/authentication_service.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/uuid.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <shared_mutex>

namespace presence_for_plex {
namespace core {

class AuthenticationServiceImpl : public AuthenticationService {
public:
    explicit AuthenticationServiceImpl(const std::filesystem::path& storage_path)
        : m_storage_path(storage_path.empty() ? get_default_auth_path() : storage_path) {
        PLEX_LOG_INFO("AuthService", "Initializing authentication service");
        ensure_storage_directory();
        load();
    }

    std::string get_plex_token() const override {
        std::shared_lock lock(m_mutex);
        return m_plex_token;
    }

    void set_plex_token(const std::string& token) override {
        {
            std::unique_lock lock(m_mutex);
            m_plex_token = token;
        }
        save();
    }

    std::string get_plex_client_identifier() const override {
        std::shared_lock lock(m_mutex);
        if (m_plex_client_identifier.empty()) {
            lock.unlock();
            const_cast<AuthenticationServiceImpl*>(this)->generate_client_identifier();
            lock.lock();
        }
        return m_plex_client_identifier;
    }

    std::string get_plex_username() const override {
        std::shared_lock lock(m_mutex);
        return m_plex_username;
    }

    void set_plex_username(const std::string& username) override {
        {
            std::unique_lock lock(m_mutex);
            m_plex_username = username;
        }
        save();
    }

    uint64_t get_discord_client_id() const override {
        std::shared_lock lock(m_mutex);
        return m_discord_client_id;
    }

    void save() override {
        std::shared_lock lock(m_mutex);
        save_internal();
    }

    void load() override {
        std::unique_lock lock(m_mutex);
        load_internal();
    }

private:
    static std::filesystem::path get_default_auth_path() {
        std::filesystem::path auth_dir;

#ifdef _WIN32
        if (const char* app_data = std::getenv("APPDATA")) {
            auth_dir = std::filesystem::path(app_data) / "Presence For Plex";
        }
#else
        if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
            auth_dir = std::filesystem::path(xdg_config) / "presence-for-plex";
        } else if (const char* home = std::getenv("HOME")) {
            auth_dir = std::filesystem::path(home) / ".config" / "presence-for-plex";
        }
#endif

        return auth_dir / "auth.yaml";
    }

    void ensure_storage_directory() {
        auto dir = m_storage_path.parent_path();
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
            PLEX_LOG_DEBUG("AuthService", "Created storage directory: " + dir.string());
        }
    }

    void generate_client_identifier() {
        PLEX_LOG_INFO("AuthService", "Generating new Plex client identifier");
        std::string id = utils::generate_uuid_v4();

        {
            std::unique_lock lock(m_mutex);
            m_plex_client_identifier = id;
        }

        save();
        PLEX_LOG_INFO("AuthService", "Generated client identifier");
    }

    void save_internal() {
        try {
            YAML::Node node;

            // Plex credentials
            if (!m_plex_token.empty()) {
                node["plex"]["auth_token"] = m_plex_token;
            }
            if (!m_plex_client_identifier.empty()) {
                node["plex"]["client_identifier"] = m_plex_client_identifier;
            }
            if (!m_plex_username.empty()) {
                node["plex"]["username"] = m_plex_username;
            }

            // Discord client ID (store as number)
            node["discord"]["client_id"] = m_discord_client_id;

            std::ofstream file(m_storage_path);
            if (!file) {
                PLEX_LOG_ERROR("AuthService", "Failed to open auth file for writing");
                return;
            }

            file << node;
            PLEX_LOG_DEBUG("AuthService", "Saved authentication data");
        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("AuthService", "Error saving auth data: " + std::string(e.what()));
        }
    }

    void load_internal() {
        if (!std::filesystem::exists(m_storage_path)) {
            PLEX_LOG_DEBUG("AuthService", "Auth file does not exist, using defaults");
            return;
        }

        try {
            YAML::Node node = YAML::LoadFile(m_storage_path.string());

            // Load Plex credentials
            if (node["plex"]) {
                auto plex = node["plex"];
                if (plex["auth_token"]) {
                    m_plex_token = plex["auth_token"].as<std::string>();
                }
                if (plex["client_identifier"]) {
                    m_plex_client_identifier = plex["client_identifier"].as<std::string>();
                }
                if (plex["username"]) {
                    m_plex_username = plex["username"].as<std::string>();
                }
            }

            // Load Discord client ID
            if (node["discord"] && node["discord"]["client_id"]) {
                m_discord_client_id = node["discord"]["client_id"].as<uint64_t>();
            }

            PLEX_LOG_DEBUG("AuthService", "Loaded authentication data");
        } catch (const std::exception& e) {
            PLEX_LOG_ERROR("AuthService", "Error loading auth data: " + std::string(e.what()));
        }
    }

private:
    mutable std::shared_mutex m_mutex;
    std::filesystem::path m_storage_path;

    // Plex authentication
    std::string m_plex_token;
    std::string m_plex_client_identifier;
    std::string m_plex_username;

    // Discord authentication - default value
    uint64_t m_discord_client_id = 1359742002618564618ULL;
};

std::unique_ptr<AuthenticationService> AuthenticationService::create(
    const std::filesystem::path& storage_path) {
    return std::make_unique<AuthenticationServiceImpl>(storage_path);
}

} // namespace core
} // namespace presence_for_plex