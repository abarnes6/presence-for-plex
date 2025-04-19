cmake_minimum_required (VERSION 3.11)

set(CMAKE_SUPPRESS_REGENERATION true)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("PlexRichPresence")

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS src/*.h)

# Add resource file for Windows
if(WIN32)
  set(RES_FILES src/resources.rc)
endif()

find_package(CURL CONFIG REQUIRED) 
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(tomlplusplus REQUIRED IMPORTED_TARGET tomlplusplus)

# Add source to this project's executable.
add_executable (PlexRichPresence src/main.cpp ${SOURCES} ${HEADERS} ${RES_FILES})

target_link_libraries(PlexRichPresence PRIVATE CURL::libcurl PkgConfig::tomlplusplus)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET PlexRichPresence PROPERTY CXX_STANDARD 20)
endif()

install(TARGETS PlexRichPresence
    RUNTIME DESTINATION .)         # Root of staging dir
install(FILES LICENSE README.md
    DESTINATION .)
if (WIN32)
  install(FILES
    $<TARGET_RUNTIME_DLLS:PlexRichPresence>
    DESTINATION .
  )
  endif()
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    # Set icon for NSIS installer
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/icon.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/src/icon.ico")
    
    # Add option for startup
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        CreateShortCut \\\"$DESKTOP\\\\Plex Rich Presence.lnk\\\" \\\"$INSTDIR\\\\PlexRichPresence.exe\\\" 
        CreateDirectory \\\"$SMPROGRAMS\\\\Plex Rich Presence\\\"
        CreateShortCut \\\"$SMPROGRAMS\\\\Plex Rich Presence\\\\Plex Rich Presence.lnk\\\" \\\"$INSTDIR\\\\PlexRichPresence.exe\\\"
        WriteRegStr HKCU \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run\\\" \\\"PlexRichPresence\\\" \\\"$INSTDIR\\\\PlexRichPresence.exe\\\" 
    ")
    
    # Add uninstaller commands to remove startup entry and Start Menu shortcut
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        Delete \\\"$DESKTOP\\\\Plex Rich Presence.lnk\\\" 
        Delete \\\"$SMPROGRAMS\\\\Plex Rich Presence\\\\Plex Rich Presence.lnk\\\"
        RMDir \\\"$SMPROGRAMS\\\\Plex Rich Presence\\\"
        DeleteRegValue HKCU \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run\\\" \\\"PlexRichPresence\\\" 
    ")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "PlexRichPresence")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Andrew Barnes")
endif()
set(CPACK_PACKAGE_NAME "PlexRichPresence")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_VENDOR "PlexRichPresence")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Plex Rich Presence for Discord")
set(CPACK_PACKAGE_EXECUTABLES "PlexRichPresence" "Plex Rich Presence")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "PlexRichPresence")

include(CPack)