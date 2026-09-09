# L2 — libui (UI Components Library)
> 🔴 **HISTORICKÝ DOKUMENT — NENÍ PLATNÁ SPECIFIKACE.**
> Původní zadání z **2026-06-19**, od té doby se neudržuje. Kód se mezitím
> podstatně změnil (jen u fontů: 5 zde uvedených už neexistuje a 5 dnešních tu
> chybí). Ber to jako záznam PŮVODNÍHO záměru, ne jako popis dneška.
> **Autorita je `CLAUDE.md` + `docs/HW_REFERENCE.md` + zdroják.** (audit 2026-08-30)


Vizuální slovník celého projektu. **Samostatná knihovna** reusable v projektech
se stejnou estetikou UI. Definuje paletu, dimenze, komponenty (pilulky, karty,
tlačítka), grafy a vektorové ikony.

**Závisí jen na:** libprim.
**Nezávisí na:** ničem z aplikace.

---

## 1. Knihovní organizace

### 1.1 Repository layout

```
libui/
├── CMakeLists.txt
├── VERSION                         # "0.1.0"
├── LICENSE                         # MIT
├── README.md
├── CHANGELOG.md
├── include/ui/                     # PUBLIC API
│   ├── ui.h                        # umbrella
│   ├── api.h                       # UI_API makro
│   ├── theme.h                     # paleta — PUBLIC
│   ├── dimensions.h                # rozměry — PUBLIC
│   ├── pill.h
│   ├── card.h
│   ├── button.h
│   ├── chart.h
│   ├── sparkline.h
│   ├── digit_group.h
│   ├── big_number.h
│   ├── icons.h
│   └── fonts.h                     # extern declarations pro font deskriptory
├── src/
│   ├── pill.c
│   ├── card.c
│   ├── button.c
│   ├── chart.c
│   ├── sparkline.c
│   ├── digit_group.c
│   ├── big_number.c
│   ├── icons.c
│   ├── internal/
│   │   ├── layout.h                # private layout helpery
│   │   └── icon_cache.h
│   └── fonts/                      # generované přes lv_font_conv
│       ├── font_mono_14.c
│       ├── font_mono_18.c
│       ├── font_mono_20.c
│       ├── font_mono_21.c
│       ├── font_mono_25.c
│       ├── font_mono_27.c
│       ├── font_mono_30.c
│       ├── font_mono_75.c
│       ├── font_sans_10.c
│       ├── font_sans_14.c
│       ├── font_sans_16.c
│       ├── font_sans_17.c
│       └── font_sans_20.c
├── tests/
│   ├── CMakeLists.txt
│   ├── test_pill.c
│   ├── test_card.c
│   └── reference/
├── examples/
│   ├── CMakeLists.txt
│   └── show_all_components.c
└── docs/
    └── api.md
```

### 1.2 Public API contract

```c
// Klient zahrnuje:
#include <ui/ui.h>           // dostane vše veřejné včetně theme a dimensions
```

Pokud klient chce **svoje vlastní theme** (jiný projekt, jiná barva), zahrne
hlavičky selektivně a override paletu vlastní implementací — to je možnost
pro budoucí rozšíření, viz sekce 14.

### 1.3 ABI a visibility

Stejný princip jako libprim — `UI_API` makro v `include/ui/api.h`:

```c
// include/ui/api.h
#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(UI_BUILDING)
    #define UI_API __declspec(dllexport)
  #else
    #define UI_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define UI_API __attribute__((visibility("default")))
#else
  #define UI_API
#endif
```

CMake `-fvisibility=hidden`, jen `UI_API` symboly veřejné.

---

## 2. Paleta (theme.h) — PUBLIC API libui

**Centrální zdroj pravdy pro barvy.** Žádná literární hex hodnota mimo tento
soubor v celém repo (lint check).

