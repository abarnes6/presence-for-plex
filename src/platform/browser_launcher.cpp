#include "presence_for_plex/platform/browser_launcher.hpp"
#include "presence_for_plex/utils/logger.hpp"
#include <iostream>
#include <cstdlib>
#include <cerrno>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <codecvt>
#include <locale>
#else
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#endif

#ifdef USE_QT_UI
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QApplication>
#include <QMetaObject>
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

#ifdef _WIN32
        // Windows: Use ShellExecuteW to avoid shell injection
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        std::wstring wurl = converter.from_bytes(url);
        
        HINSTANCE result = ShellExecuteW(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)result <= 32) {
            PLEX_LOG_ERROR("NativeBrowserLauncher", "Failed to open URL via ShellExecuteW, error code: " + std::to_string((INT_PTR)result));
            return std::unexpected(BrowserLaunchError::LaunchFailed);
        }
#elif defined(__APPLE__)
        // macOS: Use fork+execlp to avoid shell
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            execlp("open", "open", url.c_str(), (char*)NULL);
            // If execlp returns, it failed
            _exit(1);
        } else if (pid > 0) {
            // Parent process - wait for child
            int status;
            pid_t result;
            do {
                result = waitpid(pid, &status, 0);
            } while (result == -1 && errno == EINTR);
            
            if (result == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                PLEX_LOG_ERROR("NativeBrowserLauncher", "Failed to open URL via 'open' command");
                return std::unexpected(BrowserLaunchError::LaunchFailed);
            }
        } else {
            PLEX_LOG_ERROR("NativeBrowserLauncher", "Failed to fork process for opening URL");
            return std::unexpected(BrowserLaunchError::LaunchFailed);
        }
#else
        // Linux/Unix: Use fork+execlp with xdg-open to avoid shell
        pid_t pid = fork();
        if (pid == 0) {
            // Child process
            execlp("xdg-open", "xdg-open", url.c_str(), (char*)NULL);
            // If execlp returns, it failed
            _exit(1);
        } else if (pid > 0) {
            // Parent process - wait for child
            int status;
            pid_t result;
            do {
                result = waitpid(pid, &status, 0);
            } while (result == -1 && errno == EINTR);
            
            if (result == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                PLEX_LOG_ERROR("NativeBrowserLauncher", "Failed to open URL via xdg-open");
                return std::unexpected(BrowserLaunchError::LaunchFailed);
            }
        } else {
            PLEX_LOG_ERROR("NativeBrowserLauncher", "Failed to fork process for opening URL");
            return std::unexpected(BrowserLaunchError::LaunchFailed);
        }
#endif

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
        QString qtitle = QString::fromStdString(title);
        QString qmessage = QString::fromStdString(message);

        QMetaObject::invokeMethod(QApplication::instance(), [qtitle, qmessage]() {
            QMessageBox::information(nullptr, qtitle, qmessage);
        }, Qt::QueuedConnection);

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