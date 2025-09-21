#include "presence_for_plex/utils/logger.hpp"
#include "presence_for_plex/platform/system_service.hpp"
#include "presence_for_plex/core/application.hpp"
#include "presence_for_plex/platform/ui_service.hpp"

#ifdef USE_QT_UI
#include "presence_for_plex/platform/qt/qt_ui_service.hpp"
#include <QApplication>
#include <QMessageBox>
#include <QIcon>
#endif

#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <filesystem>

// Global application instance for signal handling
std::atomic<bool> g_shutdown_requested{false};
presence_for_plex::core::Application* g_app_instance = nullptr;

void signal_handler(int signal) {
    PLEX_LOG_INFO("Main", "Received signal " + std::to_string(signal) + ", requesting shutdown...");
    g_shutdown_requested.store(true);

    // If we have an app instance, tell it to quit immediately
    if (g_app_instance) {
        g_app_instance->quit();
    }
}

// Helper function to convert string log level to LogLevel enum
presence_for_plex::utils::LogLevel parse_log_level(const std::string& level_str) {
    if (level_str == "debug") return presence_for_plex::utils::LogLevel::Debug;
    if (level_str == "info") return presence_for_plex::utils::LogLevel::Info;
    if (level_str == "warning" || level_str == "warn") return presence_for_plex::utils::LogLevel::Warning;
    if (level_str == "error") return presence_for_plex::utils::LogLevel::Error;
    if (level_str == "none") return presence_for_plex::utils::LogLevel::None;
    return presence_for_plex::utils::LogLevel::Info; // Default fallback
}

int main(int argc, char *argv[]) {
    // Suppress unused parameter warnings when not using Qt
#ifndef USE_QT_UI
    (void)argc;
    (void)argv;
#endif
#ifdef USE_QT_UI
    // Create QApplication early for Qt-based operations
    QApplication qt_app(argc, argv);
    qt_app.setApplicationName("Presence for Plex");
    qt_app.setApplicationDisplayName("Presence for Plex");
    qt_app.setOrganizationName("Andrew Barnes");
    qt_app.setOrganizationDomain("presence-for-plex.github.io");
    qt_app.setQuitOnLastWindowClosed(false);

    // Set the application icon
    QIcon appIcon(":/icons/app_icon");
    if (appIcon.isNull()) {
        // Fallback to file system path if resource not found
        appIcon = QIcon("assets/icon.ico");
    }
    qt_app.setWindowIcon(appIcon);

#ifdef Q_OS_LINUX
    qt_app.setDesktopFileName("presence-for-plex.desktop");
#endif
#endif
    // Initialize configuration service first to get log level
    auto config_service = presence_for_plex::core::ConfigurationService::create("");
    auto config_load_result = config_service->load_configuration();

    if (!config_load_result) {
        std::cerr << "Failed to load configuration, using defaults" << std::endl;
    }

    const auto& config = config_service->get_config();
    auto log_level = presence_for_plex::utils::LogLevel::Debug;

    // Initialize logging with configured level
    auto logger = std::make_unique<presence_for_plex::utils::Logger>(log_level);

    // Add console sink
    logger->add_sink(
        std::make_unique<presence_for_plex::utils::ConsoleSink>(true)
    );

    // Always add file sink to config directory
    std::filesystem::path log_file_path;
#ifdef _WIN32
    // Windows: %APPDATA%/Presence For Plex/presence-for-plex.log
    if (const char* app_data = std::getenv("APPDATA")) {
        log_file_path = std::filesystem::path(app_data) / "Presence For Plex" / "presence-for-plex.log";
    }
#else
    // Unix/Linux/macOS: $XDG_CONFIG_HOME/presence-for-plex/presence-for-plex.log or ~/.config/presence-for-plex/presence-for-plex.log
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
        log_file_path = std::filesystem::path(xdg_config) / "presence-for-plex" / "presence-for-plex.log";
    } else if (const char* home = std::getenv("HOME")) {
        log_file_path = std::filesystem::path(home) / ".config" / "presence-for-plex" / "presence-for-plex.log";
    }
#endif

    if (!log_file_path.empty()) {
        auto file_sink = std::make_unique<presence_for_plex::utils::FileSink>(log_file_path, false);
        if (file_sink->is_open()) {
            logger->add_sink(std::move(file_sink));
            std::cerr << "File logging enabled: " << log_file_path << std::endl;
        } else {
            std::cerr << "Failed to open log file: " << log_file_path << std::endl;
        }
    }

    // Set as global logger
    presence_for_plex::utils::LoggerManager::set_instance(std::move(logger));

    PLEX_LOG_INFO("Main", "PresenceForPlex v0.4.0 starting up...");
    PLEX_LOG_INFO("Main", "Configuration loaded, log level: " + config.log_level);

    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifdef _WIN32
    std::signal(SIGBREAK, signal_handler);
#endif

    try {
        // Check for another instance
        auto single_instance = presence_for_plex::platform::SingleInstanceManager::create("PresenceForPlex");
        auto instance_result = single_instance->try_acquire_instance("PresenceForPlex");

        if (!instance_result || !*instance_result) {
            std::string message = "Another instance of PresenceForPlex is already running.";
            PLEX_LOG_WARNING("Main", message);

#ifdef USE_QT_UI
            // Use Qt for notifications when available
            QMessageBox::warning(nullptr, "PresenceForPlex", QString::fromStdString(message));
#else
            std::cerr << message << std::endl;
#endif
            return 1;
        }

        PLEX_LOG_INFO("Main", "Successfully acquired single instance lock");

        // Create and initialize the application
        PLEX_LOG_INFO("Main", "Creating application instance...");
        auto app_result = presence_for_plex::core::ApplicationFactory::create_default_application();

        if (!app_result) {
            PLEX_LOG_ERROR("Main", "Failed to create application");
            return 1;
        }

        auto app = std::move(*app_result);

        // Set global app instance for signal handling
        g_app_instance = app.get();

        // Initialize the application
        PLEX_LOG_INFO("Main", "Initializing application...");
        auto init_result = app->initialize();
        if (!init_result) {
            PLEX_LOG_ERROR("Main", "Failed to initialize application");
            return 1;
        }

        // Start the application
        PLEX_LOG_INFO("Main", "Starting application...");
        auto start_result = app->start();
        if (!start_result) {
            PLEX_LOG_ERROR("Main", "Failed to start application");
            return 1;
        }

        PLEX_LOG_INFO("Main", "PresenceForPlex v0.4.0 is running!");
        std::cout << "PresenceForPlex v0.4.0 is running!" << std::endl;
        std::cout << "Press Ctrl+C to exit." << std::endl;

        // Main event loop
        while (!g_shutdown_requested.load() && app->is_running()) {
#ifdef USE_QT_UI
            // Process Qt events
            qt_app.processEvents();
#endif
            // Run one iteration of the application's event loop
            app->run_once();
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Faster response
        }

        PLEX_LOG_INFO("Main", "Shutdown requested, stopping application...");

        // Stop the application gracefully
        app->stop();
        app->shutdown();


        // Release single instance lock
        single_instance->release_instance();

        // Clear global app instance
        g_app_instance = nullptr;

        PLEX_LOG_INFO("Main", "PresenceForPlex shutdown complete");
        return 0;

    } catch (const std::exception& e) {
        PLEX_LOG_ERROR("Main", "Fatal error: " + std::string(e.what()));
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        PLEX_LOG_ERROR("Main", "Unknown fatal error occurred");
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return 1;
    }
}