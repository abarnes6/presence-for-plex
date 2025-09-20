#pragma once

#include <string>
#include <expected>
#include <memory>

namespace presence_for_plex::platform {

enum class BrowserLaunchError {
    NotSupported,
    LaunchFailed,
    InvalidUrl
};

// Interface for launching URLs in the system browser
class BrowserLauncher {
public:
    virtual ~BrowserLauncher() = default;

    // Open a URL in the default system browser
    virtual std::expected<void, BrowserLaunchError> open_url(const std::string& url) = 0;

    // Show a message to the user (optional, can return false if not supported)
    virtual bool show_message(const std::string& title, const std::string& message) = 0;
};

// Factory function to create the appropriate browser launcher
std::unique_ptr<BrowserLauncher> create_browser_launcher();

} // namespace presence_for_plex::platform