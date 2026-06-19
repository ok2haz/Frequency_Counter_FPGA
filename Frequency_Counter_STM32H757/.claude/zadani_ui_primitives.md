# L1 — libprim (Primitive 2D Renderer)

Nejnižší render vrstva. **Samostatná knihovna** reusable v jakémkoli embedded
projektu s framebufferem. Generic 2D grafický stack: fill, line, arc, path,
gradient, glow, text rendering.

**Závisí jen na:** HAL abstrakci (display framebuffer + volitelně DMA2D engine).
**Nezávisí na:** ničem z `libui` nebo aplikace.

---

## 1. Knihovní organizace

### 1.1 Repository layout

```
libprim/
├── CMakeLists.txt
├── VERSION                         # "0.1.0"
├── LICENSE                         # MIT
├── README.md                       # uživatelský README
├── CHANGELOG.md
├── include/prim/                   # PUBLIC API
│   ├── prim.h                      # umbrella (klient zahrnuje tohle)
│   ├── api.h                       # PRIM_API makro pro visibility
│   ├── types.h                     # struktury, enums
│   ├── fb.h                        # framebuffer, target, clip
│   ├── fill.h                      # fill_rect, fill_rounded
│   ├── shapes.h                    # line, circle, arc, polyline
│   ├── path.h                      # opaque path API
│   ├── gradient.h
│   ├── glow.h
│   └── text.h                      # font rendering
├── src/
│   ├── fb.c
│   ├── fill.c
│   ├── shapes.c
│   ├── path.c
│   ├── gradient.c
│   ├── glow.c
│   ├── text.c
│   └── internal/                   # PRIVATE (klient nevidí)
│       ├── bezier.h
│       ├── rasterizer.h
│       ├── alpha_blend.h
│       ├── dma2d_backend.h         # abstrakce nad DMA2D HAL
│       └── path_impl.h             # plná definice opaque structů
├── tests/
│   ├── CMakeLists.txt
│   ├── test_fill.c
│   ├── test_shapes.c
│   ├── test_path.c
│   ├── test_text.c
│   └── reference/                  # PNG reference pro pixel-diff testy
│       ├── fill_rect.png
│       └── ...
├── examples/
│   ├── CMakeLists.txt
│   ├── 01_hello_rect.c
│   ├── 02_gradient.c
│   ├── 03_path.c
│   └── 04_text.c
└── docs/
    ├── api.md
    └── tutorial.md
```

### 1.2 Public API contract

Klient zahrnuje **jen umbrella header**:

```c
#include <prim/prim.h>      // dostane vše veřejné
```

Umbrella header zahrnuje všechny ostatní public hlavičky:

```c
// include/prim/prim.h
#pragma once
#include <prim/api.h>
#include <prim/types.h>
#include <prim/fb.h>
#include <prim/fill.h>
#include <prim/shapes.h>
#include <prim/path.h>
#include <prim/gradient.h>
#include <prim/glow.h>
#include <prim/text.h>
```

Funkce v hlavičkách jsou označené `PRIM_API`:

```c
// include/prim/api.h
#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(PRIM_BUILDING)
    #define PRIM_API __declspec(dllexport)
  #else
    #define PRIM_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define PRIM_API __attribute__((visibility("default")))
#else
  #define PRIM_API
#endif
```

V CMake nastavíme `-fvisibility=hidden`, takže **jen funkce s `PRIM_API`**
jsou veřejné. Klient dostane link error, pokud zkusí použít internal
symbol.

### 1.3 ABI stabilita

- **Opaque pointers** pro stavové objekty: `typedef struct prim_path prim_path_t;`
  v `include/prim/path.h`, plná definice v `src/internal/path_impl.h`.
- **Veřejné struktury** (`prim_rect_t`, `prim_point_t`, …) jsou stable. Změna
  layoutu = MAJOR version bump.
- **Žádné public globální proměnné** kromě extern font deskriptorů.
- Verze libprim je `0.1.0` (pre-1.0 = API smí drobně fluktuovat, ale ne bezdůvodně).

---

## 2. Typy a koordináty

