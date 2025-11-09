#include "presence_for_plex/services/plex/plex_auth_storage.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/utils/uuid.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>

namespace presence_for_plex {
namespace services {

PlexAuthStorage::PlexAuthStorage(const std::filesystem::path& storage_path)
    : m_storage_path(storage_path.empty() ? get_default_auth_path() : storage_path) {
    LOG_DEBUG("PlexAuthStorage", "Initializing authentication storage");
    ensure_storage_directory();
    load();
}

std::string PlexAuthStorage::get_plex_token() const {
    std::shared_lock lock(m_mutex);
    return m_plex_token;
}

void PlexAuthStorage::set_plex_token(const std::string& token) {
    {
        std::unique_lock lock(m_mutex);
        m_plex_token = token;
    }
    save();
}

std::string PlexAuthStorage::get_plex_client_identifier() const {
    std::shared_lock lock(m_mutex);
    if (m_plex_client_identifier.empty()) {
        lock.unlock();
        const_cast<PlexAuthStorage*>(this)->generate_client_identifier();
        lock.lock();
    }
    return m_plex_client_identifier;
}

std::string PlexAuthStorage::get_plex_username() const {
    std::shared_lock lock(m_mutex);
    return m_plex_username;
}

void PlexAuthStorage::set_plex_username(const std::string& username) {
    {
        std::unique_lock lock(m_mutex);
        m_plex_username = username;
    }
    save();
}

void PlexAuthStorage::save() {
    std::shared_lock lock(m_mutex);
    save_internal();
}

void PlexAuthStorage::load() {
    std::unique_lock lock(m_mutex);
    load_internal();
}

std::filesystem::path PlexAuthStorage::get_default_auth_path() {
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

void PlexAuthStorage::ensure_storage_directory() {
    auto dir = m_storage_path.parent_path();
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
        LOG_DEBUG("PlexAuthStorage", "Created storage directory: " + dir.string());
    }
}

void PlexAuthStorage::generate_client_identifier() {
    LOG_INFO("PlexAuthStorage", "Generating new Plex client identifier");
    std::string id = utils::generate_uuid_v4();

    {
        std::unique_lock lock(m_mutex);
        m_plex_client_identifier = id;
    }

    save();
    LOG_INFO("PlexAuthStorage", "Generated client identifier");
}

void PlexAuthStorage::save_internal() {
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

        std::ofstream file(m_storage_path);
        if (!file) {
            LOG_ERROR("PlexAuthStorage", "Failed to open auth file for writing");
            return;
        }

        file << node;
        LOG_DEBUG("PlexAuthStorage", "Saved authentication data");
    } catch (const std::exception& e) {
        LOG_ERROR("PlexAuthStorage", "Error saving auth data: " + std::string(e.what()));
    }
}

void PlexAuthStorage::load_internal() {
    if (!std::filesystem::exists(m_storage_path)) {
        LOG_DEBUG("PlexAuthStorage", "Auth file does not exist, using defaults");
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

        LOG_DEBUG("PlexAuthStorage", "Loaded authentication data");
    } catch (const std::exception& e) {
        LOG_ERROR("PlexAuthStorage", "Error loading auth data: " + std::string(e.what()));
    }
}

} // namespace services
} // namespace presence_for_plex
