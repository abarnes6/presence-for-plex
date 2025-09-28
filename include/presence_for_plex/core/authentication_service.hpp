#pragma once

#include <memory>
#include <string>
#include <filesystem>

namespace presence_for_plex {
namespace core {

class AuthenticationService {
public:
    virtual ~AuthenticationService() = default;

    // Plex authentication
    virtual std::string get_plex_token() const = 0;
    virtual void set_plex_token(const std::string& token) = 0;
    virtual std::string get_plex_client_identifier() const = 0;
    virtual std::string get_plex_username() const = 0;
    virtual void set_plex_username(const std::string& username) = 0;

    // Discord authentication
    virtual uint64_t get_discord_client_id() const = 0;

    // Persistence
    virtual void save() = 0;
    virtual void load() = 0;

    // Factory
    static std::unique_ptr<AuthenticationService> create(
        const std::filesystem::path& storage_path = {}
    );
};

} // namespace core
} // namespace presence_for_plex