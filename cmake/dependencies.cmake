# Dependency management for PresenceForPlex

include(cmake/CPM.cmake)

# Use system libraries if requested
if(USE_DYNAMIC_LINKS)
    message(STATUS "Using system libraries")

    find_package(CURL REQUIRED)
    find_package(nlohmann_json REQUIRED)
    find_package(yaml-cpp REQUIRED)

    if(BUILD_TESTING)
        find_package(GTest REQUIRED)
    endif()
else()
    message(STATUS "Using static libraries via CPM")

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    # Platform-specific SSL/TLS backend selection
    if(WIN32)
        set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
        set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    elseif(APPLE)
        set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
        set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    else()
        set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
        set(CURL_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
        set(CURL_USE_SECTRANSP OFF CACHE BOOL "" FORCE)
    endif()

    # Common curl options
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_FILE ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)

    set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
    CPMAddPackage(
        NAME yaml-cpp
        GITHUB_REPOSITORY jbeder/yaml-cpp
        GIT_TAG master
        OPTIONS
            "YAML_CPP_BUILD_TESTS OFF"
            "YAML_CPP_BUILD_TOOLS OFF"
            "YAML_CPP_BUILD_CONTRIB OFF"
    )
    set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)

    CPMAddPackage(
        NAME nlohmann_json
        GITHUB_REPOSITORY nlohmann/json
        VERSION 3.11.3
        OPTIONS
            "JSON_BuildTests OFF"
            "JSON_Install OFF"
    )

    CPMAddPackage(
        NAME curl
        GITHUB_REPOSITORY curl/curl
        GIT_TAG curl-8_4_0
    )

    if(BUILD_TESTING)
        CPMAddPackage(
            NAME googletest
            GITHUB_REPOSITORY google/googletest
            VERSION 1.14.0
            OPTIONS
                "gtest_force_shared_crt ON"
                "INSTALL_GTEST OFF"
        )
    endif()
endif()

# Always dynamically link Qt6 (if available)
find_package(Qt6 QUIET COMPONENTS Core Widgets Network)

# Create interface targets for easier linking
if(NOT USE_DYNAMIC_LINKS)
    if(NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl ALIAS libcurl)
    endif()
    if(NOT TARGET yaml-cpp::yaml-cpp)
        add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
    endif()
    if(NOT TARGET nlohmann_json::nlohmann_json)
        add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
    endif()
endif()

if(TARGET yaml-cpp)
    set_target_properties(yaml-cpp PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    # Disable strict warnings for yaml-cpp dependency
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(yaml-cpp PRIVATE
            -Wno-error
            -Wno-sign-conversion
            -Wno-conversion
        )
    endif()
endif()
if(TARGET curlu)
    set_target_properties(curlu PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endif()
if(TARGET libcurl_static)
    set_target_properties(libcurl_static PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
endif()

# Summary of found dependencies
message(STATUS "Dependencies summary:")
message(STATUS "  CURL: Available")
message(STATUS "  yaml-cpp: Available")
message(STATUS "  nlohmann/json: Available")

if(BUILD_TESTING)
    message(STATUS "  Google Test: Available")
endif()

if(WIN32)
    message(STATUS "  Platform: Windows")
elseif(APPLE)
    message(STATUS "  Platform: macOS")
else()
    message(STATUS "  Platform: Linux")
endif()

if(NOT Qt6_FOUND)
    message(STATUS "  Qt6 not found. UI features will be disabled.")
    set(USE_QT_UI FALSE)
else()
    message(STATUS "  Qt6 found. UI features enabled.")
    set(USE_QT_UI TRUE)
endif()