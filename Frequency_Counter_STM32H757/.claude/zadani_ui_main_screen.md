# APP — Main Screen (hlavní obrazovka)
> 🔴 **HISTORICKÝ DOKUMENT — NENÍ PLATNÁ SPECIFIKACE.**
> Původní zadání z **2026-06-19**, od té doby se neudržuje. Kód se mezitím
> podstatně změnil (jen u fontů: 5 zde uvedených už neexistuje a 5 dnešních tu
> chybí). Ber to jako záznam PŮVODNÍHO záměru, ne jako popis dneška.
> **Autorita je `CLAUDE.md` + `docs/HW_REFERENCE.md` + zdroják.** (audit 2026-08-30)


Aplikační vrstva. **Skládá komponenty `libui` do konkrétního layoutu** podle vizuální
reference v9 (`docs/reference_v9.png`). Specifická pro tento přístroj, nepřenosná.

V iteraci 1 je obrazovka **statická** — všechny hodnoty jsou pevné konstanty.

**Závisí na:** `libui`, `libprim` (přes umbrella `<ui/ui.h>`).
**Nesmí použít:** primitiva `libprim` přímo (kromě `prim_blit` pro cache). Pokud
něco chybí, **rozšiř `libui`**, nedělej to v app.

---

## 1. Pravidla pro app vrstvu

### 1.1 Co smí a nesmí

**Smí:**
- Volat `ui_*` funkce.
- Volat `prim_blit` pro blittování pre-renderovaných cache.
- Volat `prim_set_target`, `prim_set_clip` pro framebuffer management.
- Definovat vlastní layout konstanty v `screens/screen_main.h`.

**Nesmí:**
- Volat `prim_fill_*`, `prim_draw_*`, `prim_path_*`, `prim_glow_*` přímo. Pokud
  je třeba nová primitiv, rozšiř `libui`.
- Mít literární barvy. Vše přes `UI_COLOR_*` z `<ui/theme.h>`.
- Mít magic numbers pro rozměry. Vše přes `UI_DIM_*` nebo lokální `static const`.
- Dělat výpočty hodnot ze surových dat — to bude **data adapter vrstva** v
  budoucích iteracích.

### 1.2 Layout konstanty

Specifické rozměry hlavní obrazovky (mimo `UI_DIM_*`):

```c
// app/src/screens/screen_main.h
#pragma once

#define SCR_MAIN_TITLE_Y           (UI_DIM_BODY_Y + 22)
#define SCR_MAIN_NUMBER_Y_BASELINE (UI_DIM_BODY_Y + 128)
#define SCR_MAIN_GRID_Y            (UI_DIM_BODY_Y + 170)
#define SCR_MAIN_GRID_GAP          14
#define SCR_MAIN_GRID_LEFT_RATIO   53  // procento (1.15 : 1)

#define SCR_MAIN_CARD_SECTION_GAP  11
#define SCR_MAIN_SMALL_CARD_H      56

#define SCR_MAIN_NUMBER_CACHE_W    720
#define SCR_MAIN_NUMBER_CACHE_H    100
#define SCR_MAIN_BG_CACHE_W        UI_DIM_SCREEN_W
#define SCR_MAIN_BG_CACHE_H        UI_DIM_SCREEN_H
```

---

## 2. Statická data (iterace 1)

