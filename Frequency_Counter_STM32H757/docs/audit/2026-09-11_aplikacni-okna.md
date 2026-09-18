# Audit: aplikační okna (`app_gpsdo.c` — `render_*`)  (2026-09-11)

- **Commit:** `59a4a5b` (větev `audit/2026-09-09-hodiny-pwr`), pracovní strom bez změn v kódu
- **Jádro / doména:** CM7, UiTask (kreslí výhradně on)
- **Projité sekce checklistu:** D (souběh — cross-task zápisy do sdílené konfigurace),
  E (meze polí, ošetření chyb, formátování)
- **Neprojité (a proč):** A, B, C, F, G, H — okna nekonfigurují hodiny, nemluví s CM4 přímo,
  nemají DMA buffery ani volání `HAL_*` a nesahají na Flash jinak než přes hotové ovladače

## Rozsah a metoda (čti, než budeš výsledek brát jako úplný)

Modul 11 = ~45 funkcí `app_gpsdo_render_*` a jejich kreslicí helpery, **≈ 6 200 řádků**
zbývajících po vyčlenění strojovny (modul 10). **Nečetl jsem je řádek po řádku** — na jedno
sezení to není poctivě možné a předstírat to by bylo horší než to přiznat. Místo toho
proběhl **rizikově cílený průchod**: nejdřív grepy na třídy chyb, které tenhle projekt už
prokazatelně vyrobil (a má je zapsané v `CLAUDE.md`/`LESSONS.md`), pak měření tam, kde jde
měřit, a ruční čtení jen v místech, kam ty stopy vedly.

Co se dělalo, v tomhle pořadí:

| co | nástroj | výsledek |
|---|---|---|
| `fmt_fixed(…, ≥4)` — tiše zahodí desetiny | grep dle `CLAUDE.md` | 6 shod, **všechny komentáře** → **F-0054** |
| `%f`/`%e`/`%g` (nano.specs nevytiskne nic) | grep | 0 v kódu (3 shody = komentáře) ✅ |
| `sprintf`/`strcpy`/`strcat` | grep | **0** ✅ |
| meze indexů z UI do polí | ruční kontrola všech 7 | všechny ošetřené ✅ |
| velikost zásobníkových rámců | `objdump` `sub sp,#N` | max **380 B** ✅ |
| fill obcházející `mark_dirty` (F-0032) | grep + čtení | 1 místo, má `REPLACE` clear ✅ |
| `%s` nad řetězci cizích úloh | grep + analýza | není OOB ✅ (rozbor níže) |
| cross-task zápisy do sdílené konfigurace | grep + `objdump` | **F-0052** |

## Souhrn

