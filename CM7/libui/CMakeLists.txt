# libui — L2 UI components, depends only on libprim.
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" UI_VERSION)
string(STRIP "${UI_VERSION}" UI_VERSION)
project(libui VERSION ${UI_VERSION} LANGUAGES C)

if(NOT TARGET prim)
    find_package(prim 0.1 REQUIRED)
endif()

set(UI_SOURCES
    src/pill.c
    src/card.c
    src/button.c
    src/chart.c
    src/sparkline.c
    src/digit_group.c
    src/big_number.c
    src/bargraph.c
    src/icons.c
    src/ui.c
)
file(GLOB UI_FONT_SOURCES "src/fonts/*.c")
list(APPEND UI_SOURCES ${UI_FONT_SOURCES})

add_library(ui STATIC ${UI_SOURCES})
add_library(ui::ui ALIAS ui)

set_target_properties(ui PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(ui
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(ui PRIVATE UI_BUILDING=1)
target_link_libraries(ui PUBLIC prim::prim)

set_strict_warnings(ui)
set_release_optimization(ui)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_subdirectory(tests)
endif()
