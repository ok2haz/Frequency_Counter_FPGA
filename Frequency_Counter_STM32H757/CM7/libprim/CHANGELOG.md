# Changelog — libprim

All notable changes to this library are documented here. Format loosely follows
Keep a Changelog; versioning is semver (see VERSION).

## [0.1.0] — 2026-06-17

Initial release.

### Added
- Framebuffer/target/clip management (`fb.h`) over an RGB565 pixel buffer.
- Color model: `prim_color_t` ARGB8888 compositing, packed to RGB565 on write
  (`prim_argb_to_565`, `prim_565_to_argb`, `prim_blend565`). RGB888 never used.
- Fills: `prim_fill_rect`, `prim_fill_rect_rounded`, `prim_stroke_rect_rounded`,
  `prim_blit` (DMA2D backend or software fallback).
- Shapes: anti-aliased `prim_draw_line`, `prim_draw_line_dashed`,
  `prim_draw_circle`, `prim_fill_circle`, `prim_draw_arc`.
- Opaque composite `prim_path_t` (move/line/quad/arc/close, stroke & fill).
- Gradients: linear and radial.
- Glow: separable box blur of a coverage mask.
- Text: UTF-8, binary-search glyph lookup, A8/2/4-bit coverage blit; public
  `prim_font_t` layout (`font_data.h`) so clients can define fonts.
- DMA2D acceleration via dependency injection (`accel.h`); software path when
  no backend is installed (host build).