```c
// include/ui/theme.h
#pragma once
#include <prim/types.h>

// ── Pozadí ─────────────────────────────────────────────────
#define UI_COLOR_BG_0           PRIM_RGB(0x06, 0x09, 0x0E)
#define UI_COLOR_BG_1           PRIM_RGB(0x0A, 0x0F, 0x17)
#define UI_COLOR_BG_CARD        PRIM_RGB(0x0B, 0x10, 0x18)
#define UI_COLOR_BG_HEADER_TOP  PRIM_RGB(0x08, 0x0C, 0x12)
#define UI_COLOR_BG_HEADER_BOT  PRIM_RGB(0x06, 0x09, 0x0E)

// ── Linie a okraje ─────────────────────────────────────────
#define UI_COLOR_LINE           PRIM_RGB(0x15, 0x20, 0x30)
#define UI_COLOR_LINE_HI        PRIM_RGB(0x1F, 0x2D, 0x42)
#define UI_COLOR_AXIS_INK       PRIM_RGB(0x5A, 0x68, 0x78)

// ── Text ───────────────────────────────────────────────────
#define UI_COLOR_INK            PRIM_RGB(0xE3, 0xED, 0xF7)
#define UI_COLOR_INK_2          PRIM_RGB(0x9A, 0xA6, 0xB4)
#define UI_COLOR_INK_3          PRIM_RGB(0x6B, 0x77, 0x85)
#define UI_COLOR_INK_4          PRIM_RGB(0x3A, 0x48, 0x58)
#define UI_COLOR_INK_5          PRIM_RGB(0x1E, 0x27, 0x36)

// ── Akcenty ────────────────────────────────────────────────
#define UI_COLOR_ACC            PRIM_RGB(0x38, 0xBD, 0xF8)
#define UI_COLOR_ACC_SOFT       PRIM_RGB(0x7D, 0xD3, 0xFC)
#define UI_COLOR_OK             PRIM_RGB(0x34, 0xD3, 0x99)
#define UI_COLOR_OK_SOFT        PRIM_RGB(0x86, 0xEF, 0xAC)
#define UI_COLOR_OK_BG          PRIM_RGB(0x0C, 0x1E, 0x15)
#define UI_COLOR_OK_BORDER      PRIM_RGB(0x1D, 0x4D, 0x2E)
#define UI_COLOR_WARN           PRIM_RGB(0xFB, 0xBF, 0x24)
#define UI_COLOR_BAD            PRIM_RGB(0xF8, 0x71, 0x71)
#define UI_COLOR_VIOLET         PRIM_RGB(0xA7, 0x8B, 0xFA)

// ── Tlačítka ───────────────────────────────────────────────
#define UI_COLOR_BTN_RUN_TOP    PRIM_RGB(0x0F, 0x2C, 0x1C)
#define UI_COLOR_BTN_RUN_BOT    PRIM_RGB(0x0A, 0x1D, 0x11)
#define UI_COLOR_BTN_RUN_BORDER PRIM_RGB(0x2E, 0x64, 0x42)
#define UI_COLOR_BTN_ACT_TOP    PRIM_RGB(0x0F, 0x22, 0x3A)
#define UI_COLOR_BTN_ACT_BOT    PRIM_RGB(0x0A, 0x16, 0x26)
#define UI_COLOR_BTN_ACT_BORDER PRIM_RGB(0x2A, 0x4A, 0x6E)
```

---

## 3. Rozměry (dimensions.h) — PUBLIC API libui

```c
// include/ui/dimensions.h
#pragma once

#define UI_DIM_SCREEN_W            800
#define UI_DIM_SCREEN_H            480

#define UI_DIM_HEADER_H            56
#define UI_DIM_FOOTER_H            64
#define UI_DIM_BODY_Y              UI_DIM_HEADER_H
#define UI_DIM_BODY_H              (UI_DIM_SCREEN_H - UI_DIM_HEADER_H - UI_DIM_FOOTER_H)

#define UI_DIM_PADDING_X           22
#define UI_DIM_PADDING_Y           13

#define UI_DIM_PILL_RADIUS         21
#define UI_DIM_PILL_H              30
#define UI_DIM_PILL_PAD_X          15
#define UI_DIM_PILL_PAD_Y          6
#define UI_DIM_PILL_GAP            11
#define UI_DIM_PILL_INNER_GAP      7

#define UI_DIM_CARD_RADIUS         16
#define UI_DIM_CARD_PAD_X          14
#define UI_DIM_CARD_PAD_Y          9

#define UI_DIM_BUTTON_RADIUS       14
#define UI_DIM_BUTTON_H            60
#define UI_DIM_BUTTON_GAP          10
```

---

## 4. Fonty (fonts.h)

```c
// include/ui/fonts.h
#pragma once
#include <prim/text.h>
#include <ui/api.h>

UI_API extern const prim_font_t ui_font_mono_14;
UI_API extern const prim_font_t ui_font_mono_18;
UI_API extern const prim_font_t ui_font_mono_20;
UI_API extern const prim_font_t ui_font_mono_21;
UI_API extern const prim_font_t ui_font_mono_25;
UI_API extern const prim_font_t ui_font_mono_27;
UI_API extern const prim_font_t ui_font_mono_30;
UI_API extern const prim_font_t ui_font_mono_75;
UI_API extern const prim_font_t ui_font_sans_10;
UI_API extern const prim_font_t ui_font_sans_14;
UI_API extern const prim_font_t ui_font_sans_16;
UI_API extern const prim_font_t ui_font_sans_17;
UI_API extern const prim_font_t ui_font_sans_20;
```

