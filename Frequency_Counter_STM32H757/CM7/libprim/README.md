# libprim — primitive 2D renderer

Generic, stateless 2D graphics stack for embedded targets with an **RGB565**
framebuffer: fills, lines/arcs/circles, composite paths, gradients, glow and
UTF-8 text. Reusable in any project — it knows nothing about UI components,
palettes or the GPSDO counter.

`prim_color_t` is ARGB8888 used **only for in-memory alpha compositing**; the
framebuffer and all buffers are RGB565 and libprim packs to RGB565 on write
(RGB888 is never materialized).

## Dependencies

None beyond libc (`stdint`, `stdbool`, `stddef`, `string`, `math`). DMA2D
acceleration is optional and injected by the application via `prim/accel.h`;
without a backend, everything runs on the software path.

## Build (host, with tests)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug      # from the CM7/ directory
cmake --build build
ctest --test-dir build                       # libprim::test_*
```

On STM32 the library is compiled in-tree by STM32CubeIDE; see
`../GPSDO_UI_README.md` for include-path setup and the DMA2D bridge.

## Minimal example

```c
#include <prim/prim.h>

static prim_pixel_t fbmem[320 * 240];   /* RGB565 */

int main(void) {
    prim_fb_t fb;
    prim_fb_init(&fb, fbmem, 320, 240, 320 * sizeof(prim_pixel_t));
    prim_set_target(&fb);

    prim_fill_rect((prim_rect_t){0, 0, 320, 240},
                   PRIM_RGB(0x06, 0x09, 0x0E), PRIM_BLEND_REPLACE);
    prim_fill_circle((prim_point_t){160, 120}, 40, PRIM_RGB(0x38, 0xBD, 0xF8));
    return 0;
}
```

## API docs

Public headers in `include/prim/`; the umbrella is `<prim/prim.h>`. Doxygen:
`doxygen Doxyfile` → `docs/api/html/`.
