# Top-level orchestrator for the libprim / libui / app stack.
# This builds the reusable libraries (and host tests) with CMake. The on-target
# firmware itself is built by STM32CubeIDE; see GPSDO_UI_README.md.

cmake_minimum_required(VERSION 3.20)
project(gpsdo-ui-firmware LANGUAGES C ASM)

option(BUILD_TESTING  "Build library test suites"        ON)
option(BUILD_EXAMPLES "Build library examples (host)"    ON)
option(PRIM_USE_DMA2D "Enable DMA2D backend in libprim"  OFF)

if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for STM32H757")
else()
    message(STATUS "Native host build (library tests + examples)")
    # Drop the .sdram placement attribute on host (glow scratch → plain .bss).
    add_compile_definitions(PRIM_HOST_BUILD=1)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler_flags.cmake)

add_subdirectory(libprim)
add_subdirectory(libui)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
endif()