```c
// app/src/screens/screen_main_data.c
#include "screen_main.h"
#include <ui/digit_group.h>

// ── Header ─────────────────────────────────────────────────
const char *SCR_S_GNSS_LOCK = "GNSS LOCK";
const char *SCR_S_SYS_READY = "SYS READY";
const char *SCR_S_SAT_VAL   = "9";
const char *SCR_S_HDOP_L    = "HDOP";
const char *SCR_S_HDOP_V    = "0,8";
const char *SCR_S_LOC_L     = "LOC";
const char *SCR_S_LOC_V     = "JN89NS";
const char *SCR_S_CAL_L     = "CAL";
const char *SCR_S_CAL_V     = "4 min";
const char *SCR_S_HOLD_L    = "HOLD";
const char *SCR_S_HOLD_V    = "2h 14m";
const char *SCR_S_TIME      = "14:32:07";
const char *SCR_S_DATE      = "Út · 2026-06-16 · UTC";

// ── Title row ──────────────────────────────────────────────
const char *SCR_S_TITLE_FREQ = "FREQUENCY";
const char *SCR_S_TITLE_CHAN = "CH B";
const char *SCR_S_TITLE_GATE = "GATE 1 s";
const char *SCR_S_TITLE_RIGHT= "N 312 · Ω regrese";

// ── Hlavní číslo: 99 999 999 999 88 5 6 Hz, 12 jistých digitů ───
const ui_digit_segment_t SCR_MAIN_DIGITS[] = {
    {"99",  UI_DIGIT_CERTAIN, false},
    {"999", UI_DIGIT_CERTAIN, false},
    {"999", UI_DIGIT_CERTAIN, false},
    {"999", UI_DIGIT_CERTAIN, false},
    {"88",  UI_DIGIT_CERTAIN, true},   // underline = poslední jistý
    {"5",   UI_DIGIT_SIGMA,   false},
    {"6",   UI_DIGIT_FLOOR,   false},
};
const int16_t SCR_MAIN_DIGIT_COUNT = 7;
const char  *SCR_MAIN_SEPS = "··,·";        // 4 separátory mezi 5 skupinami
const char  *SCR_S_UNIT_HZ = "Hz";

// ── Karty ──────────────────────────────────────────────────
const char *SCR_S_OFFSET_L = "Offset";
const char *SCR_S_OFFSET_V = "+1,2×10⁻¹¹";
const char *SCR_S_SIGMA_L  = "σy @ τ = 1 s";
const char *SCR_S_SIGMA_V  = "±2,1×10⁻¹²";
const char *SCR_S_TREND_L  = "Trend 60 s";
const char *SCR_S_TREND_R  = "● stabilní · ±2,4×10⁻¹² p–p";
const char *SCR_S_PERIOD_L = "Perioda T";
const char *SCR_S_PERIOD_V = "10,000 000 ns";
const char *SCR_S_REF_L    = "Reference";
const char *SCR_S_REF_V    = "GPSDO 10 MHz";

// ── Allan křivka — body v pixelech inner rectu grafu (320×130) ──
const prim_point_t SCR_ALLAN_CURVE[] = {
    {30, 16}, {66, 30}, {100, 50}, {135, 60}, {170, 66},
    {205, 72}, {240, 76}, {275, 76}, {310, 74},
};
const int16_t SCR_ALLAN_CURVE_COUNT = 9;

const char *SCR_ALLAN_X_TICKS[] = {"0,1", "1", "10", "100", "1k"};
const char *SCR_ALLAN_Y_TICKS[] = {"10⁻⁹", "10⁻¹⁰", "10⁻¹¹", "10⁻¹²", "10⁻¹³"};
const char *SCR_ALLAN_X_LABEL   = "τ [s]";
const char *SCR_ALLAN_CURSOR_L  = "τ = 1 s";

// ── Sparkline (21 vzorků, normalizováno 0..255) ───────────
const int16_t SCR_SPARKLINE_VALUES[] = {
    115, 140, 102, 153, 122, 148, 97, 140, 166, 115,
    148, 128, 153, 133, 107, 140, 128, 153, 122, 140, 133,
};
const int16_t SCR_SPARKLINE_COUNT = 21;
const int16_t SCR_SPARKLINE_SIGMA_MIN = 107;
const int16_t SCR_SPARKLINE_SIGMA_MAX = 148;

// ── Footer tlačítka ────────────────────────────────────────
const char *SCR_S_BTN_FREQ     = "FREQ";
const char *SCR_S_BTN_PERIOD   = "PERIOD";
const char *SCR_S_BTN_TIME_INT = "TIME INT";
const char *SCR_S_BTN_RUN      = "▶ RUN";
const char *SCR_S_BTN_GATE_L   = "GATE";
const char *SCR_S_BTN_GATE_V   = "1 s";
const char *SCR_S_BTN_CHAN_L   = "CHAN";
const char *SCR_S_BTN_CHAN_V   = "CH B";
const char *SCR_S_BTN_MENU     = "≡ MENU";
```

---

## 3. Render API

```c
// app/src/screens/screen_main.h
#pragma once

void screen_main_init(void);          // pre-render statických prvků
void screen_main_render(void);        // hlavní render
void screen_main_invalidate(void);    // vynutit re-render cache (např. po teme změně)
```

---

## 4. Render funkce — deklarativní layout

