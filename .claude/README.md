# Zadání: UI firmware GPSDO čítače (STM32H757 + Waveshare 4,3″ 800×480)

Brief pro implementaci CLI agentem. Cílem je **firmware uživatelského rozhraní**
pro precizní reciproční frekvenční čítač disciplinovaný GNSS, navržený tak, aby
**dvě nejnižší vrstvy byly samostatné reusable knihovny** použitelné v jiných
embedded projektech (jiný měřič, jiný přístroj se stejnou estetikou UI).

V této první iteraci se vykreslí **statická hlavní obrazovka** po UART příkazu
— bez živých dat, bez dotykových vstupů, bez komunikace s FPGA. Architektura
ale musí už teď stát jako pro 8 obrazovek a vícero projektů.

---

## 1. Architektura — tři vrstvy, dvě knihovny, jedna aplikace

```
┌────────────────────────────────────────────────────────────────┐
│  APP                       gpsdo_counter (executable)           │
│  screens/, cli/, main.c                                         │
│  Specifické pro tento přístroj. Nepřenosné.                     │
├────────────────────────────────────────────────────────────────┤
│  L2  ═══════════════════   libui  (static library)              │
│  Vizuální slovník: paleta, dimenze, komponenty, ikony, fonty.   │
│  Reusable v projektech se stejnou estetikou UI.                 │
│  Závisí jen na libprim a HAL abstrakci.                         │
├────────────────────────────────────────────────────────────────┤
│  L1  ═══════════════════   libprim  (static library)            │
│  Render core: fill, line, arc, path, gradient, glow, text.      │
│  Generic 2D grafický stack. Reusable v jakémkoli embedded       │
│  projektu s framebufferem (i mimo tenhle čítač).                │
│  Závisí jen na HAL abstrakci (display, DMA2D).                  │
├────────────────────────────────────────────────────────────────┤
│  L0                        HAL (per-platform)                   │
│  display.c (LTDC/SDRAM), dma2d.c, uart.c, mpu.c                 │
│  STM32-specific. Host varianta píše do RAM bufferu.              │
└────────────────────────────────────────────────────────────────┘
```

**Klíčové vlastnosti rozdělení:**

- `libprim` neví o paletě, dimenzích, ani o tom, že existuje frekvenční čítač.
  Je to čistě geometrický 2D renderer. **Použitelná v jakémkoli jiném projektu.**
- `libui` definuje vizuální styl (paletu, dimenze, komponenty pro UI). **Použitelná
  v jiných projektech, kde chceš stejný „look & feel".**
- Aplikace `gpsdo_counter` skládá obrazovky z `libui` komponent a vlastních dat.
  Specifická pro tento přístroj.
- Vrstvy se striktně dodržují přes **CMake `target_include_directories` PRIVATE
  vs. PUBLIC** — agent nemůže fyzicky napsat křížový `#include`.

---

## 2. Repository layout