```c
// include/prim/types.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <prim/api.h>

typedef struct { int16_t x, y;            } prim_point_t;
typedef struct { int16_t x, y, w, h;       } prim_rect_t;
typedef uint32_t prim_color_t;             // ARGB8888: 0xAARRGGBB

#define PRIM_RGB(r,g,b)    (0xFF000000u | ((uint32_t)(r)<<16) | ((uint32_t)(g)<<8) | (uint32_t)(b))
#define PRIM_RGBA(r,g,b,a) (((uint32_t)(a)<<24) | ((uint32_t)(r)<<16) | ((uint32_t)(g)<<8) | (uint32_t)(b))
#define PRIM_ALPHA(c,a)    (((c) & 0x00FFFFFFu) | ((uint32_t)(a)<<24))

typedef enum {
    PRIM_BLEND_REPLACE,
    PRIM_BLEND_OVER,
} prim_blend_t;

typedef enum {
    PRIM_OK = 0,
    PRIM_ERR_NULL = -1,
    PRIM_ERR_OOM = -2,
    PRIM_ERR_INVALID_ARG = -3,
    PRIM_ERR_NOT_INITIALIZED = -4,
} prim_status_t;

typedef enum {
    PRIM_ALIGN_LEFT, PRIM_ALIGN_CENTER, PRIM_ALIGN_RIGHT,
} prim_align_t;
```

**Souřadnice:** (0,0) v levém horním rohu, x doprava, y dolů.

**Barvy:** vždy ARGB8888. Konverze do RGB565 nebo jiných formátů řeší HAL při zápisu
do hardwarového framebufferu.

---

## 3. Framebuffer a clipping

```c
// include/prim/fb.h
#pragma once
#include <prim/api.h>
#include <prim/types.h>

typedef struct prim_fb prim_fb_t;       // opaque

PRIM_API prim_status_t prim_fb_init(prim_fb_t *fb, prim_color_t *pixels,
                                    int16_t width, int16_t height,
                                    int16_t stride_bytes);

PRIM_API void prim_set_target(prim_fb_t *fb);
PRIM_API prim_fb_t *prim_get_target(void);

PRIM_API void prim_set_clip(prim_rect_t rect);
PRIM_API void prim_reset_clip(void);
PRIM_API prim_rect_t prim_get_clip(void);
```

Plná definice `prim_fb_t` je v `src/internal/fb_impl.h` — klient zná jen
opaque pointer.

Global target framebuffer + clip rectangle. Render operace klipují podle
aktuálního clipu, což umožňuje partial redraw v aplikaci.

---

## 4. Fill operace (DMA2D path)

```c
// include/prim/fill.h
#pragma once
#include <prim/api.h>
#include <prim/types.h>

PRIM_API void prim_fill_rect(prim_rect_t rect, prim_color_t color,
                              prim_blend_t blend);

PRIM_API void prim_fill_rect_rounded(prim_rect_t rect, int16_t radius,
                                      prim_color_t color, prim_blend_t blend);

PRIM_API void prim_stroke_rect_rounded(prim_rect_t rect, int16_t radius,
                                        int16_t thickness, prim_color_t color);

PRIM_API void prim_blit(prim_rect_t dst, const prim_color_t *src,
                         int16_t src_stride);
```

### Implementace

- `prim_fill_rect` — DMA2D **Register-to-Memory** mode pro `BLEND_REPLACE`,
  **Memory-to-Memory with PFC + Blending** pro `BLEND_OVER`. SW fallback pro
  host build.
- `prim_fill_rect_rounded` — rozložit na 5 oblastí: centrální obdélník (DMA2D
  fill) + 4 rohy (SW rasterizace s AA, sample 4× per pixel).
- `prim_blit` — DMA2D Memory-to-Memory blit z external bufferu do framebufferu.

DMA2D backend je v `src/internal/dma2d_backend.h` jako abstrakce nad HAL:

