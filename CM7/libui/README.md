# libui — UI component library

The project's visual vocabulary: palette, dimensions, components (pill, card,
button, chart, sparkline, digit group, big number), vector icons and vector
fonts. Reusable in any project that wants the same look & feel.

## Dependencies

Only **libprim**. No LVGL, no other third-party libraries.

## Build (host, with tests)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug      # from the CM7/ directory
cmake --build build
ctest --test-dir build                       # ui::test_*
```

Fonts are generated (not hand-written). Regenerate from TTF:

```bash
node tools/font_gen/gen_fonts.js             # → src/fonts/ui_font_*.c (bpp4)
```

## Minimal example

```c
#include <ui/ui.h>
#include <prim/prim.h>

void draw_status(prim_fb_t *fb) {
    prim_set_target(fb);

    ui_pill_t p = { .x = 22, .y = 13, .variant = UI_PILL_OK,
                    .value = "GNSS LOCK", .has_led = true };
    ui_pill_render(&p);                       /* p.computed_width is filled in */

    ui_card_t card = { .rect = {22, 60, 360, 180},
                       .header_label = "Allan σy(τ)" };
    ui_card_render_chrome(&card);
}
```

## API docs

Umbrella `<ui/ui.h>`. Colors only via `UI_COLOR_*` (theme.h), dimensions via
`UI_DIM_*` (dimensions.h). Doxygen: `doxygen Doxyfile`.