```
gpsdo-ui/
├── CMakeLists.txt                  # top-level orchestrátor
├── README.md                       # tento dokument
├── LICENSE                         # MIT pro libprim a libui, proprietary pro app
│
├── libprim/                        # ═══ KNIHOVNA L1 ═══
│   ├── CMakeLists.txt
│   ├── VERSION                     # semver, např. 0.1.0
│   ├── README.md                   # specifické pro libprim
│   ├── CHANGELOG.md
│   ├── include/prim/               # ⇽ PUBLIC API (jen tohle vidí klienti)
│   │   ├── prim.h                  # umbrella header
│   │   ├── types.h
│   │   ├── fb.h
│   │   ├── fill.h
│   │   ├── shapes.h
│   │   ├── path.h
│   │   ├── gradient.h
│   │   ├── glow.h
│   │   └── text.h
│   ├── src/                        # ⇽ PRIVATE implementace
│   │   ├── fill.c
│   │   ├── shapes.c
│   │   ├── path.c
│   │   ├── gradient.c
│   │   ├── glow.c
│   │   ├── text.c
│   │   └── internal/
│   │       ├── bezier.h            # interní helpery, klient je nevidí
│   │       ├── rasterizer.h
│   │       └── alpha_blend.h
│   ├── tests/
│   │   ├── CMakeLists.txt
│   │   ├── test_fill.c
│   │   ├── test_shapes.c
│   │   └── ...
│   ├── examples/                   # 3-5 minimálních příkladů
│   │   ├── hello_rect.c
│   │   └── ...
│   └── docs/
│       └── api.md
│
├── libui/                          # ═══ KNIHOVNA L2 ═══
│   ├── CMakeLists.txt
│   ├── VERSION
│   ├── README.md
│   ├── CHANGELOG.md
│   ├── include/ui/
│   │   ├── ui.h                    # umbrella
│   │   ├── theme.h                 # paleta — PUBLIC API
│   │   ├── dimensions.h            # rozměry — PUBLIC API
│   │   ├── pill.h
│   │   ├── card.h
│   │   ├── button.h
│   │   ├── chart.h
│   │   ├── sparkline.h
│   │   ├── digit_group.h
│   │   ├── big_number.h
│   │   └── icons.h
│   ├── src/
│   │   ├── pill.c
│   │   ├── ...
│   │   ├── icons.c
│   │   └── fonts/
│   │       ├── font_mono_75.c      # generované přes lv_font_conv
│   │       └── ...
│   ├── tests/
│   ├── examples/
│   └── docs/
│
├── app/                            # ═══ APLIKACE ═══
│   ├── CMakeLists.txt
│   ├── README.md
│   ├── src/
│   │   ├── main.c
│   │   ├── screens/
│   │   │   ├── screen_main.c/.h
│   │   │   └── screen_main_data.c
│   │   ├── cli/
│   │   │   └── cli.c/.h
│   │   └── hal/                    # platform-specific
│   │       ├── stm32/
│   │       │   ├── display.c
│   │       │   ├── dma2d.c
│   │       │   ├── uart.c
│   │       │   └── mpu.c
│   │       └── host/
│   │           ├── display.c       # render do RAM bufferu → PNG
│   │           ├── dma2d.c         # SW emulace API
│   │           └── uart.c          # stdin
│   ├── linker/
│   │   └── stm32h757.ld
│   └── tests/
│
├── docs/                           # ═══ PROJEKT-LEVEL DOKUMENTACE ═══
│   ├── architecture.md
│   ├── coding_standards.md         # platí pro libprim, libui, app
│   ├── build_system.md
│   ├── decisions/                  # ADR (Architecture Decision Records)
│   │   ├── 0001-three-layer-libraries.md
│   │   └── 0002-lvgl-vs-bare-renderer.md
│   └── reference_v9.png            # vizuální cíl pro hlavní obrazovku
│
└── cmake/                          # sdílené CMake helpery
    ├── compiler_flags.cmake
    ├── stm32_toolchain.cmake
    └── host_toolchain.cmake
```

---

## 3. Dokumenty zadání

| Dokument | Obsah | Pro koho |
|---|---|---|
| `README.md` (tento) | Architektura, repo layout, build pořadí, AKR | Vstupní bod |
| `zadani_ui_primitives.md` | L1 / `libprim` — API a implementace primitiv | Agent |
| `zadani_ui_components.md` | L2 / `libui` — komponenty, paleta, dimenze, ikony, fonty | Agent |
| `zadani_ui_main_screen.md` | APP — hlavní obrazovka jako skladba | Agent |
| `zadani_coding_standards.md` | Coding pravidla platná pro celý repo | Agent |
| `zadani_build_system.md` | CMake struktura, targety, cross-compile | Agent |

**Implementuj v tomto pořadí:** build_system → coding_standards → libprim →
libui → app. Knihovny musí být stable před aplikací.

---

## 4. Cílová platforma a deps

- **MCU:** STM32H757BIT6, Cortex-M7 @ 480 MHz + M4 @ 240 MHz, FPU double, MPU.
  M7 dělá UI, M4 v této iteraci v idle (rezerva pro budoucí FPGA SPI a ADEV).
- **HW akcelerátor:** DMA2D / Chrom-ART pro fill, blit, alpha blending. Veškerý
  render musí preferovat DMA2D před softwarem; software jen fallback (path
  rasterizace, text glyph blit).
- **Displej:** Waveshare 4,3″ 800×480 IPS, varianta **RGB-paralelní (LTDC)**.
  Kapacitní dotyk v této iteraci nepoužit.
- **Framebuffer:** v externí SDRAM, ARGB8888, double buffer (3,1 MB SDRAM).
- **Build:** CMake ≥ 3.20, arm-none-eabi-gcc pro target, system gcc/clang pro host.
- **Žádné externí knihovny v `libprim` a `libui`** — čistý C11 + HAL. Tím se
  zajistí jejich přenositelnost. V aplikaci jsou závislosti OK (LVGL, FreeRTOS,
  CMSIS, STM32 HAL).

