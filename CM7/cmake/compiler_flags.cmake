# compiler_flags.cmake — shared strict warnings + release optimization.

function(set_strict_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wshadow
        -Wcast-align
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wmissing-declarations
        -Wwrite-strings
        -Wundef
        -Wpointer-arith
        -Werror=implicit-function-declaration
        -Werror=return-type
    )
endfunction()

function(set_release_optimization target)
    target_compile_options(${target} PRIVATE
        $<$<CONFIG:Release>:-O2;-DNDEBUG>
        $<$<CONFIG:Debug>:-O0;-g3>
        $<$<CONFIG:RelWithDebInfo>:-O2;-g>
    )
endfunction()
