# Testing configuration for PresenceForPlex

enable_testing()

# Test configuration options
option(BUILD_UNIT_TESTS "Build unit tests" ON)
option(BUILD_INTEGRATION_TESTS "Build integration tests" ON)
option(BUILD_PERFORMANCE_TESTS "Build performance tests" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# Code coverage setup
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(STATUS "Enabling code coverage")

        add_compile_options(--coverage -O0 -fno-inline -fno-inline-small-functions -fno-default-inline)
        add_link_options(--coverage)

        # Find gcov or llvm-cov
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            find_program(GCOV_PATH gcov)
            if(NOT GCOV_PATH)
                message(WARNING "gcov not found! Coverage reports will not be generated.")
            endif()
        else()
            find_program(LLVM_COV_PATH llvm-cov)
            if(NOT LLVM_COV_PATH)
                message(WARNING "llvm-cov not found! Coverage reports will not be generated.")
            endif()
        endif()

        # Create coverage target
        add_custom_target(coverage
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests and generating coverage report"
        )

        if(GCOV_PATH)
            add_custom_command(TARGET coverage POST_BUILD
                COMMAND gcov ${CMAKE_BINARY_DIR}/CMakeFiles/PresenceForPlex.dir/src/**/*.gcno
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Generating gcov reports"
            )
        endif()
    else()
        message(WARNING "Code coverage is only supported with GCC or Clang")
        set(ENABLE_COVERAGE OFF)
    endif()
endif()

# Helper function to create tests
function(add_plex_test test_name)
    cmake_parse_arguments(ARG "UNIT;INTEGRATION;PERFORMANCE" "WORKING_DIRECTORY" "SOURCES;LIBRARIES;DEPENDENCIES" ${ARGN})

    # Determine test type
    if(ARG_UNIT AND NOT BUILD_UNIT_TESTS)
        return()
    elseif(ARG_INTEGRATION AND NOT BUILD_INTEGRATION_TESTS)
        return()
    elseif(ARG_PERFORMANCE AND NOT BUILD_PERFORMANCE_TESTS)
        return()
    endif()

    # Create test executable
    add_executable(${test_name} ${ARG_SOURCES})

    # Link against common test libraries
    target_link_libraries(${test_name} PRIVATE
        gtest
        gtest_main
        gmock
        gmock_main
        ${ARG_LIBRARIES}
    )

    # Include directories
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    # Apply compiler flags
    apply_compiler_flags(${test_name})

    # Add test to CTest
    if(ARG_WORKING_DIRECTORY)
        add_test(NAME ${test_name}
                COMMAND ${test_name}
                WORKING_DIRECTORY ${ARG_WORKING_DIRECTORY})
    else()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endif()

    # Set test properties
    if(ARG_UNIT)
        set_tests_properties(${test_name} PROPERTIES
            LABELS "unit"
            TIMEOUT 30
        )
    elseif(ARG_INTEGRATION)
        set_tests_properties(${test_name} PROPERTIES
            LABELS "integration"
            TIMEOUT 120
        )
    elseif(ARG_PERFORMANCE)
        set_tests_properties(${test_name} PROPERTIES
            LABELS "performance"
            TIMEOUT 300
        )
    endif()

    # Add dependencies
    if(ARG_DEPENDENCIES)
        add_dependencies(${test_name} ${ARG_DEPENDENCIES})
    endif()

    # Coverage exclusions
    if(ENABLE_COVERAGE)
        set_property(TARGET ${test_name} PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
endfunction()

# Test discovery function
function(discover_tests test_directory test_type)
    file(GLOB_RECURSE test_sources "${test_directory}/*_test.cpp")

    foreach(test_source ${test_sources})
        # Extract test name from file path
        get_filename_component(test_name ${test_source} NAME_WE)

        # Create test
        add_plex_test(${test_name}
            ${test_type}
            SOURCES ${test_source}
        )
    endforeach()
endfunction()

# Common test utilities library
if(BUILD_TESTING)
    add_library(test_utils STATIC
        tests/unit/test_utils.cpp
        tests/fixtures/test_fixtures.cpp
    )

    target_include_directories(test_utils PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(test_utils PUBLIC
        gtest
        gmock
    )

    apply_compiler_flags(test_utils)
endif()

# Custom test targets
if(BUILD_TESTING)
    # Run only unit tests
    add_custom_target(test_unit
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L unit
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running unit tests"
    )

    # Run only integration tests
    add_custom_target(test_integration
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L integration
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running integration tests"
    )

    # Run only performance tests
    if(BUILD_PERFORMANCE_TESTS)
        add_custom_target(test_performance
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L performance
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running performance tests"
        )
    endif()

    # Run all tests with verbose output
    add_custom_target(test_verbose
        COMMAND ${CMAKE_CTEST_COMMAND} --verbose --output-on-failure
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running all tests with verbose output"
    )

    # Memcheck target (requires valgrind)
    find_program(VALGRIND_EXECUTABLE valgrind)
    if(VALGRIND_EXECUTABLE AND NOT WIN32)
        add_custom_target(test_memcheck
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -T memcheck
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests with memory checking"
        )

        # Configure CTest for memcheck
        set(MEMORYCHECK_COMMAND ${VALGRIND_EXECUTABLE})
        set(MEMORYCHECK_COMMAND_OPTIONS "--tool=memcheck --leak-check=full --show-reachable=yes --num-callers=20 --track-fds=yes")
        set(MEMORYCHECK_SUPPRESSIONS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/valgrind.supp")
    endif()

    # Thread sanitizer target
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_custom_target(test_tsan
            COMMAND ${CMAKE_COMMAND} -E env TSAN_OPTIONS=halt_on_error=1:abort_on_error=1 ${CMAKE_CTEST_COMMAND} --output-on-failure
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests with thread sanitizer"
        )
    endif()
endif()

# Test installation
if(BUILD_TESTING)
    install(TARGETS test_utils
        ARCHIVE DESTINATION lib/test
        LIBRARY DESTINATION lib/test
    )
endif()

message(STATUS "Testing configuration:")
message(STATUS "  BUILD_TESTING: ${BUILD_TESTING}")
if(BUILD_TESTING)
    message(STATUS "  BUILD_UNIT_TESTS: ${BUILD_UNIT_TESTS}")
    message(STATUS "  BUILD_INTEGRATION_TESTS: ${BUILD_INTEGRATION_TESTS}")
    message(STATUS "  BUILD_PERFORMANCE_TESTS: ${BUILD_PERFORMANCE_TESTS}")
    message(STATUS "  ENABLE_COVERAGE: ${ENABLE_COVERAGE}")
    message(STATUS "  VALGRIND: ${VALGRIND_EXECUTABLE}")
endif()