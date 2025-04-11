# CMakeList.txt : CMake project for PlexRichPresence, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.10)

set(CMAKE_SUPPRESS_REGENERATION true)


# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("PlexRichPresence")

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS src/*.h)

find_package(CURL REQUIRED) 
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(tomlplusplus REQUIRED IMPORTED_TARGET tomlplusplus)

# Add source to this project's executable.
add_executable (PlexRichPresence src/main.cpp ${SOURCES} ${HEADERS})

set(DISCORD_SDK_ROOT "${CMAKE_SOURCE_DIR}/lib/discord_social_sdk")
set(DISCORD_SDK_LIB_DIR "${DISCORD_SDK_ROOT}/lib/release")
set(DISCORD_SDK_BIN_DIR "${DISCORD_SDK_ROOT}/bin/release")
set(DISCORD_SDK_INCLUDE_DIR "${DISCORD_SDK_ROOT}/include")
target_include_directories(PlexRichPresence PRIVATE ${DISCORD_SDK_INCLUDE_DIR})
if(WIN32)
    set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/discord_partner_sdk.lib")
    set(DISCORD_SHARED_LIB "${DISCORD_SDK_BIN_DIR}/discord_partner_sdk.dll")
elseif(APPLE)
    set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.dylib")
    set(DISCORD_SHARED_LIB "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.dylib")
else() # Linux
    set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.so")
    set(DISCORD_SHARED_LIB "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.so")
endif()

target_link_libraries(PlexRichPresence PRIVATE CURL::libcurl ${DISCORD_LIB_PATH} PkgConfig::tomlplusplus)

if(UNIX)
    # Use RPATH when building
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    # Set the RPATH to use the lib directory relative to the executable
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
    if(APPLE)
        set(CMAKE_INSTALL_RPATH "@executable_path")
    endif()
endif()
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET PlexRichPresence PROPERTY CXX_STANDARD 20)
endif()


add_custom_command(TARGET PlexRichPresence POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${DISCORD_SHARED_LIB}"
    $<TARGET_FILE_DIR:PlexRichPresence>
)
# TODO: Add tests and install targets if needed.