```c
// src/internal/dma2d_backend.h
typedef struct {
    void (*fill_rect)(prim_rect_t r, prim_color_t c);
    void (*blend_rect)(prim_rect_t r, prim_color_t c);
    void (*blit)(prim_rect_t dst, const prim_color_t *src, int16_t stride);
    void (*wait)(void);
} dma2d_backend_t;

PRIM_INTERNAL void prim_set_dma2d_backend(const dma2d_backend_t *backend);
```

Aplikace nainstaluje konkrétní implementaci (STM32 HAL nebo SW fallback) přes
HAL vrstvu při inicializaci. **libprim sám HAL nevidí**.

---

## 5. Linky, oblouky, kruhy

```c
// include/prim/shapes.h
#pragma once
#include <prim/api.h>
#include <prim/types.h>

PRIM_API void prim_draw_line(prim_point_t from, prim_point_t to,
                              int16_t thickness, prim_color_t color);

PRIM_API void prim_draw_line_dashed(prim_point_t from, prim_point_t to,
                                     int16_t thickness, prim_color_t color,
                                     int16_t dash_len, int16_t gap_len);

PRIM_API void prim_draw_circle(prim_point_t center, int16_t radius,
                                int16_t thickness, prim_color_t color);

PRIM_API void prim_fill_circle(prim_point_t center, int16_t radius,
                                prim_color_t color);

PRIM_API void prim_draw_arc(prim_point_t center, int16_t radius,
                             int16_t thickness, prim_color_t color,
                             int16_t start_angle_deg, int16_t sweep_angle_deg);
```

### Implementace

- **Linky:** Bresenham + Xiaolin Wu AA. Thickness > 1 přes perpendicular
  offsetting.
- **Vodorovná/svislá čára:** speciální case → `prim_fill_rect` (DMA2D).
- **Kruh, oblouk:** Bresenham circle + Wu AA, 4-way symetrie.
- **SIMD:** Cortex-M7 má `__SADD16`, `__UQADD8` — využij pro paralelní 4ch alpha
  blending.

---

## 6. Path API (opaque, kompozitní)

```c
// include/prim/path.h
#pragma once
#include <prim/api.h>
#include <prim/types.h>

typedef struct prim_path prim_path_t;   // opaque

PRIM_API prim_path_t *prim_path_create(int16_t max_ops);
PRIM_API void prim_path_destroy(prim_path_t *p);
PRIM_API void prim_path_reset(prim_path_t *p);

PRIM_API void prim_path_move_to(prim_path_t *p, prim_point_t pt);
PRIM_API void prim_path_line_to(prim_path_t *p, prim_point_t pt);
PRIM_API void prim_path_quad_to(prim_path_t *p, prim_point_t ctrl, prim_point_t end);
PRIM_API void prim_path_arc(prim_path_t *p, prim_point_t center, int16_t radius,
                             int16_t start_deg, int16_t sweep_deg);
PRIM_API void prim_path_close(prim_path_t *p);

PRIM_API void prim_path_stroke(prim_path_t *p, int16_t thickness,
                                prim_color_t color);
PRIM_API void prim_path_fill(prim_path_t *p, prim_color_t color);
```

### Implementace

- Plná definice `prim_path_t` v `src/internal/path_impl.h`:
  ```c
  struct prim_path {
      prim_path_op_t *ops;
      int16_t op_count;
      int16_t op_capacity;
      prim_point_t current_point;
      bool is_closed;
  };
  ```
- **Statický buffer** alokovaný při `create` (single `malloc` mimo hot path, OK).
- **Stroke:** rasterizace každého segmentu (line/quad/arc), spojení mitered ≤ 3 px.
- **Fill:** scanline rasterizace s even-odd rule, quad Bezier tesselace
  adaptivně.
- **Žádný `malloc` mimo `create` a `destroy`.** Stroke a fill používají
  pre-alokovaný internal buffer pro scanline data.

---

## 7. Gradient

```c
// include/prim/gradient.h
#pragma once
#include <prim/api.h>
#include <prim/types.h>

typedef enum {
    PRIM_GRAD_VERTICAL,
    PRIM_GRAD_HORIZONTAL,
} prim_grad_dir_t;

PRIM_API void prim_fill_gradient_linear(prim_rect_t rect,
                                         prim_color_t start, prim_color_t end,
                                         prim_grad_dir_t dir);

PRIM_API void prim_fill_gradient_radial(prim_rect_t rect, prim_point_t center,
                                         int16_t inner_r, int16_t outer_r,
                                         prim_color_t inner, prim_color_t outer);
```

