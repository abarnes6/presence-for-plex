# Comprehensive packaging configuration for PresenceForPlex
# Supports Windows (NSIS/ZIP), macOS (DMG/ZIP), Linux (DEB/RPM/AppImage/TGZ)

# Common package metadata
set(CPACK_PACKAGE_NAME "PresenceForPlex")
set(CPACK_PACKAGE_VENDOR "Andrew Barnes")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Discord Rich Presence for Plex")
set(CPACK_PACKAGE_DESCRIPTION "A lightweight application that displays your current Plex media activity in Discord's Rich Presence")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/abarnes6/presence-for-plex")
set(CPACK_PACKAGE_CONTACT "Andrew Barnes <andrew@example.com>")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Presence For Plex")
set(CPACK_PACKAGE_EXECUTABLES "PresenceForPlex" "Presence For Plex")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# Source package configuration
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\.git/"
    "/\\.github/"
    "/\\.vscode/"
    "/\\.claude/"
    "/build/"
    "\\.user$"
    "\\.swp$"
    "~$"
)

# ============================================================================
# Windows Packaging (NSIS Installer + Portable ZIP)
# ============================================================================
if(WIN32)
    # Primary installer format
    set(CPACK_GENERATOR "NSIS;ZIP")

    # NSIS-specific configuration
    set(CPACK_NSIS_PACKAGE_NAME "Presence For Plex")
    set(CPACK_NSIS_DISPLAY_NAME "Presence For Plex ${PROJECT_VERSION}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\PresenceForPlex.exe")
    set(CPACK_NSIS_HELP_LINK "https://github.com/abarnes6/presence-for-plex")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/abarnes6/presence-for-plex")
    set(CPACK_NSIS_CONTACT "andrew@example.com")
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

    # Icons
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
        set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
        set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    endif()

    # Installation commands - Create shortcuts and optional auto-start
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        ; Create desktop shortcut
        CreateShortCut \\\"$DESKTOP\\\\Presence For Plex.lnk\\\" \\\"$INSTDIR\\\\bin\\\\PresenceForPlex.exe\\\"

        ; Create start menu folder and shortcut
        CreateDirectory \\\"$SMPROGRAMS\\\\Presence For Plex\\\"
        CreateShortCut \\\"$SMPROGRAMS\\\\Presence For Plex\\\\Presence For Plex.lnk\\\" \\\"$INSTDIR\\\\bin\\\\PresenceForPlex.exe\\\"
        CreateShortCut \\\"$SMPROGRAMS\\\\Presence For Plex\\\\Uninstall.lnk\\\" \\\"$INSTDIR\\\\Uninstall.exe\\\"

        ; Optional: Add to startup (user can disable via Windows settings)
        ; Note: Auto-start is handled by the application's config, not forced by installer
    ")

    # Uninstallation commands
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        ; Remove shortcuts
        Delete \\\"$DESKTOP\\\\Presence For Plex.lnk\\\"
        Delete \\\"$SMPROGRAMS\\\\Presence For Plex\\\\Presence For Plex.lnk\\\"
        Delete \\\"$SMPROGRAMS\\\\Presence For Plex\\\\Uninstall.lnk\\\"
        RMDir \\\"$SMPROGRAMS\\\\Presence For Plex\\\"

        ; Remove auto-start registry key if exists
        DeleteRegValue HKCU \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run\\\" \\\"PresenceForPlex\\\"

        ; Note: We intentionally don't delete user config files in AppData
        ; Users may want to keep their settings for future reinstalls
    ")

    # Monolithic installation (no component selection UI)
    set(CPACK_NSIS_COMPONENT_INSTALL OFF)
    set(CPACK_MONOLITHIC_INSTALL ON)

    # ZIP archive naming
    set(CPACK_ARCHIVE_FILE_NAME "PresenceForPlex-${PROJECT_VERSION}-win64-portable")

