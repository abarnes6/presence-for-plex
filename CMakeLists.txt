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

find_package(CURL REQUIRED) 
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(tomlplusplus REQUIRED IMPORTED_TARGET tomlplusplus)

# Add source to this project's executable.
add_executable (PlexRichPresence src/main.cpp ${SOURCES} ${HEADERS})

target_link_libraries(PlexRichPresence PRIVATE CURL::libcurl PkgConfig::tomlplusplus)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET PlexRichPresence PROPERTY CXX_STANDARD 20)
endif()