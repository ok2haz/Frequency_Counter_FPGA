# Build System (CMake ≥ 3.20)
> 🔴 **HISTORICKÝ DOKUMENT — NENÍ PLATNÁ SPECIFIKACE.**
> Původní zadání z **2026-06-19**, od té doby se neudržuje. Kód se mezitím
> podstatně změnil (jen u fontů: 5 zde uvedených už neexistuje a 5 dnešních tu
> chybí). Ber to jako záznam PŮVODNÍHO záměru, ne jako popis dneška.
> **Autorita je `CLAUDE.md` + `docs/HW_REFERENCE.md` + zdroják.** (audit 2026-08-30)


Build systém pro libprim, libui a app. Cíl: knihovny jsou samostatně sestavitelné,
testovatelné, instalovatelné a použitelné v jiných projektech přes `find_package`.

---

## 1. Top-level CMakeLists.txt

```cmake
# CMakeLists.txt (repo root)
cmake_minimum_required(VERSION 3.20)

project(gpsdo-ui-firmware LANGUAGES C ASM)

# Global build options
option(BUILD_TESTING "Build test suites for libraries" ON)
option(BUILD_EXAMPLES "Build library examples (host only)" ON)
option(BUILD_DOCS "Generate Doxygen documentation" OFF)
option(PRIM_USE_DMA2D "Enable DMA2D HW acceleration in libprim" OFF)

# Detect cross-compile target
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for STM32H757")
    set(PRIM_USE_DMA2D ON CACHE BOOL "" FORCE)
else()
    message(STATUS "Native build for host (tests + examples)")
endif()

# Global compiler flags
include(cmake/compiler_flags.cmake)

# Libraries
add_subdirectory(libprim)
add_subdirectory(libui)

# Application (only when building full firmware, not when libs are reused externally)
if(NOT DEFINED LIBS_ONLY)
    add_subdirectory(app)
endif()

# Optional: documentation
if(BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    add_subdirectory(docs)
endif()

# Testing
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
endif()
```

---

## 2. Toolchainy

### 2.1 Host toolchain (default)

Není potřeba custom toolchain — systémový gcc/clang stačí. Spuštění:

```bash
cmake -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host
```

### 2.2 STM32 toolchain

```cmake
# cmake/stm32_toolchain.cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_AR           arm-none-eabi-ar)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy)
set(CMAKE_SIZE         arm-none-eabi-size)

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(MCU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT "${MCU_FLAGS} -fno-common -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${MCU_FLAGS} -Wl,--gc-sections -specs=nano.specs -specs=nosys.specs")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
```

Spuštění:

```bash
cmake -B build/target \
      -DCMAKE_TOOLCHAIN_FILE=cmake/stm32_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF
cmake --build build/target
```

Produkuje `build/target/app/gpsdo_counter.elf`.

### 2.3 Compiler flags (sdílené)

```cmake
# cmake/compiler_flags.cmake

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
        $<$<CONFIG:Release>:-O2 -DNDEBUG>
        $<$<CONFIG:Debug>:-O0 -g3>
        $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    )
endfunction()
```

---

## 3. libprim CMakeLists.txt

```cmake
# libprim/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PRIM_VERSION)
string(STRIP "${PRIM_VERSION}" PRIM_VERSION)

project(libprim VERSION ${PRIM_VERSION} LANGUAGES C)

# Source files
set(PRIM_SOURCES
    src/fb.c
    src/fill.c
    src/shapes.c
    src/path.c
    src/gradient.c
    src/glow.c
    src/text.c
)

# Optional DMA2D backend
if(PRIM_USE_DMA2D)
    list(APPEND PRIM_SOURCES src/internal/dma2d_backend_stm32.c)
endif()

add_library(prim STATIC ${PRIM_SOURCES})
add_library(prim::prim ALIAS prim)

set_target_properties(prim PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(prim
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(prim PRIVATE PRIM_BUILDING=1)

if(PRIM_USE_DMA2D)
    target_compile_definitions(prim PRIVATE PRIM_USE_DMA2D=1)
endif()

set_strict_warnings(prim)
set_release_optimization(prim)

# ── Install (pro reuse v jiných projektech) ─────────────────
include(GNUInstallDirs)

install(TARGETS prim
    EXPORT primTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/prim
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT primTargets
    FILE primTargets.cmake
    NAMESPACE prim::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/prim
)

# Generate primConfig.cmake for find_package()
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/primConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/primConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/prim
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/primConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/primConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/primConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/prim
)

# ── Tests ────────────────────────────────────────────────────
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(tests)
endif()

# ── Examples ─────────────────────────────────────────────────
if(BUILD_EXAMPLES AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(examples)
endif()
```

Soubor `libprim/cmake/primConfig.cmake.in`:

