cmake_minimum_required(VERSION 3.25)

# Project configuration
project(PresenceForPlex VERSION 0.4.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable modern CMake features
set(CMAKE_SUPPRESS_REGENERATION true)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE CACHE BOOL "Build with install RPATH")

# Enable Hot Reload for MSVC compilers if supported
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# Options
option(BUILD_TESTING "Build tests" OFF)
option(USE_DYNAMIC_LINKS "Use system libraries instead of static linking" OFF)
option(ENABLE_SANITIZERS "Enable sanitizers in debug builds" OFF)

# Qt configuration (always enabled for UI)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Platform-specific Qt settings
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
    set(MACOSX_BUNDLE TRUE)
elseif(LINUX)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

# Include helper modules
include(cmake/compiler_flags.cmake)
include(cmake/dependencies.cmake)

# Configure version header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/version.h"
    @ONLY
)

# Source files organization
set(CORE_SOURCES
    src/core/application/application.cpp
    src/core/configuration/config_manager.cpp
    src/core/models/model_validation.cpp
)

set(SERVICES_SOURCES
    src/services/plex/plex_authenticator.cpp
    src/services/plex/plex_service_impl.cpp
    src/services/plex/plex_cache_manager.cpp
    src/services/plex/plex_connection_manager.cpp
    src/services/plex/plex_media_fetcher.cpp
    src/services/plex/plex_session_manager.cpp
    src/services/discord/discord_client.cpp
    src/services/discord/discord_ipc.cpp
    src/services/discord/discord_presence_manager.cpp
    # Discord services with robustness features
    src/services/discord/rate_limiter.cpp
    src/services/discord/connection_manager.cpp
    src/services/discord/discord_connection_strategy.cpp
    src/services/discord/discord_presence_service.cpp
    src/services/network/http_client.cpp
    src/services/network/curl_http_client.cpp
    src/services/network/sse_client.cpp
)

set(PLATFORM_SOURCES
    src/platform/common/single_instance.cpp
    src/platform/ui_service_factory.cpp
    src/platform/browser_launcher.cpp
)

# Qt-based UI implementation (if Qt is available)
if(USE_QT_UI)
    list(APPEND PLATFORM_SOURCES
        src/platform/qt/qt_ui_service.cpp
        src/platform/qt/qt_system_tray.cpp
        src/platform/qt/qt_notification_manager.cpp
        src/platform/qt/qt_dialog_manager.cpp
        src/platform/qt/qt_window_manager.cpp
    )
endif()

set(UTILS_SOURCES
    src/utils/logging/logger.cpp
    src/utils/threading/thread_pool.cpp
    src/utils/crypto/uuid_generator.cpp
)

# Collect all headers
file(GLOB_RECURSE HEADERS "include/presence_for_plex/*.hpp")

# Main executable sources
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${SERVICES_SOURCES}
    ${PLATFORM_SOURCES}
    ${UTILS_SOURCES}
    src/main.cpp
)

# Add Qt resource files if Qt is available
if(USE_QT_UI)
    qt6_add_resources(QT_RESOURCES assets/resources.qrc)
    list(APPEND ALL_SOURCES ${QT_RESOURCES})
endif()

# Create the main executable
if(APPLE)
    add_executable(PresenceForPlex MACOSX_BUNDLE ${ALL_SOURCES} ${HEADERS})
else()
    add_executable(PresenceForPlex ${ALL_SOURCES} ${HEADERS})
endif()

# Include directories
target_include_directories(PresenceForPlex PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

# Link libraries
target_link_libraries(PresenceForPlex PRIVATE
    CURL::libcurl
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
)

# Link Qt libraries if available
if(USE_QT_UI)
    target_link_libraries(PresenceForPlex PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
    )
    target_compile_definitions(PresenceForPlex PRIVATE USE_QT_UI)
endif()

# Platform-specific linking
if(WIN32)
    target_link_libraries(PresenceForPlex PRIVATE
        ws2_32
        crypt32
        wldap32
        advapi32
    )
elseif(APPLE)
    target_link_libraries(PresenceForPlex PRIVATE
        "-framework Foundation"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework Security"
    )
else()
    target_link_libraries(PresenceForPlex PRIVATE
        pthread
        dl
    )
endif()

# Compiler-specific settings
apply_compiler_flags(PresenceForPlex)

# Installation
install(TARGETS PresenceForPlex
    RUNTIME DESTINATION .
)

install(FILES LICENSE README.md
    DESTINATION .
)

if(WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:PresenceForPlex>
        DESTINATION .
    )
endif()

# Packaging
include(CPack)

# Platform-specific packaging
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")

    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        CreateShortCut \\\"$DESKTOP\\\\Presence For Plex.lnk\\\" \\\"$INSTDIR\\\\PresenceForPlex.exe\\\"
        CreateDirectory \\\"$SMPROGRAMS\\\\Presence For Plex\\\"
        CreateShortCut \\\"$SMPROGRAMS\\\\Presence For Plex\\\\Presence For Plex.lnk\\\" \\\"$INSTDIR\\\\PresenceForPlex.exe\\\"
        WriteRegStr HKCU \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run\\\" \\\"PresenceForPlex\\\" \\\"$INSTDIR\\\\PresenceForPlex.exe\\\"
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        Delete \\\"$DESKTOP\\\\Presence For Plex.lnk\\\"
        Delete \\\"$SMPROGRAMS\\\\Presence For Plex\\\\Presence For Plex.lnk\\\"
        RMDir \\\"$SMPROGRAMS\\\\Presence For Plex\\\"
        DeleteRegValue HKCU \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run\\\" \\\"PresenceForPlex\\\"
    ")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Presence For Plex")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "PresenceForPlex")
else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "PresenceForPlex")
    set(CPACK_SET_DESTDIR ON)
    set(CPACK_INSTALL_PREFIX "/usr")
endif()

set(CPACK_PACKAGE_NAME "PresenceForPlex")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Andrew Barnes")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Discord Rich Presence for Plex")
set(CPACK_PACKAGE_EXECUTABLES "PresenceForPlex" "Presence For Plex")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")


# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
