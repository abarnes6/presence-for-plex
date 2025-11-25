# Compiler flags

function(apply_compiler_flags target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8
            /wd4996 /wd4267 /wd4244
            $<$<CONFIG:Release>:/O2 /GL>
        )
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wno-unused-parameter -Wno-missing-field-initializers
            $<$<CONFIG:Debug>:-g>
            $<$<CONFIG:Release>:-O3>
        )
        if(APPLE)
            target_link_options(${target} PRIVATE
                $<$<CONFIG:Release>:-Wl,-dead_strip>
            )
        else()
            target_link_options(${target} PRIVATE
                $<$<CONFIG:Release>:-Wl,--gc-sections,--strip-all>
            )
        endif()
    endif()

    set_target_properties(${target} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
    )
endfunction()