Okna jsou **kreslicí kód a chovají se jako kreslicí kód** — přesně jak modul 10 předpovídal.
Známé pasti projektu (`%f`, zakázané string funkce, neošetřené indexy, zásobník, pravidlo
„partial redraw musí začít clear") jsou **dodržené**, a to doložitelně.

**Verdikt: podmíněně funkční.** Jediná vada s reálným dopadem neleží v kreslení, ale v tom,
co okno MATH dělá **mimo** kreslení: **`g_meas_cfg` se z dotykové cesty přepisuje bez
kritické sekce, zatímco `scpi.c` i `ipc.c` ji kolem téhož globálu mají** (**F-0052**).
Není to teoretické — všechna tři volání téže funkce `meas_math_capture_null()` jsou vedle
sebe a dvě z nich to dělají správně.

---

### F-0052 [S2] Okno MATH přepisuje `g_meas_cfg` bez kritické sekce, přestože SCPI i IPC ji kolem téhož globálu mají

- **Místo:** `CM7/app/app_gpsdo.c:2085-2089` (`math_recenter_limits()`),
  `:8625-8651` (dotyková obsluha okna MATH, `s_view=31`), `:8639`
  (`meas_math_capture_null(&g_meas_cfg, …)`)
- **Popis:** `g_meas_cfg` je sdílená mezi **třemi** úlohami. Dvě z nich k němu přistupují
  atomicky, dotyková cesta v UiTasku ne — zapisuje `double` pole přímo do globálu.
- **Důkaz:**
  - Struktura obsahuje **pět `double`** (`meas_math.h`): `m`, `b`, `null_ref`, `lo`, `hi`.
  - **Jak to dělá SCPI** (`scpi.c:953-955`) — kopie, úprava, atomický commit:
    ```c
    meas_cfg_t tmp; taskENTER_CRITICAL(); tmp = g_meas_cfg; taskEXIT_CRITICAL();
    /* … úprava nad `tmp` … */
    taskENTER_CRITICAL(); g_meas_cfg = tmp; taskEXIT_CRITICAL();
    ```
    a `scpi.c:1021` čte snímek pro `scpi_src_t` taktéž pod kritickou sekcí.
  - **Jak to dělá IPC** (`ipc.c:539`, `:543`) — totéž, a u toho komentář `ipc.c:526`, který
    **výslovně počítá se souběžnými zápisy z UI**: *„commit JEN pri realne zmene (jinak by
    100Hz commit klobrcoval soubezne zapisy UI/USB SCPI do g_meas_cfg)"*.
  - **Jak to dělá UI** (`app_gpsdo.c:2087-2088`) — bez jakékoli ochrany:
    ```c
    g_meas_cfg.lo = y - band;
    g_meas_cfg.hi = y + band;
    ```
    V obrazu jsou to **dvě samostatné instrukce** s bodem preempce mezi nimi
    (`objdump`, `math_recenter_limits`):
    ```
    vstr d6, [r3, #40]   @ .lo
    vstr d0, [r3, #48]   @ .hi
    ```
  - 🔴 **Rozhodující důkaz — tatáž funkce, tři volající, dvě správně:**
    | volající | jak |
    |---|---|
    | `ipc.c:482` | `meas_math_capture_null(c, …)` nad **lokální** kopií → atomický commit |
    | `scpi.c:302` | `meas_math_capture_null(c, …)` nad **lokální** kopií → atomický commit |
    | `app_gpsdo.c:8639` | `meas_math_capture_null(&g_meas_cfg, …)` — **přímo do globálu** |
    A `meas_math_capture_null()` sama píše **dvě** pole (`null_ref` = double, pak `null_en`).
  - **Kdo může preemptnout:** UiTask je **BelowNormal (16)**, defaultTask **Normal (24)**
    (`ARCHITECTURE.md` §7) a volá `ipc_service()` (`freertos.c:711`) i `syscfg_flash_tick()`
    v ~100Hz smyčce. `syscfg.c:159-160` navíc čte `.lo` a `.hi` **dvěma samostatnými
    příkazy**, tedy bez ochrany i na straně čtenáře.
- **Dopad:** Preempce mezi oběma `vstr` vydá **nekonzistentní dvojici** `(nové lo, staré hi)`.
  Ta pak může být (a) **uložena do flash** přes `syscfg_flash_tick` → přežije power-cycle,
  (b) publikována do CM4 → web ukáže jiné meze než displej, (c) použita v
  `meas_limit_eval()` → **falešný verdikt FAIL → 4 pípnutí** (`alarm.c`).
  ⚠️ **Okno je úzké — dvě instrukce — a je poctivé to říct.** Ale není zanedbatelné:
  nejmenší pásmo je **0,001 Hz** (`MATH_BAND_PRESETS`), takže k **inverzi** `lo > hi`
  stačí, aby se odečet mezi dvěma doteky pohnul o 0,002 Hz — což při dnešním SIM headline
  (kolísá ~±0,05 Hz) nastane běžně. A `math_recenter_limits()` se volá při každé změně
  pásma, tedy i opakovaně přes auto-repeat drženého prstu.
  🔑 **Hlavní důvod к opravě ale není pravděpodobnost, ale nesoulad:** tři místa sahají na
  týž globál a dvě z nich mají ochranu. To je přesně `L-0012`.
- **Reprodukce:** `HYPOTÉZA — ověřit:` okno MATH, zapnout limity, držet prst na tlačítku
  pásma (auto-repeat) a současně sledovat `scpi CALC:LIM:LOW?` / `:UPP?` přes web (CM4 čte
  přes IPC). Rozpor mezi displejem a webem = zachycená nekonzistentní dvojice.
  Statisticky se to chytá špatně; **ze zdrojáku je to ale doložitelné bez měření**.
- **Návrh opravy:** Převzít vzor, který už v projektu je **třikrát**: pracovat nad lokální
  kopií a commitnout atomicky.
  ```c
  meas_cfg_t c; taskENTER_CRITICAL(); c = g_meas_cfg; taskEXIT_CRITICAL();
  c.lo = y - band; c.hi = y + band;
  taskENTER_CRITICAL(); g_meas_cfg = c; taskEXIT_CRITICAL();
  ```
  ⚠️ **Nedělat to jen v `math_recenter_limits`** — stejně se musí ošetřit `:8625-8651`
  (včetně `meas_math_capture_null(&g_meas_cfg, …)`, které patří změnit na lokální kopii
  jako u obou ostatních volajících). Jinak vznikne čtvrtá varianta téhož a nález se vrátí.
  ⚠️ Kritická sekce musí obepnout **jen přiřazení**, ne výpočet `meas_math_apply()` /
  `screen_main_freq_hz()` — ty jsou drahé a IRQ se kvůli nim vypínat nemá.
- **Riziko opravy:** nízké — vzor je v projektu zavedený a otestovaný na třech místech.
  Kritická sekce kolem kopie 56B struktury je řádově desítky cyklů.
- **Vztah k lekcím:** **`L-0012`** (symetrické instance — tady dokonce tři volající téže
  funkce) a **`L-0018`** (jeden vzor přístupu ke sdílenému stavu, ne čtyři).
- **Stav:** **opraveno 2026-09-18** v `2253ac4` (společně s F-0096), ⬜ **neověřeno na HW**.
  Provedeno podle návrhu: obsluha okna MATH pracuje nad lokální kopií `c` a commituje
  `g_meas_cfg` atomicky až na konci; `math_recenter_limits()` bere `meas_cfg_t *`.
  ⚠️ **Nesjednoceno do jedné funkce pro všechny čtyři cesty** (jak F-0096 navrhoval):
  `scpi.c`/`ipc.c` mají vzor inline a fungují, a `meas_math.c` (kde `g_meas_cfg` bydlí)
  se linkuje do CM4 **bez FreeRTOS**, takže sdílený `taskENTER_CRITICAL` helper tam
  nemůže žít. Všechny UI cesty teď používají **identický** inline vzor jako scpi/ipc.

---

### F-0053 [S3] `fmt_fixed()` zná svou mez (1–3 desetiny), ale nevynucuje ji — `default:` tiše zahodí desetinnou část

- **Místo:** `CM7/app/app_gpsdo.c:390-404` (`fmt_fixed()`)
- **Popis:** Funkce umí formátovat 1–3 desetiny. Pro cokoli jiného spadne do `default:`,
  které vytiskne **jen celou část** — bez varování, bez ořezu, bez jakékoli stopy.
- **Důkaz:**
  ```c
  switch (decimals) {
  case 1: snprintf(buf, n, "%s%ld.%01ld", …); break;
  case 2: snprintf(buf, n, "%s%ld.%02ld", …); break;
  case 3: snprintf(buf, n, "%s%ld.%03ld", …); break;
  default: snprintf(buf, n, "%ld", (long)w); break;   /* ⬅ desetiny zmizí */
  }
  ```
  - **Není to teorie — už to jednou kouslo** (`CLAUDE.md`, STATUS #132): v okně MĚŘENÍ
    hlásilo `σ (n-1)` **vždy „0 Hz"** a v okně ANALÝZA se rozšířená nejistota `U (k=2)`
    zobrazovala jako **„+-0 Hz"** — tedy právě to číslo, kvůli kterému to okno existuje.
  - `CLAUDE.md` to sám uzavírá větou **„`default:` mlčí dál, takže past je pořád živá pro
    nové volající."** — tedy vada je známá a vědomě ponechaná, ale bez ochrany.
  - ✅ Ověřeno, že **dnes na to žádný volající nešlápne**: všech 6 shod grepu jsou komentáře
    (viz F-0054), žádné skutečné volání s `decimals ≥ 4` neexistuje.
- **Dopad:** Dnes nulový. Je to **past pro příští volání**, a to past tichá: špatné číslo
  se vykreslí, nic se nenahlásí, a odhalí to až člověk, který si všimne, že „σ je pořád 0".
  Přesně ta třída, kvůli které vznikla `L-0017`.
- **Reprodukce:** `fmt_fixed(b, sizeof b, 1.2345f, 4)` → `"1"` místo `"1.2345"`.
- **Návrh opravy:** Mez vynutit na rozhraní, ne ji jen dokumentovat. Nejlevněji:
  `default:` **ořízne na 3 desetiny** místo na nulu (tj. `decimals > 3` se chová jako 3) —
  výsledek je pak nepřesný, ale ne nesmyslný, a číslo desetin je vidět.
  Poctivější a v duchu `L-0017`: k tomu **počítadlo** `fmt_fixed` volání mimo rozsah do
  `status` (stejně jako `FONTY: preskocenych glyfu`).
  ⚠️ **Nepřidávat `configASSERT`** — spadlo by to uprostřed kreslení a IWDG by desku shodil
  kvůli formátovací drobnosti.
- **Riziko opravy:** nízké; `default:` dnes nikdo netrefí, takže změna chování je bezpečná.
- **Vztah k lekcím:** **`L-0015`** (modul, který zná svou mez, ji musí na rozhraní vynutit,
  ne jen odvozovat) a **`L-0017`** (tichý přeskok je přípustný jen s počítadlem).
- **Stav:** otevřeno

---

### F-0054 [S4] Kontrola, kterou `CLAUDE.md` předepisuje na past `fmt_fixed`, nemůže být nikdy zelená

- **Místo:** `CLAUDE.md`, oddíl „Text na displeji — TŘI tiché pasti" —
  *„Kontrola: `grep -rn "fmt_fixed([^;]*, *[4-9])" CM7` musí být **prázdný**."*
- **Popis:** Ten grep dnes vrací **6 shod** a žádná z nich není vada. Kontrola, která hlásí
  6 nálezů i ve zdravém stromě, přestane být kontrolou.
- **Důkaz:** výstup předepsaného příkazu:
  - `app_gpsdo.c:4875`, `:5817`, `:8589` — **komentáře**, které před tou pastí varují
    (např. `:5817` *„Drive `fmt_fixed(...,5)` = tise jen cela cast -> σ hlasila vzdy 0"*).
    Regex nerozlišuje kód od komentáře, takže **varování před pastí spouští poplach o pasti**.
  - `CM7/Debug/H757_LED_CM7.list` ×3 — grep míří na `CM7/`, takže čte i **výstupy
    zastaralého Debug buildu** (disassembly s vloženým zdrojákem). Ty tam být nemají vůbec.
- **Dopad:** Dvojí, a ten druhý je horší:
  1. Nikdo tu kontrolu nespustí dvakrát, protože „stejně vždycky něco najde".
  2. **Až se objeví skutečné volání, utopí se mezi šesti známými shodami** — a to je přesně
    ten stav, kdy měřidlo přestane měřit (`L-0011`, `L-0020`).
- **Reprodukce:** spustit příkaz z `CLAUDE.md` nad čistým stromem → 6 shod.
- **Návrh opravy:** Přesunout kontrolu tam, kde už kontroly žijí, a zúžit ji:
  `scripts/zakazane_vzory.txt` / `scripts/check_lessons.sh` hledá jen v `CM7/app`,
  `CM7/Core/Src`, `CM7/libui`, `CM7/libprim` (bez `Debug/`, `Release/`) a vyloučí řádky,
  které začínají komentářem. `CLAUDE.md` pak jen odkáže na skript místo vlastního receptu.
  ⚠️ A **pozitivní kontrola** (`L-0020`): ověřit nad kopií s vloženým `fmt_fixed(b, n, v, 4)`,
  že to skutečně zazní — jinak se jen vymění jedna nespolehlivá kontrola za druhou.
- **Riziko opravy:** žádné pro firmware (mění se jen nástroje a dokumentace).
- **Vztah k lekcím:** **`L-0011`** (nástroj, kterému se věří, se musí sám ověřit) a
  **`L-0020`** (kontrola bez pozitivní kontroly je zelené světlo).
- **Stav:** otevřeno

---

## Co bylo zkontrolováno a je v pořádku

Pro reprodukovatelnost auditu je tenhle seznam stejně důležitý jako nálezy — u modulu,
kde vyšly jen tři, dokonce důležitější.

**Známé pasti projektu (`CLAUDE.md`) — všechny dodržené:**
- **`%f`/`%e`/`%g` v `printf` rodině:** v kódu **0 výskytů** (3 shody grepu jsou komentáře
  vysvětlující, proč se nepoužívá). Formátuje se přes `fmt_fixed`/`fmt_sdec`/`fmt_dec_u`/
  `fmt_hz`. ✅
- **`sprintf` / `strcpy` / `strcat`:** **0 výskytů** v celém souboru. ✅
- **Subsetované fonty:** kryje je od modulu 8 počítadlo `FONTY: preskocenych glyfu`
  v `status` (F-0034), takže se to nemusí hlídat čtením. ✅

**E — meze polí a indexů.** Ručně ověřeny **všechny** indexy, kterými UI sahá do tabulek;
každý má ošetřenou obě hrany:
| index | tabulka | ochrana |
|---|---|---|
| `s_graph_idx` | `GRAPH_PRESETS` | `> 0` / `< GRAPH_PRESET_N-1` (`:8543`, `:8548`) |
| `s_gpsq_idx` | `GPSQ_PRESETS` | `> 0` / `< GPSQ_PRESET_N-1` (`:8754`, `:8758`) |
| `s_setup_slot` | sloty sestav | `> 0` / `< SETUP_N-1` (`:8720-8721`) |
| `s_wiz_br` | `WIZ_BR` | `% WIZ_BRANCH_N` (`:9084`) |
| `s_meas_nom_idx` | `MEAS_NOM` | `% MEAS_NOM_N` (`:8610`), navíc sanitace při obnově z syscfg (`:5654`) |
| `s_math_m_idx` / `s_math_band_idx` | `MATH_*_PRESETS` | modulo |
| `s_help_topic` | `HELP_ITEMS` | plní se z indexu seznamu, tedy `< HELP_N` |

**D — souběh.** Kromě F-0052 prověřeno:
- **`g_mon_cfg`** (okno PRAHY, `:5134-5141`) — **je v pořádku a je to jiný případ než
  F-0052**: všechna pole jsou `float`/`uint8_t`, tedy jednoinstrukční zápis, a UI si při
  každé změně **udržuje uspořádání** (`ocxo_lo_c <= ocxo_hi_c - 2.0f`), takže ani smíšená
  dvojice stará/nová nemůže dát invertované pásmo. `alarm.c:216` je tím krytý. ✅
- **`%s` nad řetězci, které píše jiná úloha** (`g_spi_text` z FpgaTasku 20 Hz `:746`,
  `g_tz_label` z defaultTasku 1 Hz `:2716`): **není to čtení za polem.** Obojí se plní
  vzorem `strncpy(dst, src, sizeof-1)` + `dst[sizeof-1] = '\0'`, a protože `strncpy`
  s délkou `n-1` poslední bajt **nikdy nepřepíše**, je pole trvale ukončené uvnitř svých
  mezí. Nejhorší možný důsledek je roztržený řetězec na jeden snímek, který se sám opraví
  při dalším překreslení. ✅ (Kontrolováno kvůli pravidlu „čti `%.Ns`" z `CLAUDE.md`;
  to platí pro `membench` snapshot, kde je situace jiná — soubor používá `%.Ns` 11×.)

**Vykreslovací pravidla (návaznost na moduly 8 a 10):**
- **Jediný poloprůhledný fill v modulu** (`:4131`, kužel holdoveru, `PRIM_ALPHA(cc, 0x22)`
  + `PRIM_BLEND_OVER`) obchází `mark_dirty` — ale leží celý uvnitř `REPLACE` fillu, kterým
  `holdover_cone_draw()` začíná (`:4110`), takže copy-forward přes tři buffery je pokrytý.
  U funkce je to i napsané. ✅ (Přesně třída F-0032.)
- Ostatních 21 fillů s `PRIM_BLEND_OVER` používá **neprůhledné** `UI_COLOR_*`
  (alfa `0xFF`), tedy DMA2D cestu, která `mark_dirty` dělá. ✅

**Watchdog / blokující práce:** okna s `datalog_read_back` se renderují jen při vstupu nebo
změně presetu, ne z 500ms tiku — ověřeno u GRAFŮ (`:1690-1697`, change-key stabilní per
preset) a ANALÝZY (`:4779`, `ana_recompute()` jen pod `if (first)`); KVALITA GPS v tiku
vůbec není. ✅

**Zásobník:** největší rámec mezi render funkcemi je **380 B** (`app_gpsdo_render_meas`),
následuje `math_render_controls` a `cd_redraw_all` po 324 B (`objdump`, `sub sp, #N`).
Proti **5 268 B** volného stacku UiTasku (`ARCHITECTURE.md` §7) je i nejhlubší řetěz
s rezervou. ✅ (Kontrolováno kvůli historii: projekt už dvakrát spadl na přetečený stack.)

## Nezkontrolováno / omezení tohoto běhu

- 🔴 **Nebyl to úplný přezkum řádek po řádku.** ≈6 200 řádků kreslicího kódu prošlo
  **rizikově cíleným** průchodem (viz tabulka „Rozsah a metoda"). Co se v něm nehledalo:
  **geometrie a vzhled** (překryvy rectů, zarovnání, ořezy textu) a **korektnost obsahu
  jednotlivých oken** (jestli okno ukazuje správnou veličinu ve správné jednotce).
  Obojí je viditelné na displeji a levnější na ověření pohledem než čtením.
- **`app_gpsdo_handle_touch()`** (`:8283-9031`, 57 testů `s_view`) je pročtený jen v místech,
  kam vedly stopy (okno MATH, meze indexů). Per-okno obsluha tapů zbývá.
- **Nic z tohoto modulu neběželo na HW** v rámci auditu — nálezy jsou statické.
  F-0053 a F-0054 jsou dokazatelné bez desky; **F-0052 je dokazatelný ze zdrojáku
  (tři volající, dva chráněné) a statisticky špatně chytatelný měřením**.
- **Okno CHYBY (`s_view=51`, errlog)** a **PRŮVODCE kalibrací (`s_view=40`)** dostaly jen
  průchod grepy, ne čtení — obě sahají na persistentní data, takže by si ruční čtení
  zasloužily.

---

### F-0063 [S2] Okno CHYBY (s_view=51) se po tapu na dlaždici nikdy nezobrazilo

- **Místo:** `CM7/app/app_gpsdo.c:3171-3245` (`app_gpsdo_render_errlog`, konec funkce),
  volající `:8502-8503` (`s_view == 48` → `TOOLS_ITEMS[i].fn()`),
  `CM7/Core/Src/freertos_task_ui.c:382` (UiTask → `alarm_click()`)
- **Popis:** `app_gpsdo_render_errlog()` na konci **neflipovalo**. Okna z tabulek
  `MENU_ITEMS`/`MEAS_ITEMS`/`TOOLS_ITEMS` se volají přes ukazatel a volající za ně
  flip nedodělá — obsluha tapu jen vrátí `true`. Okno se tedy vykreslilo do zadního
  bufferu a **nikdy se neukázalo**, přestože `s_view` už bylo 51.
- **Důkaz:**
  1. **Nahlásil uživatel z provozu** (2026-09-11): *„tlačítko log, když na něj
     kliknu, nic to neudělá, jen slyším klik."*
  2. 🔑 **Ten klik je důkaz, ne šum:** `freertos_task_ui.c:382` přehraje
     `alarm_click()` **právě když `app_gpsdo_handle_touch()` vrátí `true`**, tedy
     když byl dotyk obsloužen. Vstupní cesta je tím vyloučená.
  3. **Systematický výčet obou tabulek:** ze **22 oken** volaných přes ukazatel má
     21 ve svém těle `present_now()`/`s_dirty` a `render_errlog` jako **jediné** ne.
  4. **Není to regrese z oprav 2026-09-11** — `git log -S` ukazuje, že funkce
     vznikla commitem `0cc2d90` („ovladani logu dotykem/encoderem — okno CHYBY");
     commity toho dne se `app_gpsdo.c` nedotkly.
- **Dopad:** **Celé okno CHYBY bylo nedostupné oběma ovládacími cestami** (dotyk
  i enkodér volají tutéž `fn()`), tedy i trvalý záznam poruch ve W25Q, kvůli kterému
  okno vzniklo. ⚠️ Přístroj přitom v tom okně „byl" — další tap padl do obsluhy
  `s_view == 51` a tlačítko SMAZAT (jehož větev `present_now()` má) by okno konečně
  zobrazilo. Tedy stav, kdy displej ukazuje jiné okno, než jaké je aktivní.
- **Reprodukce:** Diagnostika → `NASTROJE >` → dlaždice **Chyby (log)** (vpravo
  nahoře). Před opravou se nic nezměnilo, jen zazněl klik.
  🔑 **Ověřitelné bez sondy:** `status` po tom tapu hlásí okno **51**, přestože na
  displeji jsou pořád NÁSTROJE — tedy přesně ta diagnostika, která vznikla
  opravou F-0047.
- **Návrh opravy:** `present_now()` na konec funkce.
- **Riziko opravy:** nízké (8 B `.text`, jedna funkce).
  ⚠️ Obsluha tapu na `EL_ERASE_RECT` (`:9013`) volá `render_errlog()` a hned po něm
  `present_now()` — po opravě jsou tam tedy dva flipy za sebou. **Ponecháno
  záměrně**: totéž má i okno DATALOG (`:9028`), takže se vzor nerozchází, a druhý
  flip stojí nejvýš jedno čekání na vblank (~17 ms) v UiTasku, který má limit 2,5 s.
- **Vztah k lekcím:** nová **`L-0029`**; při psaní její detekce se znovu potvrdila
  **`L-0020`** (test se nejdřív ukotvil na forward deklaraci a „nic nenašel").
- **Stav:** **opraveno 2026-09-11** (`1b21c82`), ⬜ **neověřeno na HW**.
  Provedeno podle návrhu. Doplněna trvalá detekce do `scripts/check_lessons.sh`
  a ověřena pozitivní kontrolou na třech funkcích.
