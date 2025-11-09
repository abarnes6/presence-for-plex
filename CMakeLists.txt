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
    src/core/application.cpp
    src/core/config_manager.cpp
)

set(SERVICES_SOURCES
    src/services/update_service.cpp
    src/services/plex/plex_auth_storage.cpp
    src/services/plex/plex_authenticator.cpp
    src/services/plex/plex_service.cpp
    src/services/plex/plex_client.cpp
    src/services/plex/plex_connection_manager.cpp
    src/services/discord/discord_ipc.cpp
    src/services/discord/rate_limiter.cpp
    src/services/discord/connection_manager.cpp
    src/services/discord/discord_presence_service.cpp
    src/services/network/http_client.cpp
    src/services/network/curl_http_client.cpp
    src/services/network/sse_client.cpp
)

set(PLATFORM_SOURCES
    src/platform/common/single_instance.cpp
    src/platform/ui_service.cpp
    src/platform/browser_launcher.cpp
)

# Qt-based UI implementation (if Qt is available)
if(USE_QT_UI)
    list(APPEND PLATFORM_SOURCES
        src/platform/qt/qt_ui_service.cpp
        src/platform/qt/qt_system_tray.cpp
        src/platform/qt/qt_settings_dialog.cpp
    )
endif()

set(UTILS_SOURCES
    src/utils/logger.cpp
    src/utils/uuid_generator.cpp
    src/utils/yaml_config.cpp
    src/utils/url_utils.cpp
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

# Add Windows resource file for application icon
if(WIN32)
    list(APPEND ALL_SOURCES assets/resources.rc)
endif()

# Create the main executable
if(APPLE)
    add_executable(PresenceForPlex MACOSX_BUNDLE ${ALL_SOURCES} ${HEADERS})
elseif(WIN32)
    add_executable(PresenceForPlex WIN32 ${ALL_SOURCES} ${HEADERS})
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

# Installation rules
if(WIN32)
    # Windows: Install to program files
    install(TARGETS PresenceForPlex
        RUNTIME DESTINATION bin
        COMPONENT application
    )

    # Install runtime DLLs (Qt, MSVC runtime, etc.)
    install(FILES $<TARGET_RUNTIME_DLLS:PresenceForPlex>
        DESTINATION bin
        COMPONENT application
    )

    # Install documentation
    install(FILES LICENSE README.md
        DESTINATION .
        COMPONENT application
    )

elseif(APPLE)
    # macOS: Bundle installation
    install(TARGETS PresenceForPlex
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
        COMPONENT application
    )

    # Install documentation
    install(FILES LICENSE README.md
        DESTINATION .
        COMPONENT application
    )

else()
    # Linux: Install to standard locations
    install(TARGETS PresenceForPlex
        RUNTIME DESTINATION bin
        COMPONENT application
    )

    # Install desktop file
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/presence-for-plex.desktop
        DESTINATION share/applications
        COMPONENT desktop
    )

    # Install icon (convert .ico to .png if needed, or use a proper icon)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME presence-for-plex.png
            COMPONENT desktop
        )
    endif()

    # Install documentation
    install(FILES LICENSE README.md
        DESTINATION share/doc/presence-for-plex
        COMPONENT application
    )
endif()

# Packaging configuration
include(cmake/packaging.cmake)


# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