```cmake
@PACKAGE_INIT@
include("${CMAKE_CURRENT_LIST_DIR}/primTargets.cmake")
check_required_components(prim)
```

**Použití v jiném projektu:**

```cmake
# Nějaký nový projekt
find_package(prim 0.1 REQUIRED)
add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE prim::prim)
```

---

## 4. libui CMakeLists.txt

```cmake
# libui/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" UI_VERSION)
string(STRIP "${UI_VERSION}" UI_VERSION)

project(libui VERSION ${UI_VERSION} LANGUAGES C)

# Find libprim — pokud je vedle v repo, je už target; jinak find_package
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
    src/icons.c
)

# Fonty
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
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(ui PRIVATE UI_BUILDING=1)

# libui PUBLIC linkuje libprim → uživatel libui dostane prim automaticky
target_link_libraries(ui PUBLIC prim::prim)

set_strict_warnings(ui)
set_release_optimization(ui)

# ── Install ──────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS ui
    EXPORT uiTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ui
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT uiTargets
    FILE uiTargets.cmake
    NAMESPACE ui::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ui
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/uiConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/uiConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ui
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/uiConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/uiConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/uiConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ui
)

# uiConfig.cmake.in musí dotáhnout prim:
# @PACKAGE_INIT@
# include(CMakeFindDependencyMacro)
# find_dependency(prim 0.1)
# include("${CMAKE_CURRENT_LIST_DIR}/uiTargets.cmake")

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(examples)
endif()
```

Soubor `libui/cmake/uiConfig.cmake.in`:

```cmake
@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
find_dependency(prim 0.1)
include("${CMAKE_CURRENT_LIST_DIR}/uiTargets.cmake")
check_required_components(ui)
```

---

## 5. app CMakeLists.txt

```cmake
# app/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

project(gpsdo_counter LANGUAGES C ASM)

if(NOT TARGET ui)
    find_package(ui 0.1 REQUIRED)
endif()

set(APP_SOURCES
    src/main.c
    src/cli/cli.c
    src/screens/screen_main.c
    src/screens/screen_main_data.c
)

# HAL per-platform
if(CMAKE_CROSSCOMPILING)
    list(APPEND APP_SOURCES
        src/hal/stm32/display.c
        src/hal/stm32/dma2d.c
        src/hal/stm32/uart.c
        src/hal/stm32/mpu.c
        src/hal/stm32/startup.s
    )
else()
    list(APPEND APP_SOURCES
        src/hal/host/display.c
        src/hal/host/dma2d.c
        src/hal/host/uart.c
    )
endif()

add_executable(gpsdo_counter ${APP_SOURCES})

target_link_libraries(gpsdo_counter PRIVATE
    ui::ui
    prim::prim
)

target_include_directories(gpsdo_counter PRIVATE src)

set_target_properties(gpsdo_counter PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

set_strict_warnings(gpsdo_counter)
set_release_optimization(gpsdo_counter)

# Cross-compile specific: linker script, output formats
if(CMAKE_CROSSCOMPILING)
    target_link_options(gpsdo_counter PRIVATE
        -T${CMAKE_CURRENT_SOURCE_DIR}/linker/stm32h757.ld
        -Wl,-Map=gpsdo_counter.map
    )

    add_custom_command(TARGET gpsdo_counter POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:gpsdo_counter>
                ${CMAKE_CURRENT_BINARY_DIR}/gpsdo_counter.bin
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:gpsdo_counter>
                ${CMAKE_CURRENT_BINARY_DIR}/gpsdo_counter.hex
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:gpsdo_counter>
    )
endif()
```

---

## 6. Workflow

### 6.1 Pro vývoj

```bash
# Host build s testy
cmake -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host -j
ctest --test-dir build/host

# Target build pro flash
cmake -B build/target \
      -DCMAKE_TOOLCHAIN_FILE=cmake/stm32_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF
cmake --build build/target -j

# Flash
st-flash write build/target/app/gpsdo_counter.bin 0x08000000
```

### 6.2 Pro reuse knihovny v jiném projektu

```bash
# 1. Install libprim do system / local prefix
cmake -B build/install -DLIBS_ONLY=ON -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build/install
cmake --install build/install

# 2. V jiném projektu:
# CMakeLists.txt:
#   find_package(prim 0.1 REQUIRED)
#   target_link_libraries(my_app PRIVATE prim::prim)
# Build:
cmake -B build -DCMAKE_PREFIX_PATH=$HOME/.local
```

---

## 7. CI pipeline (návrh)

