#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/platform/system_service.hpp"
#include "presence_for_plex/utils/logger.hpp"

#ifdef USE_QT_UI
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#endif

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

namespace {
    std::atomic<bool> g_shutdown_requested{false};
    presence_for_plex::core::Application* g_app_instance = nullptr;

    void handle_shutdown_signal(int signal) {
        PLEX_LOG_INFO("Main", "Shutdown signal received: " + std::to_string(signal));
        g_shutdown_requested = true;

        if (g_app_instance) {
            g_app_instance->quit();
        }

#ifdef USE_QT_UI
        if (auto* qt_app = QApplication::instance()) {
            qt_app->quit();
        }
#endif
    }

    presence_for_plex::utils::LogLevel parse_log_level(const std::string& level_str) {
        using namespace presence_for_plex::utils;

        if (level_str == "debug") return LogLevel::Debug;
        if (level_str == "info") return LogLevel::Info;
        if (level_str == "warning" || level_str == "warn") return LogLevel::Warning;
        if (level_str == "error") return LogLevel::Error;
        if (level_str == "none") return LogLevel::None;
        return LogLevel::Info;
    }

    void register_signal_handlers() {
        std::signal(SIGINT, handle_shutdown_signal);
        std::signal(SIGTERM, handle_shutdown_signal);
#ifdef _WIN32
        std::signal(SIGBREAK, handle_shutdown_signal);
#endif
    }

    std::unique_ptr<presence_for_plex::utils::Logger> setup_logging(const std::string& log_level_str) {
        using namespace presence_for_plex::utils;

        auto log_level = parse_log_level(log_level_str);
        auto logger = std::make_unique<Logger>(log_level);

        logger->add_sink(std::make_unique<ConsoleSink>(true));

        std::filesystem::path log_path;
#ifdef _WIN32
        if (const char* appdata = std::getenv("APPDATA")) {
            log_path = std::filesystem::path(appdata) / "Presence For Plex" / "presence-for-plex.log";
        }
#else
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
            log_path = std::filesystem::path(xdg) / "presence-for-plex" / "presence-for-plex.log";
        } else if (const char* home = std::getenv("HOME")) {
            log_path = std::filesystem::path(home) / ".config" / "presence-for-plex" / "presence-for-plex.log";
        }
#endif

        if (!log_path.empty()) {
            auto file_sink = std::make_unique<FileSink>(log_path, false);
            if (file_sink->is_open()) {
                logger->add_sink(std::move(file_sink));
                std::cerr << "Logging to: " << log_path << std::endl;
            }
        }

        return logger;
    }

    bool check_single_instance() {
        auto single_instance = presence_for_plex::platform::SingleInstanceManager::create("PresenceForPlex");
        auto result = single_instance->try_acquire_instance("PresenceForPlex");

        if (!result || !*result) {
            const std::string message = "Another instance of PresenceForPlex is already running.";
            PLEX_LOG_WARNING("Main", message);

#ifdef USE_QT_UI
            QMessageBox::warning(nullptr, "PresenceForPlex", QString::fromStdString(message));
#else
            std::cerr << message << std::endl;
#endif
            return false;
        }

        PLEX_LOG_INFO("Main", "Single instance lock acquired");
        return true;
    }

#ifdef USE_QT_UI
    void setup_qt_application(QApplication& app) {
        app.setApplicationName("Presence for Plex");
        app.setApplicationDisplayName("Presence for Plex");
        app.setOrganizationName("Andrew Barnes");
        app.setOrganizationDomain("presence-for-plex.github.io");
        app.setQuitOnLastWindowClosed(false);

        QIcon icon(":/icons/app_icon");
        if (icon.isNull()) {
            icon = QIcon("assets/icon.ico");
        }
        app.setWindowIcon(icon);

#ifdef Q_OS_LINUX
        app.setDesktopFileName("presence-for-plex.desktop");
#endif
    }
#endif
} // anonymous namespace

int main(int argc, char* argv[]) {
#ifndef USE_QT_UI
    (void)argc;
    (void)argv;
#endif
#ifdef USE_QT_UI
    QApplication qt_app(argc, argv);
    setup_qt_application(qt_app);
#endif

    auto config_service = presence_for_plex::core::ConfigurationService::create("");
    if (!config_service->load()) {
        std::cerr << "Using default configuration" << std::endl;
    }

    const auto& config = config_service->get();
    auto logger = setup_logging(presence_for_plex::core::to_string(config.log_level));
    presence_for_plex::utils::LoggerManager::set_instance(std::move(logger));

    PLEX_LOG_INFO("Main", "PresenceForPlex v0.4.0 starting...");
    PLEX_LOG_INFO("Main", "Log level: " + presence_for_plex::core::to_string(config.log_level));

    register_signal_handlers();

    try {
        if (!check_single_instance()) {
            return 1;
        }

        auto single_instance = presence_for_plex::platform::SingleInstanceManager::create("PresenceForPlex");
        (void)single_instance->try_acquire_instance("PresenceForPlex");

        PLEX_LOG_INFO("Main", "Creating application...");
        auto app_result = presence_for_plex::core::ApplicationFactory::create_default_application();
        if (!app_result) {
            PLEX_LOG_ERROR("Main", "Application creation failed");
            return 1;
        }

        auto app = std::move(*app_result);
        g_app_instance = app.get();

        if (!app->initialize()) {
            PLEX_LOG_ERROR("Main", "Application initialization failed");
            return 1;
        }

        if (!app->start()) {
            PLEX_LOG_ERROR("Main", "Application start failed");
            return 1;
        }

        std::cout << "\nPresenceForPlex v0.4.0 running\n"
                  << "Press Ctrl+C to exit\n" << std::endl;

#ifdef USE_QT_UI
        QTimer update_timer;
        QObject::connect(&update_timer, &QTimer::timeout, [&app]() {
            if (!g_shutdown_requested && app->is_running()) {
                app->run_once();
            } else {
                QApplication::quit();
            }
        });
        update_timer.start(16);

        qt_app.exec();
#else
        while (!g_shutdown_requested && app->is_running()) {
            app->run_once();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
#endif

        PLEX_LOG_INFO("Main", "Shutting down...");
        app->stop();
        app->shutdown();
        single_instance->release_instance();
        g_app_instance = nullptr;

        PLEX_LOG_INFO("Main", "Shutdown complete");
        return 0;

    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("Main", "Fatal: " + std::string(e.what()));
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        PLEX_LOG_ERROR("Main", "Unknown fatal error");
        std::cerr << "Unknown fatal error" << std::endl;
        return 1;
    }
}