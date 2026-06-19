# Changelog — libui

Versioning is semver (see VERSION). Depends on libprim.

## [0.1.0] — 2026-06-17

Initial release.

### Added
- Palette (`theme.h`) and dimensions (`dimensions.h`) as the single sources of
  truth for color and layout.
- Components: pill, card, button, log-log chart, sparkline, digit group,
  big number — all stateless renderers parameterized by data.
- Vector icons (`icons.h`) drawn purely from libprim primitives (no bitmaps).
- Vector fonts (`fonts.h`): 13 descriptors (JetBrains Mono / Inter), generated
  from TTF into the native `prim_font_t` bpp4 format by
  `tools/font_gen/gen_fonts.js`. No LVGL dependency.
