#include "presence_for_plex/platform/browser_launcher.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <iostream>
#include <cstdlib>

#ifdef USE_QT_UI
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#endif

namespace presence_for_plex::platform {

// Native implementation using system commands
class NativeBrowserLauncher : public BrowserLauncher {
public:
    std::expected<void, BrowserLaunchError> open_url(const std::string& url) override {
        if (url.empty()) {
            return std::unexpected(BrowserLaunchError::InvalidUrl);
        }

        PLEX_LOG_INFO("NativeBrowserLauncher", "Opening URL: " + url);

        std::string cmd;
#ifdef _WIN32
        cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
        cmd = "open \"" + url + "\"";
#else
        // Linux/Unix
        cmd = "xdg-open \"" + url + "\" 2>/dev/null";
#endif

        int result = std::system(cmd.c_str());
        if (result != 0) {
            PLEX_LOG_ERROR("NativeBrowserLauncher", "Failed to open URL, command returned: " + std::to_string(result));
            return std::unexpected(BrowserLaunchError::LaunchFailed);
        }

        return {};
    }

    bool show_message(const std::string& title, const std::string& message) override {
        // Native implementation just prints to console
        std::cout << "\n=== " << title << " ===" << std::endl;
        std::cout << message << std::endl;
        std::cout << "=================" << std::endl;
        return true;
    }
};

#ifdef USE_QT_UI
// Qt implementation using QDesktopServices
class QtBrowserLauncher : public BrowserLauncher {
public:
    std::expected<void, BrowserLaunchError> open_url(const std::string& url) override {
        if (url.empty()) {
            return std::unexpected(BrowserLaunchError::InvalidUrl);
        }

        PLEX_LOG_INFO("QtBrowserLauncher", "Opening URL: " + url);

        bool success = QDesktopServices::openUrl(QUrl(QString::fromStdString(url)));
        if (!success) {
            PLEX_LOG_ERROR("QtBrowserLauncher", "Failed to open URL via QDesktopServices");
            return std::unexpected(BrowserLaunchError::LaunchFailed);
        }

        return {};
    }

    bool show_message(const std::string& title, const std::string& message) override {
        QMessageBox::information(nullptr,
                                QString::fromStdString(title),
                                QString::fromStdString(message));
        return true;
    }
};
#endif

// Factory function
std::unique_ptr<BrowserLauncher> create_browser_launcher() {
#ifdef USE_QT_UI
    // Prefer Qt implementation when available
    return std::make_unique<QtBrowserLauncher>();
#else
    // Fall back to native implementation
    return std::make_unique<NativeBrowserLauncher>();
#endif
}

} // namespace presence_for_plex::platform