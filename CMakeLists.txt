cmake_minimum_required (VERSION 3.25)

set(CMAKE_SUPPRESS_REGENERATION true)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("PlexPresence" VERSION 0.1.1 LANGUAGES CXX)

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
  HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
)
add_library(tomlplusplus::tomlplusplus INTERFACE IMPORTED)
target_include_directories(tomlplusplus::tomlplusplus
  INTERFACE "${_TP_INC}"
)

# Add source to this project's executable.
if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
    add_executable(PlexPresence WIN32 src/main.cpp ${SOURCES} ${HEADERS} ${RES_FILES})
else()
    add_executable(PlexPresence src/main.cpp ${SOURCES} ${HEADERS} ${RES_FILES})
endif()

target_link_libraries(PlexPresence PRIVATE CURL::libcurl tomlplusplus::tomlplusplus)

if (CMAKE_VERSION VERSION_GREATER 3.25)
  set_property(TARGET PlexPresence PROPERTY CXX_STANDARD 17)
endif()

install(TARGETS PlexPresence
    RUNTIME DESTINATION .)         # Root of staging dir
install(FILES LICENSE README.md
    DESTINATION .)
if (WIN32)
  install(FILES
    $<TARGET_RUNTIME_DLLS:PlexPresence>
    $<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin/zlibd1.dll>
    $<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/zlib1.dll>
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
        CreateShortCut \\\"$DESKTOP\\\\Plex Presence.lnk\\\" \\\"$INSTDIR\\\\PlexPresence.exe\\\" 
        CreateDirectory \\\"$SMPROGRAMS\\\\Plex Presence\\\" 
        CreateShortCut \\\"$SMPROGRAMS\\\\Plex Presence\\\\Plex Presence.lnk\\\" \\\"$INSTDIR\\\\PlexPresence.exe\\\" 
        WriteRegStr HKCU \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run\\\" \\\"PlexPresence\\\" \\\"$INSTDIR\\\\PlexPresence.exe\\\" 
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        Delete \\\"$DESKTOP\\\\Plex Presence.lnk\\\" 
        Delete \\\"$SMPROGRAMS\\\\Plex Presence\\\\Plex Presence.lnk\\\" 
        RMDir \\\"$SMPROGRAMS\\\\Plex Presence\\\" 
        DeleteRegValue HKCU \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run\\\" \\\"PlexPresence\\\" 
    ")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "PlexPresence")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Andrew Barnes")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4, libc6")
    set(CPACK_RPM_PACKAGE_REQUIRES "libcurl")
    
    # Install to standard Linux locations
    set(CPACK_SET_DESTDIR ON)
    set(CPACK_INSTALL_PREFIX "/usr")
endif()

set(CPACK_PACKAGE_NAME "Plex Presence")
set(CPACK_PACKAGE_VERSION "0.1.1")
set(CPACK_PACKAGE_VENDOR "Andrew Barnes")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Plex Presence for Discord")
set(CPACK_PACKAGE_EXECUTABLES "PlexPresence" "Plex Presence")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Plex Presence")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

include(CPack)