Definice v `src/fonts/font_*.c`, generované přes `lv_font_conv`. Unicode podmnožina
specifikovaná v libprim sekci 9.

---

## 5. Pilulka

```c
// include/ui/pill.h
#pragma once
#include <prim/types.h>
#include <ui/api.h>

typedef enum {
    UI_PILL_NORMAL,
    UI_PILL_OK,
    UI_PILL_WARN,
    UI_PILL_BAD,
} ui_pill_variant_t;

typedef void (*ui_icon_render_fn)(prim_point_t pos, int16_t size,
                                   prim_color_t color);

typedef struct {
    int16_t x, y;                      // input: pozice top-left
    int16_t computed_width;            // output: spočítaná šířka
    ui_pill_variant_t variant;
    const char *label;                  // NULL = bez labelu
    const char *value;                  // NULL = bez hodnoty
    ui_icon_render_fn icon_render;     // NULL = bez ikony
    int16_t icon_size;
    prim_color_t icon_color;
    bool has_led;                       // zelená LED s glow vlevo
} ui_pill_t;

UI_API void ui_pill_render(ui_pill_t *pill);
UI_API int16_t ui_pill_measure(const ui_pill_t *pill);
```

**Render postup:**
1. Spočti šířku: `pad_x + (led/icon? icon_size + inner_gap : 0) +
   (label? text_width + inner_gap : 0) + (value? text_width : 0) + pad_x`.
2. `prim_fill_rect_rounded` s barvou pozadí dle variant.
3. `prim_stroke_rect_rounded` 1 px border.
4. LED nebo ikona vlevo.
5. Label (font_mono_14, INK_3).
6. Value (font_mono_18, barva dle variant).

---

## 6. Karta

```c
// include/ui/card.h
#pragma once
#include <prim/types.h>
#include <ui/api.h>

typedef struct {
    prim_rect_t rect;
    const char *header_label;           // sans-serif 20 px, INK_3
    const char *header_right;           // sans-serif 17 px
    prim_color_t header_right_accent;
} ui_card_t;

UI_API void ui_card_render_chrome(const ui_card_t *card);
UI_API prim_rect_t ui_card_inner_rect(const ui_card_t *card);
```

`ui_card_render_chrome` kreslí pozadí, rámeček a hlavičku. **Obsah uvnitř** řeší
volající přímým voláním komponent nebo primitiv s respektem k `ui_card_inner_rect`.

---

## 7. Tlačítko

```c
// include/ui/button.h
#pragma once
#include <prim/types.h>
#include <ui/api.h>

typedef enum {
    UI_BUTTON_NORMAL,
    UI_BUTTON_ACTIVE,
    UI_BUTTON_RUN,
} ui_button_variant_t;

typedef struct {
    prim_rect_t rect;
    ui_button_variant_t variant;
    const char *label;
    const char *value;                  // NULL = jeden řádek; jinak dvouřádkové
} ui_button_t;

UI_API void ui_button_render(const ui_button_t *btn);
```

---

## 8. Log-log graf

```c
// include/ui/chart.h
#pragma once
#include <prim/types.h>
#include <ui/api.h>

typedef struct {
    prim_rect_t inner;
    int16_t x_decade_count;
    int16_t y_decade_count;
    int8_t x_min_exp;
    int8_t y_min_exp;
    const char *x_label;
    const char **x_tick_labels;
    const char **y_tick_labels;
} ui_chart_loglog_t;

UI_API void ui_chart_loglog_render_grid(const ui_chart_loglog_t *c);

UI_API void ui_chart_loglog_render_floor(const ui_chart_loglog_t *c,
                                          int16_t y_pixel,
                                          const char *label);

UI_API void ui_chart_loglog_render_curve(const ui_chart_loglog_t *c,
                                          const prim_point_t *points,
                                          int16_t count,
                                          prim_color_t stroke_color,
                                          bool fill_below);

UI_API void ui_chart_loglog_render_cursor(const ui_chart_loglog_t *c,
                                           prim_point_t pos,
                                           const char *label);
```

Body křivky se předávají v **pixelových souřadnicích** inner rectu, ne v
log-mřížkových — volající si přepočet zařídí. Tím se libui nezatěžuje
floating-point matematikou.

---

## 9. Sparkline

