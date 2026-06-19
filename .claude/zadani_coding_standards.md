# Coding Standards (libprim, libui, app)

Společná pravidla psaného kódu pro celý repo. Cílem je **knihovní kvalita** —
agent může psát kód, který někdo jiný (nebo on sám za 6 měsíců) pochopí, rozšíří
a otestuje.

Tento dokument je závazný. Lint checks v CI **vynucují** ta pravidla, která lze
strojově ověřit. Ostatní jsou předmět code review.

---

## 1. Jazyk a standard

- **C11** (`-std=c11`), žádné GNU rozšíření v public hlavičkách (v `src/` jsou OK).
- **Žádné C++** v `libprim` ani `libui`. App smí (např. pro vendor HAL), ale jen
  se zdůvodněním v ADR.
- **Žádné nestandardní typy:** `int`, `long`, `unsigned` nahraď za `int32_t`,
  `int64_t`, `uint32_t` z `<stdint.h>`.
- **Bool:** `bool` z `<stdbool.h>`, ne `BOOL`, `int`, ani `_Bool` přímo.
- **Velikost:** `size_t` z `<stddef.h>` pro paměťové velikosti, `ptrdiff_t` pro
  rozdíly pointerů. Souřadnice obrazovky `int16_t` (rozsah −32k až +32k stačí).

---

## 2. Pojmenování

### 2.1 Symboly

| Druh | Konvence | Příklad |
|---|---|---|
| Veřejné funkce | `lib_module_action` | `prim_fill_rect`, `ui_pill_render` |
| Privátní funkce (file-scope) | `static` + libovolně | `static void rasterize_quad(...)` |
| Privátní funkce (cross-file v knihovně) | `lib_internal_action` | `prim_internal_clip_intersect` |
| Typy | `lib_*_t` | `prim_rect_t`, `ui_pill_t` |
| Enums (typ) | `lib_*_t` | `prim_blend_t` |
| Enum hodnoty | `LIB_*` UPPERCASE | `PRIM_BLEND_OVER` |
| Konstanty / makra | `LIB_*` UPPERCASE | `UI_DIM_HEADER_H`, `UI_COLOR_ACC` |
| Lokální proměnné | `snake_case` | `int16_t inner_width;` |
| Členové struktur | `snake_case` | `rect.x`, `pill.label` |
| Globální (nutné) | `g_lib_name` | `g_prim_active_fb` (jen v `src/`, opaque) |

**Žádné Hungarian notation** (`int iCount`, `char *pszName`).

### 2.2 Soubory

- Veřejné hlavičky: `include/<lib>/<module>.h`
- Privátní hlavičky: `src/internal/<module>.h`
- Implementace: `src/<module>.c`
- Testy: `tests/test_<module>.c`

Hlavičky **vždy** s include guard přes `#pragma once`. Žádný `#ifndef`/`#define`/
`#endif`.

### 2.3 Identifikátory v komentářích a zprávách

- Anglicky pro identifikátory, klíčové termíny.
- Česky pro vysvětlení principů, business logiky, UI textů.
- Commit messages: **anglicky** (`feat`, `fix`, `refactor`, `docs`, `test`,
  `chore` prefixy podle Conventional Commits).

---

## 3. Layout a formátování

### 3.1 Odsazení a délka řádku

- **4 mezery** pro indentaci, žádné taby.
- **80 znaků** soft limit, 100 hard limit. Delší řádky zalom logicky.
- Konec souboru: 1 newline, ne víc.
- Konec řádku: jen `\n` (Unix), žádné `\r\n`.

### 3.2 Závorky a mezery

- Otevírací `{` na konci řádku pro funkce, if/for/while:

  ```c
  if (x > 0) {
      do_something();
  }
  ```

- Mezera mezi klíčovým slovem a `(`: `if (x)`, `for (int i = 0; ...)`.
- **Žádná mezera** mezi názvem funkce a `(`: `foo(x)`, ne `foo (x)`.
- Operátory: mezery okolo binárních (`a + b`), žádné u unárních (`!ok`, `-x`).
- Pointer star u typu: `int *p` (ne `int* p`).
- Trailing comma v inicializátorech multi-line struktur (lepší git diffy).

### 3.3 Funkce

