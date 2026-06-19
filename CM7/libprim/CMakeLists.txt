# libprim — L1 primitive 2D renderer (RGB565 framebuffer, ARGB compositing).
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PRIM_VERSION)
string(STRIP "${PRIM_VERSION}" PRIM_VERSION)
project(libprim VERSION ${PRIM_VERSION} LANGUAGES C)

add_library(prim STATIC
    src/fb.c
    src/fill.c
    src/shapes.c
    src/path.c
    src/gradient.c
    src/glow.c
    src/text.c
    src/prim.c
    src/internal/dma2d_backend.c
    src/internal/bezier.c
    src/internal/rasterizer.c
)
add_library(prim::prim ALIAS prim)

set_target_properties(prim PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(prim
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(prim PRIVATE PRIM_BUILDING=1)

# libm for sqrtf/cosf/sinf/atan2f in shapes/path.
find_library(M_LIB m)
if(M_LIB)
    target_link_libraries(prim PUBLIC ${M_LIB})
endif()

set_strict_warnings(prim)
set_release_optimization(prim)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES AND NOT CMAKE_CROSSCOMPILING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/examples)
    add_subdirectory(examples)
endif()