---

## 5. Cíl iterace 1

1. UART `screen main` vykreslí hlavní obrazovku do framebufferu, swap, na displeji
   se zobrazí **vizuálně 1:1 shodná** obrazovka s `docs/reference_v9.png`.
2. UART `ping` → `pong`.
3. UART `clear` → smaže displej do `UI_COLOR_BG_0`.
4. První render ≤ 100 ms, opakovaný ≤ 40 ms (DWT cycle counter).
5. CPU peak < 40 % na M7 během renderu (díky DMA2D), idle < 5 %.
6. Veškeré ikony **vektorové** z primitiv. **Žádné PNG/JPG/BMP v repo**.
7. Veškerá typografie z **vektorových fontů** generovaných přes `lv_font_conv`.

## Mimo rozsah iterace 1

- Živá data (hodnoty natvrdo dle `zadani_ui_main_screen.md`).
- Dotyk, animace, stavový stroj.
- FPGA SPI.
- Ostatní obrazovky (Allan, Statistiky, Kalibrace, …) — knihovny musí být
  připravené je unést bez refaktoringu.
- Lokalizace.

---

## 6. Pravidla knihovní organizace (povinná)

Tahle sekce je důvod, proč zadání není monolit. **Agent musí dodržet všechna**.

### 6.1 Závislostní graf

```
libprim          (žádné závislosti kromě HAL abstrakce)
  ↑
libui            (závisí jen na libprim)
  ↑
app              (závisí na libprim + libui + HAL implementaci)
```

- `libprim` **nikdy** nezahrnuje `ui/*.h` (ani transitive).
- `libui` **nikdy** nezahrnuje `app/*` nebo `screens/*`.
- App může zahrnout obojí.
- Cyklické závislosti = build error, ne warning.

### 6.2 Public vs. private hlavičky

- **Public hlavičky** v `<lib>/include/<lib>/*.h` — to je veřejné API.
  Klienti je vidí přes `target_include_directories(<lib> PUBLIC include/)`.
- **Private hlavičky** v `<lib>/src/internal/*.h` — jen pro implementaci.
  Klienti je nevidí přes `target_include_directories(<lib> PRIVATE src/)`.
- **Klient zahrnuje umbrella header**, ne jednotlivé hlavičky:

  ```c
  // ✓ správně
  #include <prim/prim.h>
  #include <ui/ui.h>

  // ✗ špatně (i když to teoreticky funguje)
  #include <prim/fill.h>
  ```

### 6.3 Pojmenování symbolů

| Knihovna | Prefix funkcí | Prefix typů | Prefix konstant |
|---|---|---|---|
| libprim | `prim_*` | `prim_*_t` | `PRIM_*` |
| libui | `ui_*` | `ui_*_t` | `UI_*` |
| app | `app_*` / per-modul | per-modul | per-modul |

**Žádný symbol bez prefixu**. Tím nelze způsobit kolize symbolů při linkování.

### 6.4 Visibility

V build flagách: `-fvisibility=hidden`. V hlavičkách:

```c
#if defined(_WIN32) || defined(__CYGWIN__)
  #define PRIM_API __declspec(dllexport)
#elif defined(__GNUC__)
  #define PRIM_API __attribute__((visibility("default")))
#else
  #define PRIM_API
#endif

PRIM_API void prim_fill_rect(prim_rect_t rect, prim_color_t color, prim_blend_t blend);
```

Funkce bez `PRIM_API` (resp. `UI_API`) jsou **interní** a nelze je linkovat
zvenčí. Klient dostane link error pokud zkusí použít nedeklarovanou funkci.

### 6.5 ABI stabilita

- **Veřejné struktury** v hlavičkách jsou explicitně označené jako stable. Změna
  layoutu = major version bump (semver).
- **Interní struktury** (jen v `src/internal/`) mohou měnit layout volně.
- **Opaque pointers** pro stavové objekty: `typedef struct prim_path prim_path_t;`
  v hlavičce, plná definice v `src/`. Klient pracuje jen s pointerem.
- **Žádné public globální proměnné**. Globální stav schovaný za gettery/settery.

### 6.6 Versioning (semver)

