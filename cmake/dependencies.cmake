# Dependency management for PresenceForPlex

include(FetchContent)

# Set common fetch content options
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

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
    message(STATUS "Using static libraries via FetchContent")

    # Disable building tests and examples for dependencies
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
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

    # yaml-cpp
    message(STATUS "Fetching yaml-cpp...")
    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
        GIT_SHALLOW ON
    )

    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(yaml-cpp)

    # nlohmann/json
    message(STATUS "Fetching nlohmann/json...")
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW ON
    )

    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(json)

    # curl
    message(STATUS "Fetching curl...")
    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_4_0
        GIT_SHALLOW ON
    )
    FetchContent_MakeAvailable(curl)


    # Google Test (if building tests)
    if(BUILD_TESTING)
        message(STATUS "Fetching Google Test...")
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
            GIT_SHALLOW ON
        )

        # For Windows: Prevent overriding the parent project's compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

        # Disable installation of gtest
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(googletest)
    endif()
endif()

# Platform-specific dependencies
if(WIN32)
    # Windows-specific libraries are linked in the main CMakeLists.txt
elseif(APPLE)
    # macOS frameworks are linked in the main CMakeLists.txt
else()
    # Linux-specific dependencies (simplified, no pkg-config required)

    # Optional: X11 for window management
    find_package(X11 QUIET)
    if(X11_FOUND)
        add_compile_definitions(HAVE_X11)
        message(STATUS "Found X11: ${X11_LIBRARIES}")
    else()
        message(STATUS "X11 not found - window management features disabled")
    endif()

    # For now, skip GTK and libnotify to avoid pkg-config dependency
    # These can be added later when needed with modern CMake find modules
    message(STATUS "GTK3 and libnotify support disabled (avoid pkg-config dependency)")
endif()

# Create interface targets for easier linking
if(NOT USE_DYNAMIC_LINKS)
    # Create aliases that match the system package names (only if they don't exist)
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

# Summary of found dependencies
message(STATUS "Dependencies summary:")
message(STATUS "  CURL: ${CURL_FOUND}")
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
    message(STATUS "    X11: ${X11_FOUND}")
    message(STATUS "    GTK3: Disabled (avoiding pkg-config)")
    message(STATUS "    libnotify: Disabled (avoiding pkg-config)")
endif()