```c
// app/src/screens/screen_main.c
#include "screen_main.h"
#include <ui/ui.h>
#include <prim/fb.h>
#include <prim/fill.h>

// Cache buffery v SDRAM
static prim_color_t bg_cache[SCR_MAIN_BG_CACHE_W * SCR_MAIN_BG_CACHE_H]
    __attribute__((section(".sdram")));
static prim_color_t number_cache[SCR_MAIN_NUMBER_CACHE_W * SCR_MAIN_NUMBER_CACHE_H]
    __attribute__((section(".sdram")));
static bool cache_initialized = false;

void screen_main_init(void) {
    if (cache_initialized) return;

    // 1. Pozadí — radial gradient do bg_cache
    render_background_to_cache();

    // 2. Hlavní číslo do number_cache
    render_main_number_to_cache();

    cache_initialized = true;
}

void screen_main_render(void) {
    if (!cache_initialized) screen_main_init();

    // ── 1. Pozadí ────────────────────────────────────────
    prim_blit((prim_rect_t){0, 0, UI_DIM_SCREEN_W, UI_DIM_SCREEN_H},
              bg_cache, UI_DIM_SCREEN_W * sizeof(prim_color_t));

    // ── 2. Header ────────────────────────────────────────
    render_header();

    // ── 3. Body ──────────────────────────────────────────
    render_body_title();
    render_body_number();    // blit z cache
    render_body_grid();

    // ── 4. Footer ────────────────────────────────────────
    render_footer();
}

// ──────────────────────────────────────────────────────────
//  HEADER
// ──────────────────────────────────────────────────────────
static void render_header(void) {
    // Pozadí gradient (součást bg_cache, nepřekreslujeme)

    // Spodní hraniční čára
    ui_card_t header_line = {
        .rect = {0, UI_DIM_HEADER_H - 1, UI_DIM_SCREEN_W, 1}
    };
    // Místo karty stačí prim_fill, ale to v app nemůžeme — proto použijeme
    // ui_card_render_chrome s nulovým radiusem, nebo zavedeme ui_divider_line.
    // V této iteraci je gradient + line součástí bg_cache.

    // Pilulky zleva
    int16_t x = UI_DIM_PADDING_X;
    int16_t y = (UI_DIM_HEADER_H - UI_DIM_PILL_H) / 2;

    // 1. GNSS LOCK
    ui_pill_t p = {.x=x, .y=y, .variant=UI_PILL_OK,
                   .value=SCR_S_GNSS_LOCK, .has_led=true};
    ui_pill_render(&p);
    x += p.computed_width + UI_DIM_PILL_GAP;

    // 2. SYS READY
    p = (ui_pill_t){.x=x, .y=y, .variant=UI_PILL_OK, .value=SCR_S_SYS_READY};
    ui_pill_render(&p);
    x += p.computed_width + UI_DIM_PILL_GAP;

    // 3. Satelity (s ikonou parabolické antény)
    p = (ui_pill_t){.x=x, .y=y, .variant=UI_PILL_NORMAL,
                    .value=SCR_S_SAT_VAL,
                    .icon_render=ui_icon_sat_dish, .icon_size=22,
                    .icon_color=UI_COLOR_OK_SOFT};
    ui_pill_render(&p);
    x += p.computed_width + UI_DIM_PILL_GAP;

    // 4. HDOP
    p = (ui_pill_t){.x=x, .y=y, .variant=UI_PILL_NORMAL,
                    .label=SCR_S_HDOP_L, .value=SCR_S_HDOP_V};
    ui_pill_render(&p);
    x += p.computed_width + UI_DIM_PILL_GAP;

    // 5. LOC
    p = (ui_pill_t){.x=x, .y=y, .variant=UI_PILL_NORMAL,
                    .label=SCR_S_LOC_L, .value=SCR_S_LOC_V};
    ui_pill_render(&p);
    x += p.computed_width + UI_DIM_PILL_GAP;

    // 6. CAL
    p = (ui_pill_t){.x=x, .y=y, .variant=UI_PILL_NORMAL,
                    .label=SCR_S_CAL_L, .value=SCR_S_CAL_V};
    ui_pill_render(&p);
    x += p.computed_width + UI_DIM_PILL_GAP;

    // 7. HOLD
    p = (ui_pill_t){.x=x, .y=y, .variant=UI_PILL_NORMAL,
                    .label=SCR_S_HOLD_L, .value=SCR_S_HOLD_V};
    ui_pill_render(&p);

    // Čas a datum, zarovnáno vpravo
    int16_t time_x = UI_DIM_SCREEN_W - UI_DIM_PADDING_X;
    prim_draw_text((prim_point_t){time_x, 22}, SCR_S_TIME,
                   &ui_font_mono_25, UI_COLOR_INK, PRIM_ALIGN_RIGHT);
    prim_draw_text((prim_point_t){time_x, 40}, SCR_S_DATE,
                   &ui_font_sans_14, UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
}

// ──────────────────────────────────────────────────────────
//  BODY — title row
// ──────────────────────────────────────────────────────────
static void render_body_title(void) {
    int16_t x = UI_DIM_PADDING_X + 4;
    int16_t y = SCR_MAIN_TITLE_Y;

    x = draw_mono_word(x, y, SCR_S_TITLE_FREQ, &ui_font_mono_20, UI_COLOR_ACC);
    x = draw_mono_word(x, y, " · ",            &ui_font_mono_20, UI_COLOR_LINE_HI);
    x = draw_mono_word(x, y, SCR_S_TITLE_CHAN, &ui_font_mono_20, UI_COLOR_INK_2);
    x = draw_mono_word(x, y, " · ",            &ui_font_mono_20, UI_COLOR_LINE_HI);
    x = draw_mono_word(x, y, SCR_S_TITLE_GATE, &ui_font_mono_20, UI_COLOR_INK_2);

    prim_draw_text((prim_point_t){UI_DIM_SCREEN_W - UI_DIM_PADDING_X, y},
                   SCR_S_TITLE_RIGHT, &ui_font_mono_18,
                   UI_COLOR_INK_3, PRIM_ALIGN_RIGHT);
}

// Helper: render text, return new x (right edge)
static int16_t draw_mono_word(int16_t x, int16_t y, const char *text,
                              const prim_font_t *font, prim_color_t color) {
    prim_draw_text((prim_point_t){x, y}, text, font, color, PRIM_ALIGN_LEFT);
    return x + prim_text_width(text, font);
}

// ──────────────────────────────────────────────────────────
//  BODY — main number (z cache)
// ──────────────────────────────────────────────────────────
static void render_body_number(void) {
    int16_t cache_x = (UI_DIM_SCREEN_W - SCR_MAIN_NUMBER_CACHE_W) / 2;
    int16_t cache_y = SCR_MAIN_NUMBER_Y_BASELINE - 80;  // baseline - cache height
    prim_blit((prim_rect_t){cache_x, cache_y,
                            SCR_MAIN_NUMBER_CACHE_W, SCR_MAIN_NUMBER_CACHE_H},
              number_cache,
              SCR_MAIN_NUMBER_CACHE_W * sizeof(prim_color_t));
}

static void render_main_number_to_cache(void) {
    // Pre-render do number_cache buffer.
    // Setup target = number_cache, render, restore.

    prim_fb_t cache_fb;
    prim_fb_init(&cache_fb, number_cache,
                 SCR_MAIN_NUMBER_CACHE_W, SCR_MAIN_NUMBER_CACHE_H,
                 SCR_MAIN_NUMBER_CACHE_W * sizeof(prim_color_t));
    prim_fb_t *prev_target = prim_get_target();
    prim_set_target(&cache_fb);

    // Vyplň pozadí transparentní (pro alpha blending při blit nemusí být,
    // ale můžeme dát BG_0 a budeme blittovat replace).
    prim_fill_rect((prim_rect_t){0, 0, SCR_MAIN_NUMBER_CACHE_W,
                                  SCR_MAIN_NUMBER_CACHE_H},
                   UI_COLOR_BG_0, PRIM_BLEND_REPLACE);

    // Big number komponenta
    ui_big_number_t n = {
        .x_center      = SCR_MAIN_NUMBER_CACHE_W / 2,
        .y_baseline    = 80,
        .main_font     = &ui_font_mono_75,
        .sep_font      = &ui_font_mono_25,
        .decimal_font  = &ui_font_mono_30,
        .unit_font     = &ui_font_sans_20,
        .segments      = SCR_MAIN_DIGITS,
        .segment_count = SCR_MAIN_DIGIT_COUNT,
        .separators    = SCR_MAIN_SEPS,
        .sep_color     = UI_COLOR_INK_3,
        .decimal_color = UI_COLOR_ACC,
        .unit          = SCR_S_UNIT_HZ,
        .unit_color    = UI_COLOR_INK_2,
    };
    ui_big_number_render(&n);

    prim_set_target(prev_target);
}

// ──────────────────────────────────────────────────────────
//  BODY — grid (Allan + right column)
// ──────────────────────────────────────────────────────────
static void render_body_grid(void) {
    int16_t grid_y = SCR_MAIN_GRID_Y;
    int16_t grid_h = UI_DIM_BODY_H - (SCR_MAIN_GRID_Y - UI_DIM_BODY_Y) - 12;
    int16_t grid_w = UI_DIM_SCREEN_W - 2*UI_DIM_PADDING_X - 4;
    int16_t left_w = (grid_w * SCR_MAIN_GRID_LEFT_RATIO) / 100;
    int16_t right_w = grid_w - left_w - SCR_MAIN_GRID_GAP;
    int16_t left_x = UI_DIM_PADDING_X + 4;
    int16_t right_x = left_x + left_w + SCR_MAIN_GRID_GAP;

    render_card_allan((prim_rect_t){left_x, grid_y, left_w, grid_h});
    render_right_column((prim_rect_t){right_x, grid_y, right_w, grid_h});
}

static void render_card_allan(prim_rect_t rect) {
    ui_card_t card = {
        .rect = rect,
        .header_label = "Allan σy(τ)",
        .header_right = "@ 1 s · 3,5×10⁻¹²",
        .header_right_accent = UI_COLOR_ACC,
    };
    ui_card_render_chrome(&card);

    prim_rect_t inner = ui_card_inner_rect(&card);

    // Mřížka + popisky os
    ui_chart_loglog_t chart = {
        .inner = inner,
        .x_decade_count = 4,
        .y_decade_count = 4,
        .x_min_exp = -1,
        .y_min_exp = -13,
        .x_label = SCR_ALLAN_X_LABEL,
        .x_tick_labels = SCR_ALLAN_X_TICKS,
        .y_tick_labels = SCR_ALLAN_Y_TICKS,
    };
    ui_chart_loglog_render_grid(&chart);

    // GPSDO floor čára (y v pixelech od inner.y)
    int16_t floor_y = inner.y + (inner.h * 3) / 4;
    ui_chart_loglog_render_floor(&chart, floor_y, "GPSDO floor");

    // Křivka (body v pixelech od inner.x, inner.y)
    ui_chart_loglog_render_curve(&chart, SCR_ALLAN_CURVE, SCR_ALLAN_CURVE_COUNT,
                                 UI_COLOR_ACC, true);

    // Kurzor τ=1 s
    ui_chart_loglog_render_cursor(&chart,
        (prim_point_t){inner.x + 100, inner.y + 50}, SCR_ALLAN_CURSOR_L);
}

static void render_right_column(prim_rect_t rect) {
    int16_t small_h = SCR_MAIN_SMALL_CARD_H;
    int16_t gap = SCR_MAIN_CARD_SECTION_GAP;
    int16_t big_h = rect.h - 2*small_h - 2*gap;

    // Sekce A: Offset + σy
    render_two_small_cards(
        (prim_rect_t){rect.x, rect.y, rect.w, small_h},
        SCR_S_OFFSET_L, SCR_S_OFFSET_V, UI_COLOR_OK,
        SCR_S_SIGMA_L,  SCR_S_SIGMA_V,  UI_COLOR_VIOLET);

    // Sekce B: Trend
    render_card_trend(
        (prim_rect_t){rect.x, rect.y + small_h + gap, rect.w, big_h});

    // Sekce C: Perioda + Reference
    render_two_small_cards(
        (prim_rect_t){rect.x, rect.y + small_h + gap + big_h + gap,
                      rect.w, small_h},
        SCR_S_PERIOD_L, SCR_S_PERIOD_V, UI_COLOR_INK,
        SCR_S_REF_L,    SCR_S_REF_V,    UI_COLOR_INK);
}

// ──────────────────────────────────────────────────────────
//  FOOTER
// ──────────────────────────────────────────────────────────
static void render_footer(void) {
    int16_t fy = UI_DIM_SCREEN_H - UI_DIM_FOOTER_H;
    int16_t pad = 16;
    int16_t total_w = UI_DIM_SCREEN_W - 2*pad - 6*UI_DIM_BUTTON_GAP;
    int16_t unit = total_w * 10 / 73;     // 7.3 jednotek celkem
    int16_t btn_h = UI_DIM_FOOTER_H - 22;
    int16_t btn_y = fy + 11;
    int16_t x = pad;

    struct btn_def {
        const char *label; const char *value;
        ui_button_variant_t variant; int16_t w;
    } btns[7] = {
        {SCR_S_BTN_FREQ,     NULL,           UI_BUTTON_ACTIVE, unit},
        {SCR_S_BTN_PERIOD,   NULL,           UI_BUTTON_NORMAL, unit},
        {SCR_S_BTN_TIME_INT, NULL,           UI_BUTTON_NORMAL, unit},
        {SCR_S_BTN_RUN,      NULL,           UI_BUTTON_RUN,    (int16_t)(unit*13/10)},
        {SCR_S_BTN_GATE_L,   SCR_S_BTN_GATE_V, UI_BUTTON_NORMAL, unit},
        {SCR_S_BTN_CHAN_L,   SCR_S_BTN_CHAN_V, UI_BUTTON_NORMAL, unit},
        {SCR_S_BTN_MENU,     NULL,           UI_BUTTON_NORMAL, unit},
    };

    for (int i = 0; i < 7; i++) {
        ui_button_t b = {
            .rect={x, btn_y, btns[i].w, btn_h},
            .variant=btns[i].variant,
            .label=btns[i].label,
            .value=btns[i].value,
        };
        ui_button_render(&b);
        x += btns[i].w + UI_DIM_BUTTON_GAP;
    }
}
```

