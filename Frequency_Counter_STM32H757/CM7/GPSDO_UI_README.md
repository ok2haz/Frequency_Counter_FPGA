# GPSDO UI — three-layer renderer (libprim / libui / app)

Strukturovaná grafická vrstva pro GPSDO čítač dle `.claude/zadani_*.md`, postavená
**in-place** v CM7 projektu. Tři vrstvy s tvrdými hranicemi:

```
app/      GPSDO main screen + HAL bridge   (specifické pro přístroj)
 └─ libui/   vizuální slovník: theme, dimensions, komponenty, ikony, fonty
     └─ libprim/  generický 2D renderer: fill, shapes, path, gradient, glow, text
```

`libprim` nezná `ui/*`, `libui` nezná `app/*`. Klient zahrnuje jen umbrella
(`<prim/prim.h>`, `<ui/ui.h>`). Symboly: `prim_*` / `ui_*`, visibility `hidden`,
veřejné API přes `PRIM_API` / `UI_API`.

## ⚠️ Barevný model — RGB565 mandatory, RGB888 se nepoužívá

- **Framebuffer, všechny cache buffery i DSI pipeline = RGB565** (jako stávající
  projekt; DSI BURST + RGB565 je hard-won, viz hlavní `CLAUDE.md`).
- `prim_color_t` je **ARGB8888 jen jako pracovní barva v paměti** pro alfa
  matematiku (glow, AA hran, `PRIM_BLEND_OVER`, A8 fonty). Na hranici
  framebufferu se balí do RGB565 (`prim_argb_to_565` / `prim_blend565`).
- **RGB888 se nikde v paměti ani na sběrnici nematerializuje.**

## Adresářová struktura (pod `CM7/`)

```
CM7/
├── libprim/include/prim/*.h     # public API (api,types,fb,accel,fill,shapes,
│   │                            #   path,gradient,glow,text,font_data,prim)
│   ├── src/*.c                  # fb,fill,shapes,path,gradient,glow,text,prim
│   ├── src/internal/*.{h,c}     # private: bezier,rasterizer,alpha_blend,
│   │                            #   dma2d_backend,fb_impl,font_impl,path_impl
│   ├── VERSION  CMakeLists.txt
├── libui/include/ui/*.h         # theme,dimensions,fonts,pill,card,button,chart,
│   │                            #   sparkline,digit_group,big_number,icons,ui
│   ├── src/*.c  src/internal/*.h
│   ├── src/fonts/*.c            # GENEROVANÉ (bpp4) — viz níže
│   ├── tools/font_gen/gen_fonts.js
│   ├── VERSION  CMakeLists.txt
├── app/
│   ├── app_gpsdo.{c,h}          # glue: init + render/clear
│   ├── screens/screen_main.{c,h}, screen_main_data.c
│   └── hal/stm32/prim_stm32_hal.{c,h}   # FB binding + DMA2D backend
├── cmake/  CMakeLists.txt
```

## Fonty — regenerace (bpp4, nativní prim formát)

Fonty jsou **vygenerované** z TTF přes opentype.js do nativního `prim_font_t`
(bpp4, MSB-first, bez paddingu řádků). libui tím **nezávisí na LVGL**.

```powershell
# z kořene repa:
& ".\tools\node-v20.18.1-win-x64\node.exe" ".\CM7\libui\tools\font_gen\gen_fonts.js"
```

Generuje 13× `CM7/libui/src/fonts/ui_font_*.c`. `ui_font_mono_75` je omezen na
číslice 0–9 (jen hlavní číslo) → ~10 KB místo ~393 KB. Znaková sada: ASCII,
Latin-ext (česká diakritika), ±·×°, řecká, horní indexy, šipky, ●▶≡.

## Build

### Knihovny + host testy (CMake, mimo CubeIDE)
```bash
cmake -B build/host -DCMAKE_BUILD_TYPE=Debug      # z adresáře CM7/
cmake --build build/host
```
`libprim` se софtwarově sestaví bez DMA2D (host path). `libui` linkuje `prim`.

### Firmware na STM32 (CubeIDE)
Toolchain `arm-none-eabi` ani CMake nejsou v tomto prostředí — **on-target build
běží v CubeIDE**. Integrace (jednorázově):

1. **Add include paths** (Project → C/C++ Build → Settings → MCU GCC Compiler →
   Include paths): `CM7/libprim/include`, `CM7/libui/include`,
   `CM7/libprim/src` (kvůli internal headerům), `CM7/app`.
2. **Add source folders** do buildu: `CM7/libprim/src`, `CM7/libui/src`,
   `CM7/app` (Project → Properties → C/C++ General → Paths and Symbols → Source
   Location, nebo přesuň do existující zdrojové složky).
3. **Linker `.sdram` sekce** — `glow.c` a `screen_main.c` alokují scratch buffery
   (~1,68 MB) do sekce `.sdram`. **Už přidáno** do `STM32H757BITX_FLASH.ld`:
   MEMORY region `SDRAM @0xC0800000 (16M)` + `.sdram (NOLOAD)` sekce. Je to volné
   SDRAM nad bignum regionem; mimo MPU R0/R1 → default mapa = Device paměť
   (přístupné, DMA2D-koherentní, necachované). Při regeneraci linkeru z CubeMX
   tu sekci znovu přidej (CubeMX ji nepřepisuje, ale ověř).
4. **Volání** z `StartUartTask` / `UiTask` (LVGL zůstává paralelně):
   ```c
   #include "app/app_gpsdo.h"
   // nový UART příkaz, např. "screen main2":
   app_gpsdo_render_main();   // musí běžet z jednoho kontextu (UiTask)
   ```
   `app_gpsdo_init()` se zavolá interně jednorázově (bind FB @0xC0000000 +
   DMA2D backend + pre-render cache).

## Stav a co ověřit při prvním buildu

- **Žádný soubor nebyl zkompilován** v tomto prostředí (chybí toolchain).
  Kód je psán konzistentně, ale první build v CubeIDE doladí drobnosti.
- Zkontroluj: úhlová konvence oblouků (`prim_draw_arc`, rohy zaoblených rámečků),
  baseline/`oy` posuny textu (font metrika), DMA2D registrová sekvence
  (`prim_stm32_hal.c`) vs. tvoje gfx.c — pokud výplně blbnou, `prim_stm32_use_dma2d(0)`
  shodí na SW cestu.
- Pixel-perfektnost vůči `reference_v9.png` je věc dolaďování souřadnic v
  `screen_main.c` + font metrik.

## Verze
libprim 0.1.0, libui 0.1.0 (semver; pre-1.0 API smí drobně fluktuovat).
Licence knihoven: MIT (dle zadání).