Každá knihovna má `VERSION` soubor (např. `0.1.0`) a `CHANGELOG.md`.

- **MAJOR** (1.0.0 → 2.0.0): rozbití API (smazaná funkce, změněný signature, layout struct).
- **MINOR** (0.1.0 → 0.2.0): přidaná funkce, zachovaná zpětná kompatibilita.
- **PATCH** (0.1.0 → 0.1.1): opravy bugů, výkonové optimalizace, dokumentace.

V iteraci 1 budou obě knihovny `0.1.0` (pre-1.0 = API ještě může lehce
fluktuovat, ale nedělá se to bezdůvodně).

### 6.7 Testovatelnost

Každá knihovna má **vlastní testovou suite** v `<lib>/tests/`, spustitelnou
samostatně:

```bash
cd libprim/build && ctest         # testy jen libprim
cd libui/build && ctest           # testy jen libui (linkuje libprim)
cd app/build && ctest             # integrační testy
```

`libprim/tests/` nesmí používat `libui` (testuje primitiva izolovaně). `libui/
tests/` smí používat `libprim` (testuje komponenty postavené nad primitiv).

### 6.8 Dokumentace per-library

Každá knihovna má `<lib>/README.md` minimálně s:

- Účel a scope (1–2 odstavce).
- Závislosti.
- Build instrukce (kopírovatelný snippet).
- Minimal working example (10–20 řádků kódu).
- Link na detailní API docs (Doxygen).

Bez tohohle je knihovna nepoužitelná pro nikoho jiného než původního autora —
ani pro tebe za 6 měsíců.

### 6.9 Licence

- `libprim` a `libui`: **MIT License** (chceš mít volné ruce reusovat).
- `app/`: dle volby autora (může být proprietary, GPL pro hobby projekt, …).

Soubor `LICENSE` v repo root pokrývá app. Každá knihovna má **vlastní `LICENSE`
soubor** v `<lib>/`.

---

## 7. Optimalizace CPU — povinné techniky

Cíl je < 40 % CPU peak a < 5 % idle. Nedosáhneš toho bez:

1. **DMA2D pro každý fill a blit.** Pozadí, gradient, karty, tlačítka. Software
   jen pro path rasterizaci a text glyph rendering.
2. **Partial redraw.** L3 si pamatuje dirty rectangles. V této iteraci to není
   plně využité (vše je statické), ale knihovní API to musí podporovat pro
   budoucí iterace.
3. **Pre-rendering statických prvků.** Mřížka Allan grafu, popisky os, ikony,
   pozadí — renderuj jednou, blit dále. Cache buffery v SDRAM.
4. **Cache fontových glyfů.** Hlavní číslo má 14 digitů × 75 px. Glyfy 0–9
   rasterizované jednou při bootu, dále DMA2D blit.
5. **Skip-on-equal.** Před zápisem porovnej obsah → přeskočit, pokud beze změny.
6. **Žádný floating-point v hot path.** Layout, souřadnice, alpha blending —
   vše integer. FP jen pro výpočet hodnot (v této iteraci nepoužito).
7. **MPU konfigurace SDRAM** jako cacheable write-back pro CPU, ne cacheable
   pro DMA2D — řeší přes MPU regiony.

Cílový rozpočet hlavní obrazovky:

| Fáze | První render | Opakovaný (z cache) |
|---|---|---|
| Pozadí (radial gradient) | 5 ms | 0,5 ms (blit) |
| Karty a pilulky | 8 ms | 8 ms |
| Allan graf | 15 ms | 3 ms (mřížka z cache) |
| Sparkline | 5 ms | 5 ms |
| Hlavní číslo (14×75 px) | 25 ms | 3 ms (blit z cache) |
| Zbytek textu | 20 ms | 12 ms |
| Tlačítka | 10 ms | 10 ms |
| Header/footer chrome | 5 ms | 0,5 ms (blit) |
| **Celkem** | **≤ 95 ms** | **≤ 42 ms** |

---

## 8. UART protokol

| Příkaz | Odpověď | Akce |
|---|---|---|
| `ping` | `pong` | sanity |
| `version` | `gpsdo-ui v0.1.0` | identifikace |
| `screen main` | `OK` | render hlavní obrazovky |
| `clear` | `OK` | mazání displeje |
| `help` / `?` | seznam příkazů | nápověda |
| ostatní | `ERR unknown` | error |

