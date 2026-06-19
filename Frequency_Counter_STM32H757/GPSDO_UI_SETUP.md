# GPSDO UI (LVGL v9) — detailní návod k zprovoznění

> **STAV (předkonfigurováno automaticky):** LVGL **v9.5.0** je vendorováno v
> `Middlewares/Third_Party/lvgl` (ořezáno na 28 MB). `.project` má folder-link na
> `lvgl/src`, `.cproject` má include cestu `../../Middlewares/Third_Party/lvgl` ve všech
> 4 konfiguracích, a `lv_conf.h` je v `Middlewares/Third_Party/` (LVGL ho najde defaultně —
> **`LV_CONF_INCLUDE_SIMPLE` netřeba**). Zálohy: `CM7/.project.bak_lvgl`, `CM7/.cproject.bak_lvgl`.
> **Zbývá:** v CubeIDE zavřít+otevřít projekt (re-načte `.project`/`.cproject`), Clean+Build,
> a (volitelně) vygenerovat fonty. Sekce 2–3 níže jsou tím pádem hotové (ponechány pro referenci).

Krok-za-krokem od čistého stromu projektu po vykreslenou statickou hlavní obrazovku
GPSDO čítače přes UART příkaz `screen main`. Cílová deska: **STM32H757BIT6**, jádro
**CM7**, IDE **STM32CubeIDE**. Displej Waveshare 800×480, framebuffer RGB565 @ `0xC0000000`.

> Proč ručně: samotná LVGL knihovna a vygenerované fonty nejsou v repu a build běží jen
> v IDE (toolchain mimo). Kód integrace (glue + screen + CLI) už hotový je — viz sekce 0.

---

## 0. Co už je v projektu hotové

| Soubor | Role |
|---|---|
| `CM7/Core/Inc/theme.h` | Paleta v9 (RGB888→RGB565) + role fontů (`FONTS_READY`) |
| `CM7/Core/Inc/lv_conf.h` | Konfigurace LVGL v9 (RGB565, 64 KB mem, montserrat_14) |
| `CM7/Core/Inc/lv_port_disp.h` + `CM7/Core/Src/lv_port_disp.c` | Display port: DIRECT render do `0xC0000000` |
| `CM7/Core/Inc/ui_main_screen.h` + `CM7/Core/Src/ui_main_screen.c` | Layout obrazovky (sekce 3 briefu) |
| `CM7/Core/Src/freertos.c` | UART příkazy `ping/screen main/clear/version/help` + screen-mode v UiTask |
| `tools/gen_fonts.ps1` | Generátor 19 LVGL fontů |

**Chybí dodat:** (1) LVGL v9 zdroje, (2) vygenerované fonty, (3) build+flash v IDE.

---

## 1. Nástroje

- **STM32CubeIDE** (build/flash CM7).
- **Node.js + lv_font_conv**: `npm i -g lv_font_conv`
- **Sériový terminál**: PuTTY / Tera Term / `pyserial` (115200 8N1).
- **TTF fonty**: JetBrains Mono (Regular/Medium/SemiBold/Bold) — OFL licence, Inter
  (Regular/Medium) — OFL. Stáhni z oficiálních repozitářů (JetBrains/rsms).

---

## 2. Vendorování LVGL v9

1. Stáhni LVGL **v9.x** (release ZIP nebo `git clone --branch release/v9.3 https://github.com/lvgl/lvgl`).
2. Zkopíruj do: `H757_LED/H757_LED/Middlewares/Third_Party/lvgl/`
   (tj. ať existuje `Middlewares/Third_Party/lvgl/lvgl.h` a `Middlewares/Third_Party/lvgl/src/...`).
3. **Smaž / vylouč z buildu** podsložky, co nepotřebuješ a tahají závislosti:
   `lvgl/examples`, `lvgl/demos`, `lvgl/tests`, `lvgl/docs`, `lvgl/scripts`.

---

## 3. Zapojení do buildu (CubeIDE)

### 3a. Include cesty
Project → pravý klik → **Properties → C/C++ General → Paths and Symbols → Includes → GNU C**:
- Přidej `../Middlewares/Third_Party/lvgl`  ← aby `#include "lvgl.h"` našel `lvgl/lvgl.h`.
  > Pozn.: kód používá `#include "lvgl.h"`; include path musí ukazovat na složku, kde
  > `lvgl.h` přímo leží (kořen `lvgl/`). `CM7/Core/Inc` (kde je `lv_conf.h`, `theme.h`)
  > už na include path je.

