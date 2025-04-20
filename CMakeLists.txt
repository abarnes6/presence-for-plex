cmake_minimum_required (VERSION 3.25)

set(CMAKE_SUPPRESS_REGENERATION true)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("PlexRichPresence" VERSION 1.0.0 LANGUAGES CXX)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS src/*.h)

# Add resource file for Windows
if(WIN32)
  set(RES_FILES src/resources.rc)
endif()

find_package(CURL CONFIG REQUIRED) 
find_package(nlohmann_json REQUIRED)
# print the link‑dependent libraries of CURL::libcurl
get_target_property(_curl_deps CURL::libcurl
    INTERFACE_LINK_LIBRARIES)
message(STATUS "CURL::libcurl deps = ${_curl_deps}")

find_path(_TP_INC
  NAMES toml++/toml.hpp
  HINTS "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include"
)
add_library(tomlplusplus::tomlplusplus INTERFACE IMPORTED)
target_include_directories(tomlplusplus::tomlplusplus
  INTERFACE "${_TP_INC}"
)

# Add source to this project's executable.
if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
    add_executable(PlexRichPresence WIN32 src/main.cpp ${SOURCES} ${HEADERS} ${RES_FILES})
else()
    add_executable(PlexRichPresence src/main.cpp ${SOURCES} ${HEADERS} ${RES_FILES})
endif()

target_link_libraries(PlexRichPresence PRIVATE CURL::libcurl tomlplusplus::tomlplusplus)

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
    $<$<CONFIG:Debug>:${VCPKG_ROOT}/${VCPKG_TARGET_TRIPLET}/debug/bin/zlibd1.dll>
    $<$<NOT:$<CONFIG:Debug>>:${VCPKG_ROOT}/${VCPKG_TARGET_TRIPLET}/bin/zlib1.dll>
    DESTINATION .
  )
endif()

# Platform-specific packaging options
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
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Andrew Barnes")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libgtk-3-0, libappindicator3-1, libcurl4, libc6")
    set(CPACK_RPM_PACKAGE_REQUIRES "gtk3, libappindicator, libcurl")
    
    # Desktop integration for Linux
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/plex-rich-presence.desktop"
            DESTINATION "share/applications")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/plex-rich-presence.png"
            DESTINATION "share/icons/hicolor/256x256/apps")
    
    # Install to standard Linux locations
    set(CPACK_SET_DESTDIR ON)
    set(CPACK_INSTALL_PREFIX "/usr")
endif()

set(CPACK_PACKAGE_NAME "Plex Rich Presence")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_VENDOR "Plex Rich Presence")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Plex Rich Presence for Discord")
set(CPACK_PACKAGE_EXECUTABLES "PlexRichPresence" "Plex Rich Presence")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Plex Rich Presence")

include(CPack)