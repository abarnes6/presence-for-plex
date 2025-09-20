# Modern C++ compiler flags and optimizations

function(apply_compiler_flags target)
    # Common flags for all compilers
    target_compile_features(${target} PRIVATE cxx_std_23)

    # Compiler-specific flags
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE
            /W4                     # Warning level 4
            /WX                     # Treat warnings as errors
            /permissive-           # Disable non-conforming code
            /Zc:__cplusplus        # Enable __cplusplus macro
            /Zc:preprocessor       # Use conforming preprocessor
            /utf-8                 # UTF-8 source and execution charset

            # Disable specific warnings
            /wd4996                # Deprecated function warnings
            /wd4267                # Conversion warnings
            /wd4244                # Conversion warnings
        )

        # Debug-specific flags
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:/Od>     # No optimization
            $<$<CONFIG:Debug>:/RTC1>   # Runtime checks
            $<$<CONFIG:Debug>:/MDd>    # Debug runtime
        )

        # Release-specific flags
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:/O2>   # Optimize for speed
            $<$<CONFIG:Release>:/Oi>   # Enable intrinsic functions
            $<$<CONFIG:Release>:/GL>   # Whole program optimization
            $<$<CONFIG:Release>:/MD>   # Release runtime
        )

        # Linker flags
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Release>:/LTCG> # Link time code generation
            $<$<CONFIG:Release>:/OPT:REF> # Remove unreferenced functions
            $<$<CONFIG:Release>:/OPT:ICF> # Identical COMDAT folding
        )

    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target} PRIVATE
            -Wall                  # Enable all warnings
            -Wextra               # Extra warnings
            -Werror               # Treat warnings as errors
            -Wpedantic            # Pedantic warnings
            -Wconversion          # Conversion warnings
            -Wsign-conversion     # Sign conversion warnings
            -Wnull-dereference    # Null dereference warnings
            -Wdouble-promotion    # Double promotion warnings
            -Wformat=2            # Format string warnings
            -Wundef               # Undefined macro warnings

            # Disable specific warnings
            -Wno-unused-parameter # Unused parameter warnings
            -Wno-missing-field-initializers # Missing field initializers
        )

        # GCC-specific flags
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE
                -Wlogical-op          # Logical operation warnings
                -Wduplicated-cond     # Duplicated condition warnings
                -Wduplicated-branches # Duplicated branch warnings
                -Wrestrict            # Restrict warnings
            )
        endif()

        # Clang-specific flags
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(${target} PRIVATE
                -Wthread-safety      # Thread safety warnings
                -Wloop-analysis      # Loop analysis warnings
                -Wno-c99-extensions  # Allow C99 extensions
            )
        endif()

        # Debug-specific flags
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:-O0>     # No optimization
            $<$<CONFIG:Debug>:-g3>     # Maximum debug info
            $<$<CONFIG:Debug>:-ggdb>   # GDB-specific debug info
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer> # Keep frame pointers
        )

        # Release-specific flags
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:-O3>           # Maximum optimization
            $<$<CONFIG:Release>:-DNDEBUG>      # Disable assertions
            $<$<CONFIG:Release>:-ffunction-sections> # Function sections
            $<$<CONFIG:Release>:-fdata-sections>     # Data sections
        )

        # Linker flags
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Release>:-Wl,--gc-sections> # Garbage collect sections
            $<$<CONFIG:Release>:-Wl,--strip-all>   # Strip symbols
        )

        # Sanitizers for debug builds
        if(ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address>
                $<$<CONFIG:Debug>:-fsanitize=undefined>
                $<$<CONFIG:Debug>:-fsanitize=leak>
                $<$<CONFIG:Debug>:-fno-sanitize-recover=all>
            )

            target_link_options(${target} PRIVATE
                $<$<CONFIG:Debug>:-fsanitize=address>
                $<$<CONFIG:Debug>:-fsanitize=undefined>
                $<$<CONFIG:Debug>:-fsanitize=leak>
            )
        endif()
    endif()

    # Position independent code
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # Link-time optimization for release builds
    set_target_properties(${target} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
        INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON
    )

    # C++ standard library selection
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT WIN32)
        # Use libstdc++ with Clang on non-Windows platforms for better compatibility
        target_compile_options(${target} PRIVATE -stdlib=libstdc++)
        target_link_options(${target} PRIVATE -stdlib=libstdc++)
    endif()

endfunction()