### Implementace

- **Linear vertikální:** generuj 1-pixel-wide buffer s gradient hodnotami pro
  `rect.h` řádků, pak DMA2D Memory-to-Memory s repeat horizontálně.
- **Linear horizontální:** stejně, otočeně.
- **Radial:** vždy software. Cca 5 ms pro 800×480. Aplikace by měla cachovat
  výsledek, pokud je pozadí statické (typický případ).

---

## 8. Glow

```c
// include/prim/glow.h
#pragma once
#include <prim/api.h>
#include <prim/types.h>

PRIM_API void prim_glow_rect(prim_rect_t rect, int16_t blur_radius,
                              prim_color_t color, uint8_t intensity_pct);

PRIM_API void prim_glow_line(prim_point_t from, prim_point_t to,
                              int16_t blur_radius, prim_color_t color,
                              uint8_t intensity_pct);
```

### Implementace

- Box blur 2× pass (separable horizontal + vertical kernel).
- Pre-alokovaný internal scratch buffer pro blur (statický, 800×480×1 B).
- Aplikace cachuje glow pro statické prvky (LED, underline) — to není věc
  libprim.

---

## 9. Text rendering

```c
// include/prim/text.h
#pragma once
#include <prim/api.h>
#include <prim/types.h>

typedef struct prim_font prim_font_t;       // opaque

// Aplikace deklaruje fonty extern v vlastních souborech, libprim je čte přes
// typed pointer. Příklad v src/screens/main.c:
//   extern const prim_font_t font_mono_75;

PRIM_API void prim_draw_text(prim_point_t pos, const char *utf8,
                              const prim_font_t *font, prim_color_t color,
                              prim_align_t align);

PRIM_API int16_t prim_text_width(const char *utf8, const prim_font_t *font);
PRIM_API int16_t prim_text_height(const prim_font_t *font);
```

### Formát fontu

```c
// src/internal/font_impl.h
struct prim_font {
    const uint8_t *bitmap_data;      // 8-bit alpha, packed
    const uint32_t *glyph_table;     // [codepoint, offset, w, h, advance] tuples
    int16_t glyph_count;
    int16_t line_height;
    int16_t baseline;
    int16_t ascent, descent;
};
```

Generovaný přes `lv_font_conv` z TTF (JetBrains Mono, Inter). Příklad:

```bash
lv_font_conv \
  --font JetBrainsMono-Medium.ttf \
  --size 75 --bpp 8 \
  --range 0x20-0x7E,0x00B0-0x00FF,0x2022,0x00B7,0x00B1,0x00D7,0x0394,0x03A9,\
0x03C0,0x03C3,0x03C4,0x2070-0x209F,0x2190-0x21FF,0x2261,0x25B6,0x25B7,0x25CF \
  --format lvgl --output font_mono_75.c
```

(Nástroj `lv_font_conv` vyrobí samostatné C pole, nevyžaduje LVGL runtime.)

### Unicode podmnožina (povinná pro všechny fonty)

- ASCII 0x20–0x7E
- Latinka rozšířená 0xC0–0x17F (česká diakritika)
- Středová tečka `·` (U+00B7), bullet `•` (U+2022)
- Plus/mínus `±` (U+00B1), krát `×` (U+00D7)
- Řecká písmena: `σ τ μ Ω Δ π α β`
- Horní indexy: `⁰¹²³⁴⁵⁶⁷⁸⁹⁻⁺` (U+2070–U+209F)
- Šipky: `← → ↑ ↓` (U+2190+)
- Symboly: `≡` (U+2261), `▶` (U+25B6), `▷` (U+25B7), `●` (U+25CF)

### Glyph cache

Implementace `prim_draw_text` projde UTF-8, pro každý codepoint binárně vyhledá
glyph v tabulce, blittuje 8-bit alpha bitmap přes DMA2D Memory-to-Memory with
PFC + Blending (source A8 → dest ARGB8888 s alpha multiplication).