# ============================================================================
# macOS Packaging (DMG + ZIP)
# ============================================================================
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;ZIP")

    # DMG configuration
    set(CPACK_DMG_VOLUME_NAME "Presence For Plex ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT "UDZO")  # Compressed
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/packaging/macos_dmg_setup.scpt")

    # Background image (if available)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/dmg_background.png")
        set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/assets/dmg_background.png")
    endif()

    # Application icon (already set in bundle)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.icns")
        set(CPACK_BUNDLE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.icns")
    endif()

    # Bundle configuration
    set(CPACK_BUNDLE_NAME "Presence For Plex")
    set(CPACK_BUNDLE_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")

    # Create symbolic link to /Applications in DMG
    set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE ON)

    # ZIP archive naming
    set(CPACK_ARCHIVE_FILE_NAME "PresenceForPlex-${PROJECT_VERSION}-macos")

# ============================================================================
# Linux Packaging (DEB, RPM, AppImage, TGZ)
# ============================================================================
else()
    # Generate multiple package formats
    set(CPACK_GENERATOR "DEB;RPM;TGZ;STGZ")

    # ========================================================================
    # Debian/Ubuntu Package (.deb)
    # ========================================================================
    set(CPACK_DEBIAN_PACKAGE_NAME "presence-for-plex")
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")  # Auto-generate filename
    set(CPACK_DEBIAN_PACKAGE_SECTION "net")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Andrew Barnes <abarnes6@proton.me>")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}\n ${CPACK_PACKAGE_DESCRIPTION}")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")

    # Dependencies for Debian-based systems
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.34), libcurl4 (>= 7.68.0), libstdc++6 (>= 11)")

    # Desktop integration
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/packaging/postinst"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/packaging/postrm"
    )

    # Automatically detect shared library dependencies
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    # ========================================================================
    # RPM Package (.rpm) for Fedora/RHEL/CentOS
    # ========================================================================
    set(CPACK_RPM_PACKAGE_NAME "presence-for-plex")
    set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")  # Auto-generate filename
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
    set(CPACK_RPM_PACKAGE_VENDOR "Andrew Barnes")
    set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION}")

    # Dependencies for RPM-based systems
    set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.34, libcurl >= 7.68.0, libstdc++ >= 11")
    set(CPACK_RPM_PACKAGE_SUGGESTS "discord")

    # Post-install/uninstall scripts
    set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/packaging/postinst")
    set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/packaging/postrm")

    # Automatically detect shared library dependencies
    set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)

    # Prevent stripping debug symbols (optional, can be changed)
    set(CPACK_RPM_SPEC_MORE_DEFINE "%define __strip /bin/true")

    # ========================================================================
    # Generic Linux Archive (TGZ)
    # ========================================================================
    set(CPACK_ARCHIVE_FILE_NAME "PresenceForPlex-${PROJECT_VERSION}-linux-x86_64")

    # Installation prefix for DEB/RPM packages (use /usr for standard packages)
    # TGZ archives can use /opt for optional manual installation
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    # Component installation
    set(CPACK_COMPONENTS_ALL application desktop)
    set(CPACK_COMPONENT_APPLICATION_DISPLAY_NAME "Application")
    set(CPACK_COMPONENT_APPLICATION_DESCRIPTION "Main application executable")
    set(CPACK_COMPONENT_APPLICATION_REQUIRED ON)
    set(CPACK_COMPONENT_DESKTOP_DISPLAY_NAME "Desktop Integration")
    set(CPACK_COMPONENT_DESKTOP_DESCRIPTION "Desktop entry and icon")
endif()

# ============================================================================
# Component definitions (cross-platform)
# ============================================================================
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)

# Output package to dedicated directory
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_BINARY_DIR}/packages")

# Verbose packaging output
set(CPACK_PACKAGE_VERBOSE ON)

message(STATUS "Packaging configuration:")
message(STATUS "  Package name: ${CPACK_PACKAGE_NAME}")
message(STATUS "  Version: ${CPACK_PACKAGE_VERSION}")
message(STATUS "  Generators: ${CPACK_GENERATOR}")
message(STATUS "  Output directory: ${CPACK_OUTPUT_FILE_PREFIX}")

# Include CPack after all variables are set
include(CPack)