```c
// include/ui/sparkline.h
#pragma once
#include <prim/types.h>
#include <ui/api.h>

typedef struct {
    prim_rect_t inner;
    const int16_t *y_values;            // integer normalizace 0..255
    int16_t count;
    bool show_sigma_band;
    int16_t sigma_min;                  // 0..255
    int16_t sigma_max;
    bool show_endpoint_marker;
    bool fill_below;
    prim_color_t stroke_color;
} ui_sparkline_t;

UI_API void ui_sparkline_render(const ui_sparkline_t *s);
```

Hodnoty `int16_t` (ne float) — celočíselná aritmetika je rychlejší a
deterministická.

---

## 10. Skupina číslic se stínováním

```c
// include/ui/digit_group.h
#pragma once
#include <prim/text.h>
#include <ui/api.h>

typedef enum {
    UI_DIGIT_CERTAIN,
    UI_DIGIT_SIGMA,
    UI_DIGIT_FLOOR,
} ui_digit_level_t;

typedef struct {
    const char *text;
    ui_digit_level_t level;
    bool with_underline;
} ui_digit_segment_t;

typedef struct {
    int16_t x, y;                       // baseline
    const prim_font_t *font;
    const ui_digit_segment_t *segments;
    int16_t segment_count;
} ui_digit_group_t;

UI_API void ui_digit_group_render(const ui_digit_group_t *g);
UI_API int16_t ui_digit_group_width(const ui_digit_group_t *g);
```

Render: pro každý segment vykresli text v dané barvě (mapování level → color v
implementaci), underline + glow pod posledním jistým digitem.

---

## 11. Velké číslo

```c
// include/ui/big_number.h
#pragma once
#include <prim/text.h>
#include <ui/digit_group.h>
#include <ui/api.h>

typedef struct {
    int16_t x_center;
    int16_t y_baseline;
    const prim_font_t *main_font;        // např. ui_font_mono_75
    const prim_font_t *sep_font;          // např. ui_font_mono_25 (tečky)
    const prim_font_t *decimal_font;     // např. ui_font_mono_30 (čárka)
    const prim_font_t *unit_font;        // např. ui_font_sans_20 (Hz)
    const ui_digit_segment_t *segments;
    int16_t segment_count;
    const char *separators;              // např. "··,·" — 4 znaky
    prim_color_t sep_color;
    prim_color_t decimal_color;
    const char *unit;
    prim_color_t unit_color;
} ui_big_number_t;

UI_API void ui_big_number_render(const ui_big_number_t *n);
```

---

## 12. Ikony — vektorové, žádné bitmapy

```c
// include/ui/icons.h
#pragma once
#include <prim/types.h>
#include <ui/api.h>

UI_API void ui_icon_sat_dish    (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_led         (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_menu        (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_play        (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_temperature (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_gear        (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_chart_line  (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_chart_histo (prim_point_t pos, int16_t size, prim_color_t color);
UI_API void ui_icon_warning     (prim_point_t pos, int16_t size, prim_color_t color);
```

### Pravidla pro implementaci ikon

- Každá ikona pracuje v interním 24×24 viewboxu, škálovaném na `size`.
- **Jen primitiva libprim**. Žádné raw pixel pole, žádné kódované bitmapy, žádné
  PNG.
- Funkce ikon mohou volat **interní helper** `ui_icon_scale(int16_t raw, int16_t size)`,
  který v `src/internal/icon_helpers.h` přepočítá 24×24 souřadnice na cílovou
  velikost.

### Příklad — parabolická anténa

```c
// src/icons.c (zkráceno, plná implementace v repo)
void ui_icon_sat_dish(prim_point_t pos, int16_t size, prim_color_t color) {
    // Misa otevřená vzhůru: kvadratický Bézier (3,9) → (12,19) → (21,9)
    prim_path_t *p = prim_path_create(8);
    prim_path_move_to(p, ICON_PT(3, 9));
    prim_path_quad_to(p, ICON_PT(12, 19), ICON_PT(21, 9));

    prim_path_fill(p, PRIM_ALPHA(color, 0x40));
    prim_path_stroke(p, 2, color);
    prim_path_destroy(p);

    // Stojan, základna, rameno feed hornu
    prim_draw_line(ICON_PT(12, 14), ICON_PT(12, 20), 2, color);
    prim_draw_line(ICON_PT(9,  20), ICON_PT(15, 20), 2, color);
    prim_draw_line(ICON_PT(12, 13), ICON_PT(15, 6),  2, color);
    prim_fill_circle(ICON_PT(15, 6), ICON_SC(2), color);

    // 3 RF oblouky od družic
    prim_draw_arc(ICON_PT(15, 6), ICON_SC(3), 1,
                  PRIM_ALPHA(color, 0xB0), -45, 90);
    prim_draw_arc(ICON_PT(15, 6), ICON_SC(5), 1,
                  PRIM_ALPHA(color, 0x70), -45, 90);
}
```