```c
// Public hlavička: krátký Doxygen komentář
/**
 * @brief Fill rectangle with given color.
 * @param rect    Target rectangle in framebuffer coordinates.
 * @param color   ARGB8888 color.
 * @param blend   Blending mode.
 */
PRIM_API void prim_fill_rect(prim_rect_t rect, prim_color_t color,
                              prim_blend_t blend);
```

```c
// Implementace
void prim_fill_rect(prim_rect_t rect, prim_color_t color, prim_blend_t blend)
{
    // ...
}
```

Funkce s ≤ 3 parametry na jednom řádku. Víc parametrů → jeden na řádek,
zarovnaný pod prvním:

```c
void prim_fill_rect_rounded(prim_rect_t rect,
                             int16_t radius,
                             prim_color_t color,
                             prim_blend_t blend)
{
    // ...
}
```

### 3.4 Struktury

```c
typedef struct {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
} prim_rect_t;
```

Pro krátké struktury OK jeden řádek členů:

```c
typedef struct { int16_t x, y;       } prim_point_t;
typedef struct { int16_t x, y, w, h; } prim_rect_t;
```

---

## 4. Const-correctness

**Striktně používej `const`:**

- **Vstupní pointery, které funkce nemění:**

  ```c
  void ui_pill_render(const ui_pill_t *pill);   // ✓ jen čte
  void ui_pill_compute(ui_pill_t *pill);        // ✓ zapisuje (computed_width)
  ```

- **Const data:**

  ```c
  static const ui_digit_segment_t SCR_MAIN_DIGITS[] = { ... };
  ```

- **Konstantní lokální:**

  ```c
  const int16_t padding = 22;
  ```

- **Strings literály:** `const char *` ne `char *`. Modifikace string literálu
  je UB.

Lint: warn na `char *` ukazujícího na literál (`-Wwrite-strings`).

---

## 5. Error handling

### 5.1 Návratové hodnoty

Funkce, které mohou selhat, vrací `prim_status_t` / `ui_status_t`:

```c
typedef enum {
    PRIM_OK = 0,
    PRIM_ERR_NULL          = -1,
    PRIM_ERR_OOM           = -2,
    PRIM_ERR_INVALID_ARG   = -3,
    PRIM_ERR_NOT_INITIALIZED = -4,
} prim_status_t;
```

**Konvence:**

- `PRIM_OK == 0`, chyby záporné.
- Žádné `int 1 = úspěch, 0 = chyba` (zaměnitelné).
- Funkce vracející hodnotu (například handle) vrací `NULL` při chybě, plus
  volitelně `errno`-like global getter.

### 5.2 Assertions vs. validace

| Druh chyby | Mechanismus |
|---|---|
| Programátorská chyba (precondition violation) | `assert()` v debug, UB v release |
| Runtime situace (NULL input, OOM) | Návratový kód, ne crash |
| Hardware (DMA2D timeout) | Návratový kód + logování |
| Fatal (nelze pokračovat) | `prim_fatal()` → reset MCU nebo abort host |

```c
void prim_fill_rect(prim_rect_t rect, prim_color_t color, prim_blend_t blend)
{
    assert(prim_get_target() != NULL);    // programátorská chyba

    if (rect.w <= 0 || rect.h <= 0) {     // benigní, jen skip
        return;
    }

    // ...
}
```

### 5.3 Žádný `errno`

`errno` je thread-locální a v embedded nepotřebné. Pokud chceš detail chyby,
poslední chyba v knihovně přes:

```c
PRIM_API const char *prim_last_error(void);
```

### 5.4 Žádné výjimky, longjmp

C nemá výjimky a `setjmp`/`longjmp` v embedded skoro vždy špatné. Návratové
kódy jsou jediný legitimní mechanismus.

---

## 6. Paměť

### 6.1 Hot path

**Žádný `malloc`, `free`, `realloc` v render hot path.** Definice hot path:

- Vše volané z `screen_*_render`.
- Vše volané každý frame.
- Veřejné `ui_*_render` funkce.

Linter pravidlo: grep `malloc\|calloc\|realloc\|free` v `*_render` funkcích
nesmí najít hit.

Povolené alokace:
- `prim_path_create` (alloc) / `prim_path_destroy` (free) — explicit lifecycle.
- `screen_main_init` (alloc cache bufferů) — jednou při bootu.

### 6.2 Stack vs. heap

- Krátké struktury (`prim_rect_t`, `prim_point_t`) na stacku.
- Velké buffery (framebuffer, cache) ve statické SDRAM section
  (`__attribute__((section(".sdram")))`) nebo jako globální.
