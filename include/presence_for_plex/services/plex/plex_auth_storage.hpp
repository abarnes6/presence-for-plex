#pragma once

#include <memory>
#include <string>
#include <filesystem>
#include <shared_mutex>

namespace presence_for_plex {
namespace services {

class PlexAuthStorage {
public:
    explicit PlexAuthStorage(const std::filesystem::path& storage_path = {});
    ~PlexAuthStorage() = default;

    // Plex authentication
    std::string get_plex_token() const;
    void set_plex_token(const std::string& token);
    std::string get_plex_client_identifier() const;
    std::string get_plex_username() const;
    void set_plex_username(const std::string& username);

    // Persistence
    void save();
    void load();

private:
    static std::filesystem::path get_default_auth_path();
    void ensure_storage_directory();
    void generate_client_identifier();
    void save_internal();
    void load_internal();

    mutable std::shared_mutex m_mutex;
    std::filesystem::path m_storage_path;

    // Plex authentication
    std::string m_plex_token;
    std::string m_plex_client_identifier;
    std::string m_plex_username;
};

} // namespace services
} // namespace presence_for_plex