Makra `ICON_PT()`, `ICON_SC()` v `src/internal/icon_helpers.h`.

### Cache ikon (volitelná optimalizace)

`src/internal/icon_cache.h` poskytuje wrapper, který při prvním volání ikony
v dané `size + color` vyrenderuje do RAM bufferu a dále jen blittuje. Klient
nemusí o cache vědět:

```c
// src/internal/icon_cache.h (interní)
const prim_color_t *ui_icon_get_cached(ui_icon_render_fn fn, int16_t size,
                                        prim_color_t color);
```

Použito interně v `ui_pill_render`, když je ikona stejná v opakovaných snímcích.

---

## 13. Build (CMakeLists.txt)

```cmake
# libui/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" UI_VERSION)
string(STRIP "${UI_VERSION}" UI_VERSION)

project(libui VERSION ${UI_VERSION} LANGUAGES C)

# libui závisí na libprim (jako already-built target)
if(NOT TARGET prim)
    message(FATAL_ERROR "libui requires libprim. Add libprim/ before libui/ in top-level CMakeLists.txt")
endif()

add_library(ui STATIC
    src/pill.c
    src/card.c
    src/button.c
    src/chart.c
    src/sparkline.c
    src/digit_group.c
    src/big_number.c
    src/icons.c
    src/fonts/font_mono_14.c
    src/fonts/font_mono_18.c
    src/fonts/font_mono_20.c
    src/fonts/font_mono_21.c
    src/fonts/font_mono_25.c
    src/fonts/font_mono_27.c
    src/fonts/font_mono_30.c
    src/fonts/font_mono_75.c
    src/fonts/font_sans_10.c
    src/fonts/font_sans_14.c
    src/fonts/font_sans_16.c
    src/fonts/font_sans_17.c
    src/fonts/font_sans_20.c
)

set_target_properties(ui PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
)

target_include_directories(ui
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(ui PRIVATE UI_BUILDING)

target_link_libraries(ui PUBLIC prim)

target_compile_options(ui PRIVATE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wcast-align -Wstrict-prototypes
    -Wmissing-prototypes -Wmissing-declarations
)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(examples)
endif()
```

---

## 14. Future: theme override (mimo iteraci 1, jen design hint)

Pokud někdy bude potřeba `libui` použít v projektu s jinou paletou:

- `include/ui/theme.h` bude defaultní implementace.
- Klient může před `#include <ui/ui.h>` `#define UI_THEME_CUSTOM` a poskytnout
  vlastní `ui_theme_override.h` se stejnými makry.
- Nebo lépe: rozdělit `theme.h` na `theme_tokens.h` (named role: `UI_COLOR_PRIMARY`,
  `UI_COLOR_OK`, …) a `theme_default.h` s konkrétními hodnotami. Klient pak
  override jen mapování role → barva.

Toto **není v iteraci 1**, ale architektura by neměla bránit budoucímu rozšíření.

---

## 15. Akceptační kritéria libui

1. **Sestaví se s libprim** jako jediná dependency.
2. **Host build a target build** projdou bez warningu.
3. **Žádná literární barva mimo `theme.h`** (grep `0x[0-9A-Fa-f]{6}` v
   `src/*.c` jen v `fonts/*.c`).
4. **Žádný PNG/JPG/BMP** v `libui/` (lint).
5. **Všechny ikony implementované jen pomocí `prim_*` funkcí** — žádné raw
   pixel pole v `icons.c`.
6. **`ctest` v libui/build** passne.
7. **Pixel-diff** vs. referenční PNG pro každou komponentu < 0,5 %.
8. **Reusable v jiném projektu:** `examples/show_all_components.c` se sestaví
   a vyrenderuje PNG s ukázkami všech komponent.

---

## 16. Co libui NEdělá

- **Žádné konkrétní obrazovky.** Layout, počet pilulek v hlavičce — to je app.
- **Žádné hodnoty natvrdo.** Komponenty přebírají data parametrem.
- **Žádný stav.** Komponenty jsou stateless renderery.
- **Žádné dotyky.** Ovládací stavy spravuje app.
- **Žádné animace.** Iterace 1 statická; pokud někdy budou animace, řeší je app
  voláním `ui_*_render` s dirty regions a interpolovanými hodnotami.
