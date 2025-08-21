cmake_minimum_required (VERSION 3.25)

set(CMAKE_SUPPRESS_REGENERATION true)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("PresenceForPlex" VERSION 0.3.6 LANGUAGES CXX)

# Configure version.h from template
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/include/version.h.in
  ${CMAKE_CURRENT_LIST_DIR}/include/version.h
  @ONLY
)

# Add both project include directories and binary directory to include paths
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR})

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS include/*.h)

# Add resource file for Windows
if(WIN32)
  set(RES_FILES assets/resources.rc)
endif()

# Allow to use system libraries
if(USE_DYNAMIC_LINKS)
    find_package(CURL REQUIRED)
    find_package(nlohmann_json REQUIRED)
    find_package(yaml-cpp REQUIRED)
else()
    # Packages
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF)
    if(WIN32)
      set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    elseif(APPLE)
      set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
    else()
      find_package(OpenSSL QUIET)
      if(OPENSSL_FOUND)
        set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
      endif()
    endif()

    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_CURL_TESTS OFF CACHE BOOL "" FORCE)      # Disable curl tests
    set(CURL_DISABLE_TESTS ON CACHE BOOL "" FORCE)     # Really disable tests
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
    set(ENABLE_MANUAL OFF CACHE BOOL "" FORCE)         # Don't build manual
    set(ENABLE_DOCS OFF CACHE BOOL "" FORCE)           # Don't build docs
    ## yaml-cpp
    FetchContent_Declare(
      yaml-cpp
      GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
      GIT_TAG master
    )
    FetchContent_MakeAvailable(yaml-cpp)
    
    ## nlohmann_json
    FetchContent_Declare(
      json
      URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    )
    FetchContent_MakeAvailable(json)
    
    ## curl
    FetchContent_Declare(
      curl
      URL https://github.com/curl/curl/releases/download/curl-7_86_0/curl-7.86.0.tar.gz
    )
    FetchContent_MakeAvailable(curl)
endif()

# Add source to this project's executable.
if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS")
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
    add_executable(PresenceForPlex WIN32 src/main.cpp ${SOURCES} ${HEADERS} ${RES_FILES})
else()
    add_executable(PresenceForPlex src/main.cpp ${SOURCES} ${HEADERS} ${RES_FILES})
endif()

target_include_directories(PresenceForPlex PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(PresenceForPlex PRIVATE CURL::libcurl yaml-cpp::yaml-cpp nlohmann_json::nlohmann_json)

if (CMAKE_VERSION VERSION_GREATER 3.25)
  set_property(TARGET PresenceForPlex PROPERTY CXX_STANDARD 17)
endif()

install(TARGETS PresenceForPlex
    RUNTIME DESTINATION .)         # Root of staging dir
install(FILES LICENSE README.md
    DESTINATION .)
if (WIN32)
  install(FILES
    $<TARGET_RUNTIME_DLLS:PresenceForPlex>
    DESTINATION .
  )
endif()

# Platform-specific packaging options
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    # Set icon for NSIS installer
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    
    # Add option for startup
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
elseif(UNIX)
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "PresenceForPlex")
    
    # Install to standard Linux locations
    set(CPACK_SET_DESTDIR ON)
    set(CPACK_INSTALL_PREFIX "/usr")
endif()

set(CPACK_PACKAGE_NAME "PresenceForPlex")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Andrew Barnes")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Discord Rich Presence for Plex")
set(CPACK_PACKAGE_EXECUTABLES "PresenceForPlex" "Presence For Plex")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

include(CPack)