```yaml
# .github/workflows/ci.yml (zkráceno)
name: CI

on: [push, pull_request]

jobs:
  host-build-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=Debug
      - name: Build
        run: cmake --build build -j
      - name: Test
        run: ctest --test-dir build --output-on-failure
      - name: Lint
        run: ./scripts/lint.sh

  target-build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Setup ARM toolchain
        uses: carlosperate/arm-none-eabi-gcc-action@v1
      - name: Configure
        run: |
          cmake -B build/target \
                -DCMAKE_TOOLCHAIN_FILE=cmake/stm32_toolchain.cmake \
                -DCMAKE_BUILD_TYPE=Release \
                -DBUILD_TESTING=OFF
      - name: Build
        run: cmake --build build/target -j
      - name: Check size
        run: arm-none-eabi-size build/target/app/gpsdo_counter.elf

  docs:
    runs-on: ubuntu-latest
    if: github.ref == 'refs/heads/main'
    steps:
      - uses: actions/checkout@v4
      - name: Generate Doxygen
        run: |
          cmake -B build -DBUILD_DOCS=ON
          cmake --build build --target doxygen
      - name: Deploy to GitHub Pages
        uses: peaceiris/actions-gh-pages@v3
        with:
          publish_dir: ./build/docs/html
```

### Lint script

```bash
#!/usr/bin/env bash
# scripts/lint.sh — Vynucuje pravidla z coding_standards.md
set -e

echo "→ Magic numbers in screens"
if grep -E '\b[0-9]{3,}\b' app/src/screens/*.c | grep -v -E '#define|//|/\*|".*"' ; then
    echo "FAIL: magic numbers found in screens"
    exit 1
fi

echo "→ Literal colors outside theme.h"
if grep -RE '0x[0-9A-Fa-f]{6}' libui/src libui/include \
   --include='*.c' --include='*.h' | grep -v fonts/ | grep -v theme.h ; then
    echo "FAIL: literal color outside theme.h"
    exit 1
fi

echo "→ Raster images in libraries"
if find libprim libui app/src \( -name "*.png" -o -name "*.jpg" -o -name "*.bmp" \) ; then
    echo "FAIL: raster images found in libraries"
    exit 1
fi

echo "→ Cross-layer includes"
if grep -r '#include.*<ui/' libprim/ ; then
    echo "FAIL: libprim includes libui"
    exit 1
fi
if grep -r '#include.*<app' libui/ libprim/ ; then
    echo "FAIL: library includes app"
    exit 1
fi

echo "→ clang-format check"
clang-format --dry-run --Werror libprim/**/*.c libprim/**/*.h \
                                libui/**/*.c libui/**/*.h \
                                app/src/**/*.c app/src/**/*.h

echo "OK"
```

---

## 8. Reuse v jiném projektu — kontrola

Test reuse funguje takto:

```bash
# 1. Vytvoř testovací projekt
mkdir /tmp/reuse-test && cd /tmp/reuse-test
cat > main.c <<EOF
#include <prim/prim.h>
#include <ui/ui.h>
int main(void) {
    /* basic smoke: include works, link works */
    (void)PRIM_RGB(0, 0, 0);
    return 0;
}
EOF
cat > CMakeLists.txt <<EOF
cmake_minimum_required(VERSION 3.20)
project(reuse_test C)
find_package(prim 0.1 REQUIRED)
find_package(ui 0.1 REQUIRED)
add_executable(rt main.c)
target_link_libraries(rt PRIVATE ui::ui prim::prim)
EOF

# 2. Build proti instalaci
cmake -B build -DCMAKE_PREFIX_PATH=$HOME/.local
cmake --build build
./build/rt   # smoke test
```

Pokud tohle funguje, knihovny jsou skutečně reusable.

---

## 9. Versioning workflow

Pří release iterace 1:

```bash
# Aktualizovat VERSION soubory
echo "0.1.0" > libprim/VERSION
echo "0.1.0" > libui/VERSION

# Aktualizovat CHANGELOG.md
# Tag
git tag -a libprim-v0.1.0 -m "libprim 0.1.0: initial release"
git tag -a libui-v0.1.0 -m "libui 0.1.0: initial release"
git tag -a v0.1.0 -m "Initial firmware release"
git push --tags
```

**Pro budoucí breaking change** (např. změna `prim_rect_t` layoutu):

```bash
echo "1.0.0" > libprim/VERSION    # MAJOR bump
# Aktualizovat CHANGELOG: BREAKING: ...
```

---

## 10. Akceptační kritéria build systemu

1. **`cmake -B build && cmake --build build`** funguje na čistém checkoutu (host).
2. **Cross-compile** funguje s STM32 toolchain souborem.
3. **`ctest`** spustí všechny testy a passnou.
4. **`cmake --install`** vytvoří funkční install pro reuse v jiném projektu.
5. **`find_package(prim 0.1)`** funguje v externím projektu po instalaci.
6. **Lint script** (`./scripts/lint.sh`) passne.
7. **CI pipeline** (host build + target build + lint) je v GitHub Actions.