### 3b. lv_conf.h
LVGL hledá `lv_conf.h` přes `lv_conf_internal.h`. Nejjednodušší:
- Properties → **C/C++ General → Paths and Symbols → Symbols → GNU C → Add**:
  - `LV_CONF_INCLUDE_SIMPLE`  (bez hodnoty)
- `lv_conf.h` nech v `CM7/Core/Inc/` (už tam je, na include path).

### 3c. Zdroje LVGL do překladu
CubeIDE překládá soubory pod „source location". Middlewares složka většinou source
location není → přidej ji:
- Properties → **C/C++ General → Paths and Symbols → Source Location → Add Folder…**
  → vyber `Middlewares/Third_Party/lvgl/src`.
- Ověř, že `examples/demos/tests` jsou **Excluded from build** (pravý klik na složku →
  Resource Configurations → Exclude from Build → zaškrtni Debug i Release).

### 3d. Naše nové .c soubory
`lv_port_disp.c` a `ui_main_screen.c` jsou v `CM7/Core/Src/` → překládají se automaticky.
Zkontroluj, že je IDE „vidí" (Refresh F5).

---

## 4. Fonty

1. `npm i -g lv_font_conv`
2. Vlož TTF do `H757_LED/H757_LED/tools/ttf/`:
   `JetBrainsMono-Regular.ttf`, `-Medium.ttf`, `-SemiBold.ttf`, `-Bold.ttf`,
   `Inter-Regular.ttf`, `Inter-Medium.ttf`