---

## 5. Hot-path optimalizace v této obrazovce

Jelikož je iterace 1 statická:

1. **Pre-render při `screen_main_init()`:**
   - Pozadí (radial gradient) → `bg_cache` (1,5 MB SDRAM).
   - Hlavní číslo → `number_cache` (290 KB SDRAM).
2. **`screen_main_render()`:**
   - 2× `prim_blit` z cache (rychlé).
   - Zbytek živě renderovaný (header, karty, graf, sparkline, footer) — všechno
     opakovatelné rychle.

Cílový čas opakovaného renderu: **≤ 40 ms.**

V budoucích iteracích, kdy hodnoty budou živé:
- `bg_cache` zůstává.
- `number_cache` se invalidovat při změně hodnoty → `screen_main_invalidate()`.
- Karty Offset/σy/Trend renderovat jen při změně dat (dirty tracking).
- Statické tlačítka přeskakovat `skip-on-equal` pravidlem.

---

## 6. Build (app/CMakeLists.txt zlomek)

```cmake
# app/CMakeLists.txt
add_executable(gpsdo_counter
    src/main.c
    src/cli/cli.c
    src/screens/screen_main.c
    src/screens/screen_main_data.c
)

# HAL implementace per-platform
if(CMAKE_CROSSCOMPILING)
    target_sources(gpsdo_counter PRIVATE
        src/hal/stm32/display.c
        src/hal/stm32/dma2d.c
        src/hal/stm32/uart.c
        src/hal/stm32/mpu.c
    )
else()
    target_sources(gpsdo_counter PRIVATE
        src/hal/host/display.c
        src/hal/host/dma2d.c
        src/hal/host/uart.c
    )
endif()

target_link_libraries(gpsdo_counter PRIVATE ui prim)

target_include_directories(gpsdo_counter PRIVATE
    src
    src/screens
    src/hal
    src/cli
)

set_target_properties(gpsdo_counter PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)
```

---

## 7. Akceptační kritéria APP

1. **Pixel diff < 0,5 %** vs. `docs/reference_v9.png` (host test).
2. **Žádné `prim_*` volání** v `screens/screen_main.c` kromě `prim_blit`,
   `prim_draw_text`, `prim_set_target`, `prim_get_target`, `prim_text_width`,
   `prim_fb_init` (lint check). Vše ostatní jde přes `ui_*`.
3. **Žádné magic numbers** mimo `screen_main.h` a `theme.h`/`dimensions.h`.
4. **První render ≤ 100 ms**, opakovaný ≤ 40 ms.
5. **`screen_main_init()` volaný jen jednou** při bootu, neopakovaně.

---

## 8. Co tato vrstva NEdělá

- **Žádné výpočty.** Vše natvrdo v `screen_main_data.c`.
- **Žádný stavový stroj** mezi obrazovkami — v této iteraci je jen jedna.
- **Žádné dotyky, animace, FPGA komunikace.**