USART, 115 200 8N1, ASCII, CRLF nebo LF terminate, max 64 B na řádek.

---

## 9. Akceptační kritéria iterace 1

1. **`libprim` se sestaví samostatně** bez `libui` a `app`. Má vlastní `ctest` suite.
2. **`libui` se sestaví samostatně** bez `app`, jen s `libprim` jako dependency.
   Má vlastní `ctest` suite.
3. **`app` se sestaví** a flashne na STM32H757.
4. **Host build** existuje pro `libprim`, `libui` a část `app`, generuje PNG ze
   screen renderu.
5. **Pixel diff < 0,5 %** mezi vygenerovaným PNG a `docs/reference_v9.png`.
6. **První render ≤ 100 ms, opakovaný ≤ 40 ms** (DWT cycle counter).
7. **Veřejné API libprim a libui je dokumentované** Doxygen komentáři, generuje
   se HTML do `<lib>/docs/api/`.
8. **README v každé knihovně** s working example, který se sestaví a poběží.
9. **Žádné magic numbers v kódu** mimo `theme.h` a `dimensions.h` (lint check).
10. **Žádný PNG/JPG/BMP** v `libprim/`, `libui/`, `app/src/` (lint check).
11. **`-Wall -Wextra -Wpedantic` bez warnings** na všech buildech.
12. **Žádný cyklický `#include`** mezi vrstvami (CMake to fyzicky zakáže přes
    PRIVATE include dirs).
13. **Žádný `malloc` v render hot path** (grep audit `prim_render_*` a `ui_*_render`).

---

## 10. Deliverables

- Struktura repository dle sekce 2.
- Tři build targety: `libprim`, `libui`, `gpsdo_counter` (executable).
- Dva toolchainy: `host` (Linux/macOS native, pro testy) a `target`
  (arm-none-eabi-gcc pro STM32).
- Host PNG out testů jako CI artefakt.
- Doxygen-generovaná API docs pro libprim a libui.
- Foto z fyzického displeje s render výsledkem.
- Commits granulárně po modulech: `feat(prim): add fill_rect with DMA2D`,
  `feat(ui): pill component`, `feat(app): main screen layout`.
- Tag `v0.1.0` na všech `VERSION` souborech po dokončení iterace.

---

## 11. Pravidla pro rozhodování v případě nejednoznačnosti

Pokud agent narazí na nejasnou volbu:

1. **Volba renderovací knihovny** (LVGL vs. holý LTDC): zvol **holý LTDC + DMA2D
   přímo**, ne LVGL. Tady je důvod: LVGL je sama o sobě komplexní knihovna,
   která by zastiňovala custom `libprim` a `libui`. Pokud má smysl mít vlastní
   knihovny, mají taky smysl bez LVGL. (Toto je změna proti původnímu doporučení
   — knihovní organizace přebíjí pohodlí frameworku.)
2. **Fonty**: JetBrains Mono Medium pro mono, Inter Regular pro sans. OFL licence.
   Generuj přes `lv_font_conv` (samotný nástroj nevyžaduje LVGL runtime, jen
   produkuje C pole).
3. **Build systém**: CMake ≥ 3.20.
4. **Coding style**: viz `zadani_coding_standards.md`.
5. **Cokoliv ostatního**: zvol nejjednodušší rozumnou variantu a zaznamenej
   rozhodnutí jako ADR (`docs/decisions/NNNN-title.md`).

---

## 12. Co tahle architektura vyžaduje od agenta

Knihovní organizace má cenu v disciplíně. Konkrétně agent musí:

- **Před každým `#include`** zkontrolovat, jestli je to public hlavička klienta
  (přes umbrella `prim/prim.h` nebo `ui/ui.h`) nebo private z aktuální knihovny
  (přes `internal/*.h`).
- **Před přidáním symbolu** zvážit, jestli má být `PRIM_API`/`UI_API` (veřejné)
  nebo `static` (privátní v translation unit).
- **Před změnou veřejného API** rozhodnout: je to MAJOR (změna API) nebo MINOR
  (přidání)? Zaznamenat do CHANGELOG.
- **Před commitem** spustit lint check: žádné magic numbers, žádné křížové
  include, žádné PNG, žádné malloc v hot path.

Tohle agent dělat zvládne, jen mu to musí být explicitně řečeno — proto ten
podrobný `coding_standards.md`.