Glyph cache pro velké fonty (75 px) je věc **aplikace**, ne libprim. Aplikace
si může pre-renderovat často používané glyfy do bufferu a blittnout je přes
`prim_blit`.

---

## 10. Build (CMakeLists.txt)

```cmake
# libprim/CMakeLists.txt
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
)

set_target_properties(prim PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(prim
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(prim PRIVATE PRIM_BUILDING)

target_compile_options(prim PRIVATE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wcast-align -Wstrict-prototypes
    -Wmissing-prototypes -Wmissing-declarations
)

# Volitelně: vlastní backend pro DMA2D, default je SW
option(PRIM_USE_DMA2D "Enable DMA2D HW acceleration backend" OFF)
if(PRIM_USE_DMA2D)
    target_sources(prim PRIVATE src/internal/dma2d_backend_stm32.c)
    target_compile_definitions(prim PRIVATE PRIM_USE_DMA2D)
endif()

# Tests
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples (jen host build)
if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(examples)
endif()
```

---

## 11. Testing

```cmake
# libprim/tests/CMakeLists.txt
add_executable(test_prim_fill test_fill.c)
target_link_libraries(test_prim_fill PRIVATE prim)
add_test(NAME prim::fill COMMAND test_prim_fill)

# ... ostatní testy
```

Každý test:
1. Inicializuje host framebuffer (in-memory RGBA buffer).
2. Vyvolá testovanou primitiv.
3. Uloží výsledek jako PNG přes `stb_image_write.h`.
4. Pixel-diff s `tests/reference/<test_name>.png`.
5. Pass pokud diff < 0,5 %.

**Žádný target build testů.** Testy běží jen na hostu.

---

## 12. Závislosti

Žádné runtime závislosti kromě libc základu:
- `stdint.h`, `stdbool.h`, `string.h` (memcpy).
- HAL nezávislost: DMA2D backend je dependency injection přes
  `prim_set_dma2d_backend`. Když aplikace backend neinstaluje, libprim
  funguje plně softwarově (host build).

**Build-time:**
- CMake ≥ 3.20.
- C11 compiler (arm-none-eabi-gcc nebo system gcc/clang).
- Pro testy: `stb_image_write.h` (single header, vendorovaný v `tests/`).

---

## 13. Akceptační kritéria libprim

1. **Sestaví se samostatně** bez libui a app.
2. **Host build i target build** projdou bez warningu (`-Wall -Wextra -Wpedantic`).
3. **`ctest` v libprim/build** všechny testy passnou.
4. **Veřejné API je dokumentované** Doxygen komentáři, `make docs` generuje
   HTML.
5. **Žádný `malloc` ve veřejných funkcích** kromě explicitně `_create` (lint).
6. **Žádný `#include` z libui ani app** — fyzicky nedostupné přes CMake config.
7. **Pixel-diff < 0,5 %** vs. referenční PNG pro všechny primitivy.
8. **Performance budget na MCU:**
   - `prim_fill_rect(0,0,800,480)` ≤ 2 ms (DMA2D).
   - `prim_path_stroke` cca 100 segmentů ≤ 8 ms.
   - `prim_draw_text` font_mono_75 jeden glyph ≤ 1 ms (z cache 0,1 ms).
9. **Reusable v jiném projektu:** instalací (`cmake --install`) a linkováním
   v jiném repo funguje out-of-the-box (smoke test v `examples/`).

---

## 14. Co libprim NEdělá

- **Žádné UI komponenty.** Pilulka, karta, tlačítko — to je libui.
- **Žádné layouty.** Souřadnice prvků určuje volající.
- **Žádné stavy, animace, dotyk.** Stateless renderer.
- **Žádná paleta nebo design tokens.** Klient (libui/app) předává barvy přímo.
- **Žádný high-level text styling** (word wrap, justify, hyphenation).
  Klient si počítá přes `prim_text_width`.
- **Žádný high-DPI scaling.** Pracuje v device pixelech.