3. Spusť:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\H757_LED\H757_LED\tools\gen_fonts.ps1
   ```
   → vznikne 19 `.c` v `CM7/Core/Src/ui/fonts/`.
4. Přidej `CM7/Core/Src/ui/fonts` do buildu (Source Location, jako 3c — pokud `ui/` ještě
   není pod source location; `Core/Src` obvykle je, takže podsložka se vezme automaticky).
5. V `theme.h` přepni:
   ```c
   #define FONTS_READY 1
   ```
   Tím se role `F_*` napojí na reálné fonty (75px číslo, ⁻¹², σ/τ/Ω/▶/≡/●…).

> **Dokud fonty nemáš:** nech `FONTS_READY 0` — vše jede na vestavěném `montserrat_14`.
> Layout/pozice sedí, ale velikosti ne a speciální glyphy (super­skripty, řecká písmena,
> ▶/≡/●) se nevykreslí (montserrat_14 je nemá). To je OK pro první ověření, že kreslení žije.

---

## 5. Paměť a MPU (kontrola)

- **LVGL heap** (objekty obrazovky) = statické pole `LV_MEM_SIZE` (64 KB v `lv_conf.h`) →
  jde do `.bss` v interní SRAM. Pokud linker hlásí přetečení RAM, sniž na 48 KB nebo
  přesměruj `.bss` do AXI SRAM (D1).
- **Framebuffer** zůstává v SDRAM @ `0xC0000000`, region **Write-Through** (dle CLAUDE.md).
  LVGL renderuje CPU přímo do něj; WT zajistí koherenci s LTDC → **není potřeba cache clean**.
  `flush_cb` dělá jen `__DSB()`.
- Nech ostatní MPU/DSI/LTDC/TC358762 konfiguraci **beze změny** (hard-won, viz CLAUDE.md).

---

## 6. Build a flash

1. Vyber projekt **H757_LED_CM7**.
2. **Build** (Ctrl+B). První build vychytá:
   - chybějící include (`lvgl.h` / `lv_conf.h`) → zkontroluj sekci 3,
   - případné drobné odchylky LVGL **v9 API** (kód psán bez kompilace) → oprav dle hlášek.
3. **Run** (Ctrl+F11), konfigurace CM7. (CM4 beze změny.)

---

## 7. Test přes UART

- Terminál: **115200, 8N1, žádný handshake** (USART1, viz CLAUDE.md).
- Posílej příkazy zakončené **CRLF** (`\r\n`) nebo `\n`. V Tera Term: *Setup → Terminal →
  New-line Transmit = CR+LF*; v PuTTY stačí Enter (CR) — parser bere obojí.

| Pošli | Očekávaná odpověď | Efekt |
|---|---|---|
| `ping` | `pong` | sanity check |
| `screen main` | `OK` | vykreslí GPSDO obrazovku |
| `clear` | `OK` | smaže na THEME_BG_0 |
| `version` | `gpsdo-ui v0.1` | — |
| `help` | seznam příkazů | — |
| `ui` | `UI ON …` | zpět na původní dotykové gfx UI |
| cokoli jiného | `ERR unknown command` | — |

> `screen main` přepne do LVGL screen-mode (vypne původní gfx UI). `ui` se vrátí zpět.
> Kreslení LVGL probíhá v **UiTask** (UART task jen nastaví příznak — LVGL není thread-safe).

---

## 8. Akceptační kontrola (brief sekce 6)

- `ping`→`pong` < 50 ms; `screen main` vykreslí < 200 ms a vrátí `OK`.
- Opakovaný `screen main` je vizuálně identický (idempotentní; recyklace aktivního screenu
  `lv_obj_clean` → bez leaku, splňuje „×1000 < 1 KB").
- Vizuální shoda po zapnutí reálných fontů: 14 číslic (12 jistých světlých, 2 ztmavené),
  desetinná čárka modrá a větší než tečky, anténa „mísou" vzhůru, 7 tlačítek dole
  (FREQ aktivní, RUN zelený), horní lišta v pořadí dle 3.4.2, lokátor `JN89NS`, `HOLD 2h 14m`.

---

## 9. Troubleshooting

| Symptom | Příčina / řešení |
|---|---|
| `fatal error: lvgl.h: No such file` | Include path nemíří na kořen `lvgl/` (sekce 3a). |
| `lv_conf.h not found` / samé defaulty | Chybí symbol `LV_CONF_INCLUDE_SIMPLE` nebo `lv_conf.h` není na include path (3b). |
| Linker `undefined reference to lv_*` | `lvgl/src` není v Source Location (3c). |
| Linker `undefined reference to jbmono_*`/`inter_*` | Fonty nevygenerovány nebo `ui/fonts` není v buildu; nebo `FONTS_READY 1` bez fontů. |
| Build přeteče RAM | Sniž `LV_MEM_SIZE` (lv_conf.h) nebo přesuň `.bss` do AXI SRAM. |
| Displej zůstane na gfx UI | `screen main` nepřišel jako přesný řetězec (pozor na mezeru) / mode nepřepnut — zkontroluj `g_screen_req`. |
| Prázdný/černý displej po `screen main` | LTDC neukazuje na `0xC0000000` (po bench/bounce) — `lv_port_disp_init` to nastavuje, ověř že proběhl; zkontroluj že LTDC/DSI vůbec běží (původní UI se zobrazí?). |
| Špatné barvy (R/B prohozené) | RGB565 byte order — v `lv_conf.h` zkus `LV_COLOR_16_SWAP` (pokud to tvá verze v9 má) nebo ověř LTDC pixel format. |
| Chybí glyphy (super­skripty, σ, ▶…) | `FONTS_READY 0` (placeholder montserrat_14) nebo font vygenerován bez daného rozsahu — viz tabulka v `gen_fonts.ps1`. |
| Drobné probliknutí při překreslení | DIRECT render do živého FB — pro statickou obrazovku očekávané; řešitelné double-bufferem + DMA2D kopií (další iterace). |
| Padá `lv_init`/hardfault | LVGL volána z víc tasků — musí jen z UiTask; UART task smí jen `printf` + nastavit `g_screen_req`. |

---

## 10. Co je v této iteraci placeholder vs. finální

- **Finální:** UART příkazy, screen-mode, display port, layout (pozice/barvy/gradienty/
  zaoblení/stíny), idempotence, leak-free.
- **Placeholder / další iterace:** reálné fonty (krok 4), radiální gradient pozadí (LVGL
  neumí → plná výplň BG_0 dle 3.3), SVG anténa (zjednodušená na úsečky), grafy jako
  polylinie, double-buffer proti tearingu, **živá data + dotyk + stavový stroj** (mimo
  rozsah iterace 1).