- **Žádné VLA** (variable-length arrays). Vždy fixní velikost.

### 6.3 Alignment

- DMA2D vyžaduje 32-byte aligned buffery. Pre-alokované cache buffery musí být
  zarovnané:

  ```c
  static prim_color_t bg_cache[W * H]
      __attribute__((section(".sdram"), aligned(32)));
  ```

### 6.4 Cache (Cortex-M7)

- Framebuffer v SDRAM: write-back, write-allocate, cacheable pro CPU.
- DMA2D pracuje přímo s SDRAM — **po DMA2D operaci invalidate cache**, před
  další CPU read.
- Helper: `prim_internal_cache_sync_after_dma()` (private, ne API).

---

## 7. Thread safety

**libprim a libui jsou single-threaded.** Veškerý render předpokládá běh
v jednom kontextu (typicky M7 main loop).

V hlavičkách dokumentováno:

```c
/**
 * @brief ...
 * @note Not thread-safe. Caller must serialize access.
 */
```

Pokud někdy přibude FreeRTOS s více render tasky, **jeden render task** vlastní
přístup k libprim API. Komunikace přes queue.

---

## 8. Includes

### 8.1 Pořadí

V `.c` souborech:

```c
// 1. Vlastní hlavička modulu (selfsufficiency check)
#include "module.h"

// 2. Knihovní hlavičky (umbrella)
#include <prim/prim.h>
#include <ui/ui.h>

// 3. System hlavičky
#include <stdint.h>
#include <string.h>

// 4. Privátní hlavičky knihovny
#include "internal/bezier.h"
```

V hlavičkách:

```c
#pragma once

// Vždy zahrň, co potřebuješ pro deklarace v této hlavičce
#include <prim/types.h>      // pro prim_rect_t
#include <prim/api.h>        // pro PRIM_API
```

### 8.2 Pravidla

- **Hlavičky musí být self-sufficient** — kompilují se samostatně.
- **Žádné transitive include relying** — nezahrnuj nic přes jinou hlavičku.
- **Forward declarations** kde je možné (pointery na struktury).
- Žádný `#include "../other_module.h"` — relativní include přes ../ je červený
  vlajka.

---

## 9. Static a inline

### 9.1 `static`

- File-scope helpers: `static`.
- Const data v `.c`: `static const`.
- Default scope = nic = global. Vždy raději `static` pokud není API.

### 9.2 `inline`

- Krátké, časté funkce (gettery, převody): `static inline` v hlavičce.
- Hot path helpery: `static inline` v internal hlavičce.
- **Nezneužívat** — moderní kompiler inlinuje sám lépe. `inline` použij jen
  když měření ukáže přínos.

---

## 10. Optimalizace

### 10.1 Co dělat

- **Měřit, ne hádat.** DWT cycle counter na MCU, `clock_gettime` na hostu.
- **Pre-computation** statických hodnot (cache).
- **DMA2D** pro hromadné operace.
- **Bit manipulace** pro packed formats (ARGB8888 → RGB565).
- **Look-up tables** pro nelineární výpočty (sinus, log, gamma).

### 10.2 Co nedělat

- **Předčasně optimalizovat.** První napsání = čitelné, druhé = rychlé.
- **Manuálně inlinovat** kompilátorovo job.
- **Microoptimize jednotlivé instrukce** — DMA2D a algorithmic optimization
  dají 10–100× zrychlení, micro-tweaks 1.05×.
- **Použít `__builtin_*`** v public API. Jen v `src/internal/`.

### 10.3 Compiler flags

Release build:

```cmake
target_compile_options(prim PRIVATE
    -O2                          # rozumné, ne -O3 (větší binár, jen málo rychlejší)
    -ffunction-sections          # každá funkce v sekci → linker eliminuje dead code
    -fdata-sections
    -fno-common
)
target_link_options(prim PRIVATE
    -Wl,--gc-sections            # garbage collect unused sections
)
```

Debug build:

```cmake
target_compile_options(prim PRIVATE
    -O0 -g3 -DDEBUG
    -fsanitize=address,undefined    # jen host build
)
```

---

## 11. Logování

`libprim` a `libui` **nelogují**. Tisk z knihovny je špatný design (nemůžeš
ho ovládat z aplikace, závisí na `printf` runtime).

**App smí** logovat přes vlastní wrapper:

```c
// app/src/log.h
void app_log(const char *fmt, ...);    // implementace přes UART
```

Pro debug výstup uvnitř libprim (vývoj) povolen `#ifdef DEBUG` blok s konkrétně
voláním `app_log` přes weak symbol nebo callback — ne přes `printf`.

---

## 12. Doxygen

Veřejné API (`PRIM_API`, `UI_API`) musí mít Doxygen komentář:

```c
/**
 * @brief Render filled rectangle into active framebuffer.
 *
 * @param rect   Target rectangle. Clipped to current clip region.
 * @param color  ARGB8888 color value.
 * @param blend  Blending mode (REPLACE writes directly, OVER alpha-blends).
 *
 * @note Uses DMA2D when available; software fallback otherwise.
 * @warning Caller must call prim_set_target() before invoking render functions.
 *
 * @see prim_set_target
 * @see prim_set_clip
 */
PRIM_API void prim_fill_rect(prim_rect_t rect, prim_color_t color,
                              prim_blend_t blend);
```

Generování:

```bash
cd libprim && doxygen Doxyfile        # → libprim/docs/api/html/
```

Privátní funkce (`static`) komentář nemají povinný, ale doporučený pro
netriviální logiku.

---

## 13. Commits a větve

### 13.1 Commit messages

**Conventional Commits** formát:

```
<type>(<scope>): <subject>

<body>

<footer>
```

- `type`: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`, `style`.
- `scope`: `prim`, `ui`, `app`, `build`, `ci`.
- `subject`: imperativ, anglicky, ≤ 50 znaků.

Příklady:

```
feat(prim): add fill_rect_rounded with DMA2D acceleration

Implements rounded corners via 5-region split: central rect via DMA2D
fill, 4 corners via software AA rasterization.

Closes #12
```

```
fix(ui): correct pill width calculation for icon-only variants
```

### 13.2 Větve

- `main` — vždy buildable, taggované verze.
- `feature/<name>` — vývoj feature.
- `fix/<name>` — bugfixy.
- Merge přes squash + rebase do main.

---

## 14. Lint check pravidla (CI vynucuje)

Tato pravidla agent musí dodržet — CI je strojově ověří:

1. **Žádné magic numbers** v `app/src/screens/*.c`:
   ```bash
   grep -E '\b[0-9]{2,}\b' app/src/screens/*.c | grep -v '#define'
   ```
   musí vrátit jen řádky s validním důvodem (např. komentář).

2. **Žádné literální barvy** mimo `theme.h`:
   ```bash
   grep -E '0x[0-9A-Fa-f]{6}' libui/src/*.c libui/include/**/*.h | grep -v fonts/
   ```
   musí vrátit jen řádky z `theme.h`.

3. **Žádné PNG/JPG/BMP** v `libprim/`, `libui/`, `app/src/`:
   ```bash
   find libprim libui app/src -name "*.png" -o -name "*.jpg" -o -name "*.bmp"
   ```
   musí vrátit prázdno (kromě `docs/`).

4. **Žádné `malloc` v render hot path**:
   ```bash
   grep -E 'malloc|calloc|realloc|free' libprim/src/**/*_render*.c
   ```
   prázdno.

5. **Žádné křížové include**:
   ```bash
   grep -E '#include.*<ui/' libprim/src libprim/include
   ```
   prázdno (libprim nesmí znát libui).

6. **`-Wall -Wextra -Wpedantic` bez warnings.**

7. **`clang-format` check** podle `.clang-format` v repo root.

---

## 15. Specifické pro embedded

### 15.1 ISR (interrupt handlers)

V iteraci 1 žádné kromě DMA2D TC interrupt (volitelné). Pravidla:

- Krátké, žádné `printf`, žádné lockyna spinning.
- Sdílené proměnné s main loop přes `volatile` + atomic operations.
- `__DSB()` před návratem z ISR pro paměťovou synchronizaci.

### 15.2 RTOS

V iteraci 1 bez RTOS — single main loop. Pokud později FreeRTOS:

- libprim/libui zůstanou single-threaded — používá je jen jeden task.
- HAL operace mohou yieldit přes task notification.

### 15.3 Power

V iteraci 1 nepotřeba. Předpokladem do budoucna:

- `screen_main_render` vyvolá `__WFI()` po dokončení (čeká na další UART příkaz).
- DMA2D umí běžet s CPU sleeping (`SLEEPONEXIT`).
