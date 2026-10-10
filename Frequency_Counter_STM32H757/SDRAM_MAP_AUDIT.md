# Audit využití pamětí — SDRAM + interní RAM (STATUS #71)

> **Zdroj pravdy:** `CM7/Debug/H757_LED_CM7.map` (Debug/-O0), `CM7/STM32H757BITX_FLASH.ld`,
> `main.c MPU_Config`, pevné adresy ve `prim_stm32_hal.c`/`membench.c`/`ltdc.c`.
> Provedeno 2026-08-28. **Závěr: rozložení je zdravé, žádná změna kódu není potřeba.**

## Otázka #71
1. Leží to, co MÁ být v SDRAM (bg_cache, glyph atlas, glow), skutečně tam — nebo omylem v interní RAM?
2. Kolik z 32 MB SDRAM je fakticky obsazeno?
3. Jsou kandidáti na přesun z interní RAM_D1 do SDRAM kvůli uvolnění interní paměti?

## (1) Co je v SDRAM — OVĚŘENO, vše sedí
Linker sekce `.sdram` @ `0xC0800000`, celkem **0x1B7000 = 1,79 MB**:

| Objekt | Velikost | Obsah |
|---|---|---|
| `prim_stm32_hal.o .sdram` | 256 KB (0x40000) | DMA2D glyph A8 atlas |
| `screen_main.o .sdram`    | 767 KB (0xBB800) | `bg_cache` (předrenderované pozadí, 800×480×2) |
| `glow.o .sdram`           | 767 KB (0xBB800) | libprim glow scratch |

✅ Vše, co CLAUDE.md „SDRAM mapa" slibuje, je v SDRAM. **ADEV/trend decimační pyramidy jsou
ZÁMĚRNĚ v RAM_D1** (`s_adev`/`s_tr` ve `screen_main.o .bss`, ~4,7 kB) — malé, rychlé,
cacheable; do SDRAM nepatří. Nic velkého neskončilo omylem v interní RAM.

## (2) Obsazení 32 MB SDRAM (`0xC0000000`–`0xC2000000`)
Framebuffery a scratch se umisťují **pevnou adresou** (compile-time), ne linkerem:

| Rozsah | Velikost | Obsah | Mechanismus |
|---|---|---|---|
| `0xC0000000` | 4 MB | FB0/FB1/FB2 (3×1 MB) + canvas pool (1 MB) | pevná adresa, MPU R0 (WT) |
| `0xC0400000` | 4 MB (region) | sdram test/scratch — reálně dotčeno **~512 kB** (membench/`sdram`/screenshot) | pevná adresa, MPU R1 (WBWA) |
| `0xC0800000` | 16 MB (okno) | `.sdram` sekce — reálně **1,79 MB** | linker NOLOAD |
| `0xC1800000` | 8 MB | **nikdy nemapováno linkerem** | — |

**Aktivně využito ≈ 6,3 MB z 32 MB → ~24–26 MB leží ladem.** Rezerva je obrovská:
`.sdram` okno má volných ~14 MB a nad ním je 8 MB úplně mimo linker.

## (3) Kandidáti na přesun z interní RAM — ANO existují, ale BEZ DŮVODU
Interní **RAM_D1** (AXI SRAM, 512 KB): `.bss` **0x1FFF8 = 128 KB** + `.data` 912 B →
**~384 KB volných (75 %).** Největší konzumenti `.bss`:

| Objekt | Velikost | Poznámka k případnému přesunu |
|---|---|---|
| `heap_4.o` (FreeRTOS heap) | 32 KB | 🔴 NEpřesouvat — rychlá interní RAM pro tasky/mutexy |
| `sd_export.o` (SD_SPEED_BUF) | 32 KB | 🔴 NEpřesouvat — IDMA/cache-citlivý bounce buffer, SDRAM je jiná doména |
| `sensor_hist.o` | 15,5 KB | 🟡 přesun bezpečný (2 Hz, nekritický), ale **zbytečný** |

**Rozhodnutí: nic nepřesouvat.** Interní RAM má 384 KB volných → není žádný tlak. Přesun do
SDRAM by přidal FMC latenci (pomalejší, cachovaná dle MPU regionu) a u `sd_export`/DMA cílů
i riziko cache/DMA koherence, výměnou za uvolnění RAM, kterou nikdo nepotřebuje.

## Souvislosti
- **#72 (HW):** intermitentní alias adres po 2 MB v SDRAM (podezření `PF15`/FMC_A9) — HW nález,
  netýká se tohoto SW auditu; `membench fb_alias` ho hlídá.
- **#70:** benchmark pamětí (`membench`) — komplementární runtime test integrity, ne mapy.
