cmake_minimum_required(VERSION 3.25)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(PresenceForPlex VERSION 0.4.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt configuration
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Platform settings
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
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
    src/services/plex/plex.cpp
    src/services/plex/plex_auth.cpp
    src/services/plex/plex_sse.cpp
    src/services/plex/metadata/tmdb.cpp
    src/services/plex/metadata/jikan.cpp
    src/services/discord/discord.cpp
    src/services/discord/discord_ipc.cpp
    src/services/discord/presence_builder.cpp
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
        include/presence_for_plex/platform/qt/qt_settings_dialog.hpp
    )
endif()

set(UTILS_SOURCES
    src/utils/logger.cpp
    src/utils/uuid_generator.cpp
    src/utils/yaml_config.cpp
    src/utils/url_utils.cpp
    src/utils/format_utils.cpp
    src/utils/json_helper.cpp
    src/utils/plex_headers_builder.cpp
)

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
    add_executable(PresenceForPlex MACOSX_BUNDLE ${ALL_SOURCES})
elseif(WIN32)
    add_executable(PresenceForPlex WIN32 ${ALL_SOURCES})
else()
    add_executable(PresenceForPlex ${ALL_SOURCES})
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
    if(TARGET_RUNTIME_DLLS)
        install(FILES $<TARGET_RUNTIME_DLLS:PresenceForPlex>
            DESTINATION bin
            COMPONENT application
        )
    endif()

    # Install Qt plugins and dependencies for Qt-based UI
    if(USE_QT_UI)
        # Deploy Qt dependencies using windeployqt
        get_target_property(_qt6_qmake_location Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt6_bin_dir "${_qt6_qmake_location}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt6_bin_dir}")

        if(WINDEPLOYQT_EXECUTABLE)
            install(CODE "
                execute_process(
                    COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                        --no-translations
                        --no-system-d3d-compiler
                        --no-opengl-sw
                        --no-compiler-runtime
                        --no-quick-import
                        \"\${CMAKE_INSTALL_PREFIX}/bin/PresenceForPlex.exe\"
                )
                # Remove DirectX shader compiler DLLs (only needed for QML/Quick)
                file(REMOVE
                    \"\${CMAKE_INSTALL_PREFIX}/bin/dxcompiler.dll\"
                    \"\${CMAKE_INSTALL_PREFIX}/bin/dxil.dll\"
                )
            " COMPONENT application)
        endif()
    endif()

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
