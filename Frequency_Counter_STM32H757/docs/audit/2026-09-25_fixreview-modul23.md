# Přezkum vlastních oprav modulu 23 (F-0149…F-0153) — 2026-09-25

**Datum:** 2026-09-25
**Fáze:** F3 (přezkum kódu) — **bez editace kódu**
**Jádro:** CM7
**Branch:** `audit/2026-09-09-hodiny-pwr`

## Proč tenhle běh vznikl

`/audit-modul` byl spuštěn **bez jména modulu**. Podle procesu se cíl vybírá
z `AUDIT_STATUS.md` jako první modul se stavem `nezačato` — žádný takový
nezbyl (ověřeno grepem; jediná shoda je legenda stavového automatu).

Zvolen proto cíl s nejvyšší výtěžností a **vlastním precedensem v tomto
projektu**: přezkum oprav provedených dnes. Poslední dvě sezení dělala totéž
a našla reálnou regresi (**F-0148**) i lekci **L-0081** („oprava napsaná hned
vedle právě opraveného anti-vzoru ho dokázala zopakovat o pár řádků níž").
Dnes navíc platí dvě věci, které tenhle přezkum přímo vyžadují:

1. Měnil jsem kód v **6 souborech** během jednoho sezení.
2. **Jeden můj úsudek už měření vyvrátilo** (F-0152 — odstranění zdržení
   ve `fmc.c` zabilo USB CDC konzoli), takže spoléhat na vlastní posouzení
   zbylých čtyř oprav by bylo nemístné.

## Rozsah — co bylo čteno

Rozsah je dán diffem `d39c323~1..HEAD` nad `*.c`/`*.h`:

| soubor | změna | co obsahuje |
|---|---|---|
| `CM7/Core/Src/usart.c` | +26 | **F-0153** — `uart1_rearm_rx()`, `g_uart1_rearm_fail` |
| `CM7/Core/Src/fmc.c` | +128/−39 | **F-0149** zámek, **F-0151** odstranění assertu, **F-0152** (odstranění → vrácení), **F-0150** komentář |
| `CM7/Core/Inc/fmc.h` | +27 | **F-0151** — `REFRESH_COUNT_SPEC_MAX` a jeho vstupy |
| `CM7/Core/Src/alarm.c` | +8 | **F-0144** — jen komentář |
| `CM7/Core/Src/sd_export.c` | +7 | **F-0145** — jen komentář |
| `CM7/app/hal/stm32/prim_stm32_hal.c` | +11 | **F-0147** — jen komentář |

**Kontext dočtený kvůli rozhodnutí nálezů:** `CM7/Core/Src/flightrec.c`
(`errlog_put`, `errlog_fmt_detail`), `CM7/Core/Inc/errlog.h`,
`CM7/Core/Src/freertos.c` (`gpio_cfg_lock`), `CM4/Core/Src/main.c`
(rozsah HSEM 1), `CM7/Core/Src/main.c` (pořadí bootu).

**Umístění v `.map`:** změna nezavádí žádný buffer. Jediný nový globál
`g_uart1_rearm_fail` leží na `0x2401eea8`, tedy v AXI SRAM (RAM_D1), těsně
vedle sourozenců (`g_uart1_ore` `0x2401eebc`) — žádná DMA ani cache
souvislost. Ověřeno v `CM7/Release/H757_LED_CM7.map`, ne odhadem.

---

### F-0155 [S3] Selhání re-armu se v záznamníku vypíše jako FALEŠNÉ `ORE=<n>` — dekodér můj `sub = 0xFE` nezná

- **Místo:** `CM7/Core/Src/usart.c:204` (volání), dekodér
  `CM7/Core/Src/flightrec.c:912-919`.
- **Popis:** F-0153 loguje selhání re-armu jako
  `errlog_put(ERRLOG_K_UART, 0xFEu, g_uart1_rearm_fail, 0u, "REARM")`.
  Signatura je `errlog_put(kind, **sub**, a, b, tag)`, takže `0xFE` jde do
  `sub`. Dekodér pro `ERRLOG_K_UART` ale rozlišuje **jedinou** hodnotu:
  ```c
  case ERRLOG_K_UART:
      if (r->sub == 0xFFu) snprintf(..., "fronta GPS plna, zahozeno bajtu=%lu", r->a);
      else                 snprintf(..., "ORE=%lu FE=%lu NE=%lu PE=%lu", r->a, ...);
  ```
  `sub = 0xFE` tedy spadne do `else` a vytiskne
  **`ORE=<počet selhání re-armu> FE=0 NE=0 PE=0`**.
- **Důkaz:** `errlog.h:88` `bool errlog_put(uint8_t kind, uint8_t sub, uint32_t a,
  uint32_t b, const char *tag);` — druhý parametr je `sub`.
  `flightrec.c:913` testuje `r->sub == 0xFFu`; žádná větev pro `0xFE` neexistuje.
  `usart.c:204` předává `0xFEu` jako druhý argument.
- **Dopad:** Záznamník **tvrdí chybu, která se nestala** (přetečení přijímače),
  a **zamlčuje tu, která se stala** (trvalá smrt GPS příjmu). Je to přesně ta
  třída, kterou projekt pojmenoval v **L-0011** a **L-0049** — a je o to horší,
  že jediným účelem F-0153 bylo udělat tichou poruchu viditelnou.
  ⚠️ Hodnota `tag = "REARM"` to nezachrání: `else` větev `tag` netiskne.
  ⚠️ Volba sentinelu `0xFE` sama o sobě **je v pořádku** — `ec & 0xFF` z
  `HAL_UART_ERROR_*` dává nejvýš `0x3F` (PE 0x01 … RTO 0x20), takže se s ním
  srazit nemůže. Vada je výlučně v tom, že dekodér tu hodnotu nezná.
- **Reprodukce:** staticky doložitelné. Na HW by vyžadovalo selhání re-armu,
  které dosud nenastalo (`g_uart1_rearm_fail = 0`, změřeno na desce).
- **Návrh opravy:** Doplnit do `errlog_fmt_detail` větev
  `else if (r->sub == 0xFEu) snprintf(..., "re-arm prijmu SELHAL, GPS RX je mrtvy, pokusu=%lu", r->a);`.
  🔑 Formulace má říct **následek** („GPS RX je mrtvý"), ne jen detekci — to je
  **L-0056**.
  ⚠️ Kontrola L-0012: ostatní `sub` v projektu jsou pokryté. Prověřeno výčtem
  všech volání `errlog_put` — jen `0xFE` je dekodéru neznámé.
- **Riziko opravy:** nízké (jedna větev ve formátovači, žádná změna chování).
- **Vztah k lekcím:** **L-0049** (rozluštit `a`/`b` do věty JEDNOU, na zdroji),
  **L-0011** (diagnostika tvrdí, co neměřila), **L-0017** (tichý přeskok jen
  s počítadlem — tady je počítadlo, ale jeho popisek lže), **L-0056**.
- **Stav:** otevřeno.

---

### F-0156 [S3] Záznam o selhání re-armu je v hlavní cestě VŽDY potlačen — tentýž průchod ISR si minutový cooldown sám vyčerpal

- **Místo:** `CM7/Core/Src/usart.c:236` a `:245` (dvě volání `errlog_put`
  v jednom průchodu `HAL_UART_ErrorCallback`), mechanismus
  `CM7/Core/Src/flightrec.c:458` + `:591-593`.
- **Popis:** `errlog_put` má prodlevu **per DRUH** (`kind`), ne per `sub`:
  ```c
  #define ERRLOG_COOLDOWN_MS  60000u     /* max 1 zaznam na druh a minutu */
  if ((int32_t)(now - s_el_cool_next[kind]) >= 0) { s_el_cool_next[kind] = now + ERRLOG_COOLDOWN_MS; … }
  else if (s_el_pending[kind] < 0xFFFFu) { s_el_pending[kind]++; }
  ```
  `HAL_UART_ErrorCallback` ale loguje **dvakrát v témže průchodu**: nejdřív
  chybu (`ORE/FE/NE/PE`), a o pár instrukcí dál — po `AbortReceive` — volá
  `uart1_rearm_rx()`, které loguje selhání re-armu. **Druhé volání má stejný
  `kind`, takže narazí na prodlevu, kterou si první volání právě nastavilo.**
- **Důkaz:** pořadí v `usart.c` (`HAL_UART_ErrorCallback`):
  `errlog_put(ERRLOG_K_UART, ec & 0xFF, …)` → `__HAL_UART_CLEAR_*FLAG` →
  `HAL_UART_AbortReceive` → `uart1_rearm_rx()` → `errlog_put(ERRLOG_K_UART, 0xFE, …)`.
  Obě volání proběhnou v jednom přerušení, tedy ve stejném `HAL_GetTick()`.
  Podmínka `(int32_t)(now - s_el_cool_next[kind]) >= 0` je pro druhé volání
  nesplnitelná. Hodnota `60000` je doložená i **na desce**: záznamy
  `#3236 up=16149s FPGA x20` a `#3235 up=16089s FPGA x20` jsou přesně 60 s
  od sebe a nesou počet sloučených opakování.
- **Dopad:** **Není to vzácný souběh, ale jistota** — v cestě, kde re-arm
  reálně může selhat (tedy po chybě), se záznam neemituje **nikdy**. Zvedne se
  jen `s_el_pending[ERRLOG_K_UART]`, který by se vyvezl až s PŘÍŠTÍM záznamem
  téhož druhu. Jenže selhání re-armu je **terminální**: příjem je mrtvý, další
  `RxCplt` ani `ErrorCallback` nepřijde, takže žádný příští záznam **nebude**
  a počet opakování odejde s restartem.
  ⇒ **F-0153 v hlavní cestě nedoručuje to, co jeho vlastní nález slibuje**
  („převádí ji z tiché na viditelnou"). Zůstane jen `g_uart1_rearm_fail`,
  čitelné **pouze ladicí sondou** — a to je přesně to, co zpřísněná **L-0017**
  zakazuje jako dostatečné.
  ⚠️ V cestě `HAL_UART_RxCpltCallback` potlačení jisté není: tam se první
  `errlog_put` volá jen při plné frontě.
- **Reprodukce:** staticky doložitelné z pořadí volání. Na HW dosud nenastalo
  (`g_uart1_rearm_fail = 0`).
- **Návrh opravy:** Tři varianty, liší se cenou i dosahem:
  1. **Nejlevnější:** v `uart1_rearm_rx()` použít **jiný `kind`** (nový
     `ERRLOG_K_UARTFATAL`), takže má vlastní kbelík prodlevy. Cena: nová
     položka výčtu + větev ve formátovači + `flightrec.c:526` jméno druhu;
     řeší **zároveň F-0155**.
  2. Emitovat záznam **mimo cooldown** (parametr „tohle je terminální, nezdržuj").
     Zásah do sdíleného `errlog_put`, dotkne se všech volajících.
  3. Nechat jen počítadlo a **vystavit ho ve `status`** vedle `ORE/FE/NE/PE`.
     Nejmenší zásah do záznamníku, ale porušuje „jeden soubor, jeden vzor",
     kterým se F-0153 zdůvodňovalo.
  🔑 **Doporučeno (1)** — jediná, která řeší obě vady jedním zásahem a
  vyjadřuje, že trvalá smrt příjmu není totéž co přechodná chyba linky.
- **Riziko opravy:** nízké až střední. Nový `kind` rozšiřuje výčet sdílený
  s IPC/webem (`ERRLOG_KIND_MAX`) → ověřit, že se neposune význam uložených
  záznamů a že `errlog_fmt_detail` i jméno druhu znají novou hodnotu.
  ⚠️ **Nezvedat `IPC_VERSION` bezdůvodně** — text se posílá už hotový
  (`errlog_fmt_detail`), takže web novou hodnotu nemusí znát.
- **Vztah k lekcím:** **L-0017** (počítadlo musí být dosažitelné bez sondy),
  **L-0016** (porucha, kterou nelze odlišit od normálního provozu), **L-0059**
  (čítač, jehož nula nese diagnózu). Nová třída: *„událost a její vlastní
  potlačení vznikly v jednom průchodu"*.
- **Stav:** otevřeno.

---

### F-0157 [S4] Oprava F-0151 zavedla dvě nová nevynucená vyjádření téhož faktu (`SDRAM_ROWS`, `FMC_SDCLK_MHZ`)

- **Místo:** `CM7/Core/Inc/fmc.h` (nové `FMC_SDCLK_MHZ`, `SDRAM_ROWS`)
  vs. `CM7/Core/Src/fmc.c:207` (`RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13`)
  a `:212` (`SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2`).
- **Popis:** Nová mez `REFRESH_COUNT_SPEC_MAX` počítá z `SDRAM_ROWS 8192u`
  a `FMC_SDCLK_MHZ 50u`. Obě hodnoty jsou **druhým vyjádřením** toho, co už
  stojí v konfiguraci řadiče. Kdyby někdo změnil `RowBitsNumber` na 12
  (4096 řádků), assert by dál počítal s 8192, **prošel by** a tvářil se, že
  hodnotu hlídá — zatímco by hlídal špatnou veličinu.
- **Důkaz:** `grep` potvrdil, že nová makra nikde jinde nekolidují, ale také
  že `fmc.c:207`/`:212` jsou jediná místa, kde tytéž fakty žijí — a žádná
  vazba mezi nimi a `fmc.h` neexistuje.
- **Dopad:** Malý a ohraničený. Komentáře u obou maker svůj zdroj **jmenují**
  (`/* PLL2R 100 MHz / SDClockPeriod_2 */`, `/* RowBitsNumber = 13 -> 2^13 */`),
  takže pečlivý čtenář vazbu najde. Riziko je latentní a projeví se jen při
  změně geometrie SDRAM, což je vzácné.
  ⚠️ Poctivě: oprava F-0151 **čistou bilanci zlepšila** (historickou vadu 1835
  nově chytí překladač, což dřív nechytil nikdo), ale zároveň vyrobila dvě
  menší místa téže třídy, kterou odstraňovala — **L-0018**.
- **Reprodukce:** staticky.
- **Návrh opravy:** Vynutit to nejde: `RowBitsNumber` i `SDClockPeriod` jsou
  **runtime přiřazení v generovaném kódu mimo `USER CODE`**, takže na ně
  `_Static_assert` nedosáhne a regen by úpravu smazal. Zbývá levná varianta:
  u obou maker ve `fmc.h` doplnit **explicitní odkaz** `fmc.c:207` / `:212`
  s větou „při změně tam uprav i tady", aby byl vztah dohledatelný z obou
  stran. Vlastní hodnoty nechat.
- **Riziko opravy:** nulové (`docs:`).
- **Vztah k lekcím:** **L-0018** (dvě místa počítající touž veličinu),
  **L-0020** (duplicitu, kterou je dražší odstranit než snést, převeď na
  kontrolu rozdílu — tady kontrola nedosáhne, takže zbývá odkaz).
- **Stav:** otevřeno.

---

## Co bylo zkontrolováno a je v pořádku

**F-0153 (`usart.c`) — vše kromě cesty do záznamníku:**
- `uart1_rearm_rx()` je **jediný zdroj pravdy** pro obě obsluhy → dvě kopie
  se nemohou rozejít (**L-0012**/**L-0018** dodrženo). ✅
- **Tvrzení v mém komentáři ověřeno, ne předpokládáno** (L-0028): `errlog_put`
  je skutečně ISR-safe — krátká sekce `__disable_irq()` s komentářem
  „smi bezet i z ISR", zápis **jen do RAM ringu**, `seq`/`t_unix` se dopočítají
  až v `tick` („parsovani casu nepatri do ISR"). Žádný zápis do flash. ✅
- Obava z „bouře `errlog_put` v ISR" je **neopodstatněná** — prodleva 60 s
  per druh ji ohraničuje (ironicky je to zároveň příčina F-0156). ✅
- `g_uart1_rearm_fail` je `volatile uint32_t`, zapisuje výhradně ISR. ✅
- Volba sentinelu `0xFE` nekoliduje s `ec & 0xFF` (max `0x3F`). ✅
- Na HW: `g_uart1_rearm_fail = 0` a `g_gps_rx_drop = 0`, stabilní přes 6 s ⇒
  re-arm reálně neselhává a přidaný kód nic nespouští. ✅

**F-0149 (`fmc.c` zámek):**
- `#include "gpio_guard.h"` i volání leží uvnitř `USER CODE` → **regen-safe**. ✅
- HSEM hodiny jsou zapnuté **před** použitím: `__HAL_RCC_HSEM_CLK_ENABLE()`
  na `main.c:331`, `MX_FMC_Init()` až na `:382`. ✅
- `gpio_cfg_lock()` je best-effort s ohraničeným čekáním → **nemůže zavést
  deadlock při bootu**; ověřeno i empiricky (`g_cm4_absent = 0`, deska
  naběhla po dvou power-cyklech). ✅
- `gpio_cfg_unlock()` při nezískaném zámku je neškodné — HSEM release cizím
  jádrem HW ignoruje (týž předpoklad, na kterém stojí `CM4/main.c:193`). ✅
- ⚠️ **Pozorování, ne nález (pre-existující):** CM7 nemá obdobu příznaku
  `g_hsem_gpio_unlocked`, který si CM4 zavedlo v F-0135, takže se nepozná,
  jestli konkrétní průchod běžel bez zámku. Týká se **všech šesti** volajících
  na CM7, ne jen tohohle — proto to sem nepatří jako nález modulu 23.

**F-0151 (`fmc.h` mez):** aritmetika `64000u * 50u = 3 200 000` bezpečně v 32
bitech; `REFRESH_COUNT_SPEC_MAX` je celé v závorkách; `_Static_assert`
v hlavičce je legální (`-std=gnu11`); nová makra nikde nekolidují (ověřeno
grepem přes CM7 i CM4). Pozitivní kontrola proběhla na obou vadách. ✅

**F-0152 (vrácené zdržení):** `HAL_Delay` je po `HAL_Init()` (`main.c:316`),
tedy s běžícím timebase; umístění je shodné s původním kódem. ✅

**F-0144 / F-0145 / F-0147 / F-0150:** ověřeno diffem, že obsahují **výhradně
komentáře** — žádný nekomentářový řádek. Binárně doloženo tím, že `.text`
zůstal 615 096 B resp. 615 112 B beze změny. ✅

## Checklist — relevantní položky

| sekce | položka | verdikt |
|---|---|---|
| B | sdílená GPIO mezi jádry / HSEM | OK (F-0149 splnilo účel, pořadí hodin ověřeno) |
| C | umístění bufferů, DMA, cache | netýká se — změna nezavádí buffer; `g_uart1_rearm_fail` v RAM_D1 dle `.map` |
| D | `volatile` u proměnné sdílené s ISR | OK |
| D | žádné blokující volání / `malloc` / `printf` v ISR | OK (`errlog_put` ověřen jako RAM-only) |
| E | návratové hodnoty `HAL_*` | OK — právě to F-0153 opravilo |
| E | rozpoznání vypadlé periferie (UART ORE/FE/NE) | OK v detekci, **NÁLEZ v hlášení** → F-0155, F-0156 |
| G | FMC/SDRAM: refresh z reálné HCLK | OK, nově hlídáno překladačem; vazba na zdroj nevynucená → F-0157 |
| H | warningy nejsou vypnuté | OK — build 0 varování, `audit.py` 92/0/2 |

## Shrnutí

**Verdikt: podmíněně funkční.**

Tři nálezy (**2× S3, 1× S4**), **všechny ve vlastní práci z dnešního dne**.
Žádný z nich nemění chování přístroje za normálního provozu — jsou to vady
v **diagnostice**, tedy přesně v tom, co mělo být opravou vylepšeno.

🔑 **Obě S3 míří na tutéž opravu (F-0153) a obě znamenají, že nedoručuje, co
slibuje.** V hlavní cestě — po chybě linky — se záznam o selhání re-armu
**nikdy neemituje** (tentýž průchod ISR si minutový cooldown právě vyčerpal),
a i kdyby se emitoval, vypsal by se jako **falešné `ORE=<n>`**. Zůstalo by
jen počítadlo čitelné ladicí sondou — tedy stav, který zpřísněná L-0017
výslovně nepovažuje za dostatečný.

🔑 **Poučení, které tenhle běh potvrdil potřetí:** F-0153 prošlo buildem,
`audit.py`, kontrolou v `.elf` i HW testem — a přesto nefunguje tak, jak má.
Žádná z těch kontrol na tuhle třídu nedosáhne, protože všechny ověřují, že se
kód **přeložil a vykonal**, ne že jeho **výstup dává smysl**. Jediné, co to
odhalilo, bylo přečtení konzumenta (`errlog_fmt_detail`) a mechanismu
(`errlog_put`) — tedy kód, který jsem **neměnil**.
