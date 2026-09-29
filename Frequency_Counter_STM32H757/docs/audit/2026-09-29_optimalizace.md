# Audit: optimalizace (průřezový)  (2026-09-29)

- **Commit:** HEAD větve `audit/2026-09-09-hodiny-pwr` (viz `787dee2`)
- **Jádro / doména:** CM7 (+ CM4 SPA/JS okrajově)
- **Projité sekce checklistu:** C (mapa paměti/bufferů — okrajově, žádné DMA1/2
  v dotčených souborech), D (souběh — jen tam, kde optimalizace zavádí sdílený stav)
- **Neprojité (a proč):** A/B/E/F/G/H — modul „optimalizace" není vázaný na
  konkrétní periferii ani na inicializační pořadí, je to průřezová vlastnost
  (rychlost kreslení, algoritmická složitost, cache guardy). Tyto sekce
  checklistu už pokryly moduly 1–23.

## Souhrn

„Optimalizace" **není samostatný modul se svými soubory** — je to vlastnost,
která už byla podrobně auditována (a z velké části i HW-ověřena) v rámci
**modulu 8** (vykreslovací řetězec — DMA2D backend, dirty-rect copy-forward,
glyph akcelerace, F-0032/33/35/36/140) a **modulu 24** (matematické funkce —
algoritmická složitost ADEV/MDEV, přesnost float vs double, F-0179, F-0182,
F-0186). Tenhle běh cíleně re-prověřil klíčové optimalizační mechanismy
**mimo** ty dva moduly (nebo místa, kde se od posledního auditu nezměnily,
ale ještě nebyly čteny s optimalizační optikou) a hledal nové S1–S3 nálezy.

**Verdikt: funkční.** Žádný nový S1/S2/S3 nález. Optimalizační kód v tomto
projektu je nadprůměrně dobře zdokumentovaný — každé netriviální zrychlení
(isqrt, lineární copy-forward pásy, `gate_same`, glyph cache, prefixové součty
v MDEV) nese komentář s **naměřeným** zdůvodněním, ne jen tvrzením, a je kryté
selftestem nebo `status` čítačem podle L-0016/L-0017.

---

## Co bylo zkontrolováno a je v pořádku

### 1. `CM7/libprim/src/gradient.c` — `isqrt32` (Newtonův floor sqrt)
Nahrazuje dřívější per-pixel lineární hledání (~O(√v) iterací) O(log v)
algoritmem pro radiální gradient pozadí. Ověřeno na hraničních hodnotách
(`v<=0→0`, `v=1→1`, `v=4→2`) — implementace je standardní a pixel-identická
s dřívější lineární verzí (komentář v kódu to tvrdí, algoritmus to potvrzuje:
Newtonova iterace pro celočíselnou odmocninu konverguje k floor(√v) shora).
Žádné přetečení `d2` na rozlišení 800×480 (max `d2` ~ 800²+480² ≈ 870 000,
hluboko pod `int32_t`).

### 2. `CM7/app/hal/stm32/prim_stm32_hal.c` — DMA2D backend + triple buffering
Toto je jádro modulu 8 a bylo už vyčerpávajícím způsobem prověřeno (F-0032
až F-0140, viz `docs/audit/2026-09-10_vykreslovaci-retezec.md` a záznam
modulu 8 v `AUDIT_STATUS.md`). Fresh re-čtení dnes potvrdilo:
- `d2d_wait()` — dvoufázová mez (levný spin + časová mez `D2D_WAIT_MS`) je
  zdokumentovaná naměřenou hodnotou (61 ms max při `d2ddt=240`, 800×480),
  ne odhadem (L-0016 dodrženo — historie ukazuje, že tady se dřív udělala
  přesně opačná chyba, dnes opravená).
- `copy_forward_dedup()` — insertion sort nad `band[]` (nejvýš `2×MAX_DIRTY`
  = 96 prvků) je O(n²) v nejhorším případě, ale n je tvrdě omezené konstantou
  a volá se nejvýš jednou za `present()` (~desítky Hz) — zanedbatelné proti
  DMA2D přenosu samotnému. Lineární pásy místo přesných obdélníků jsou
  zdokumentované jako **rychlejší** navzdory větší kopírované ploše (F-0140,
  měřeno 38→0 podtečení FIFO) — klasický případ, kdy „optimálnější" algoritmus
  (méně bajtů) byl ve skutečnosti pomalejší kvůli SDRAM stride penalizaci.
- `mark_dirty()` — rozsah kontrolován proti `back` bufferu (`dst < base ||
  dst >= base + FB_W*FB_H`), volá se vždy až po ořezu (`prim_internal_clip_rect`)
  v `libprim/src/fill.c`, takže `w,h` předané do `d2d_fill`/`d2d_blit_ex` už
  nemohou přesahovat framebuffer.

### 3. `CM7/libprim/src/text.c` — glyph akcelerace (DMA2D vs CPU)
- `s_glyph_accel` je globální přepínač, ale **všechna tři volací místa**
  v `screen_main.c` (řádky 1238/1240, 3321/3323, 3337/3339) jsou párová
  (zapnutí → kreslení → vypnutí) bez early-returnu mezi nimi a kreslí
  výhradně UiTask (jediný kreslič, viz zlatá pravidla „Vlákna") → žádný
  souběh ani únik stavu.
- Počítadlo chybějících glyfů (`s_missing_glyphs`, L-0017) je oddělené od
  `prim_text_width` (aby se při CENTER/RIGHT zarovnání nezdvojilo) — správně.
- Glyph cache (`s_gc[]`, klíč = ukazatel na coverage data) je bump alokátor
  bez eviction, dokumentovaný jako platný jen pro statické `const` fonty
  (runtime-generované glyfy by klíč rozbily) — komentář to výslovně varuje,
  žádný current caller runtime glyfy negeneruje.
- HW cesta (`hw_ok`) se použije jen při **plně neořezaném** glyfu
  (`cr.x==gx0 && cr.y==gy0 && cr.w==g->w && cr.h==g->h`), jinak spadá na
  CPU cestu se stejným výstupem (`prim_blend565`) — pixel-identické chování,
  jen jiná cesta.

### 4. `CM7/app/screens/screen_main.c` — `gate_same()` (STATUS #88, L-0016/17)
Guard „obsah je stejný → nekresli" napříč **třemi** framebuffery. Ověřeno,
že se volá na KAŽDÝ tik u všech čtyř nasazení (trend, 3× statistiky, CPU
blok, sys pilulka) a **není** schovaný pod podmínkou, která po usazení
přestane platit (přesně tahle třída chyby STATUS #88 dřív způsobila
problikávání — dnes je to kryté selftestem `gate_same` vektorů).

### 5. Decimační pyramidy — `trend_feed` (screen_main.c) vs. `sensor_hist.c`
Obě používají tentýž kaskádový decimační vzor (ring + akumulátor, ×4 nebo
×10 decimace mezi stage). `sensor_hist.c` explicitně cituje `trend_feed`
jako vzor. Zkontrolováno:
- `TREND_PRESETS[]` (app_gpsdo.c:225-234) je `int32_t` s explicitním
  komentářem „60 dní = 5 184 000 s by v int16 přeteklo" — L-0031 dodrženo.
- `s_trend_secs` (screen_main.c:3655) je taky `int32_t`.
- `sensor_hist.c` používá `float` pro akumulaci teplot/napětí (ne `double`
  jako ADEV pyramida po F-0179) — to je v pořádku, protože hodnoty (desítky
  až tisíce jednotek, rozlišení setiny/jednotky) nemají tu specifickou past
  F-0179 (relativní odchylka `|y| < 1/f` u frekvence blízké celému číslu);
  float má tu dost sig. číslic na absolutní teploty/napětí.
- `sensor_hist_series()`/`hist_pick()`/`hist_at()` — stejná bezpečná
  modulo-aritmetika jako `tr_pick`/`tr_at`, žádný rozdíl v kvalitě.

### 6. `dchg()` — change-detect cache pro partial redraw (app_gpsdo.c:756)
CLAUDE.md dokumentuje invariant „`sizeof(src) >= sizeof(cache)`" jako past.
Prošel jsem klíčová volání, kde `src` je **globální/volatile buffer** (ne
lokální `buf[]`/`key[]`, kde je invariant triviálně splněný):
- `dchg(c_fpga, sizeof(c_fpga)=68, (const char*)g_freq_info)` —
  `g_freq_info` je `volatile char[64]`, tedy **menší** než `sizeof(c_fpga)`.
  **Není to ale reálná vada**: `freertos_task_fpga.c:61` explicitně
  vynucuje `g_freq_info[63]='\0'` při každém zápisu, takže `strncmp`/
  `strncpy` s `n=68` narazí na NUL nejpozději na indexu 63 a nikdy nesáhne
  na indexy 64-67 (obě funkce se zastaví na prvním NUL, nečtou dál naslepo).
  Skutečný invariant, který `dchg` potřebuje, je „zdroj obsahuje NUL uvnitř
  `min(sizeof(src), n)` bajtů" — a ten je tady garantovaný jinou cestou
  (explicitním dopsáním terminátoru), ne velikostí bufferu. Totéž pro
  `dchg(c_link, sizeof(c_link)=64, (const char*)g_spi_text)` — `g_spi_text`
  je `volatile char[64]`, velikosti se přesně shodují a terminátor je
  vynucen (`freertos_task_fpga.c:148`).
  ⚠️ Je to **křehké** (past se aktivuje, pokud by někdo vynucení NUL
  odstranil), ale dnes není porušená — viz „Nezkontrolováno" níže.

### 7. Web SPA — `mdev()` (httpd_min.c, JS) po F-0182
Ověřeno, že oprava F-0182 (prefixové součty místo O(N²) klouzavého okna nad
druhými diferencemi fáze) je skutečně O(N) na délku řady: `mdev()` počítá
pole druhých diferencí `d[]` jednou (O(N)), pak posuvné okno o šířce `m`
s inkrementálním součtem `w` (žádný vnořený průchod). Potvrzeno čtením kódu
(`httpd_min.c:2377-2393`) — žádná regrese od zápisu F-0182.

### 8. Meze algoritmické složitosti v `adev_points()`/`noise_desc()`
`ADEV_PTS_MAX = ADEV_STAGES(6) × ALLAN_DENS_MAXK(9) = 54`. EDF smyčka
(řádky 2126-2137) má vnořené `while` posouvající se monotónně (hledání
sousedů ≥0,29 dekády), amortizovaně O(np), nikdy O(np²) — a i kdyby byla
O(np²), np≤54 je zanedbatelné pro kreslení jednou za vstup do okna/změnu dat.

---

## Nezkontrolováno / omezení tohoto běhu

- **Kompletní projití `httpd_min.c` (SPA JS, ~3060 řádků) mimo `mdev()`**
  z hlediska algoritmické složitosti — modul 15 (`2026-09-12_spa-web.md`)
  ho už auditoval funkčně, ale ne cíleně na výkon. Vzhledem k tomu, že SPA
  běží v prohlížeči (ne na omezeném MCU), riziko S1/S2 z pomalého JS je nízké.
- **`dchg()` invariant u zbylých ~90 volání** — projity jen ty, kde `src`
  je globální/volatile buffer menší než cíl (potenciálně nejrizikovější
  třída); zbytek jsou lokální `buf[]`/`key[]` se `sizeof(src) >= sizeof(cache)`
  z konstrukce (viditelné přímo v deklaraci o pár řádků výš), tedy nízké
  riziko a nebyly projity jednotlivě kvůli rozsahu (~90 volání).
- **HW-dependentní optimalizace ze STATUS.md** (#140 SDCLK 50→100 MHz,
  #144 SDMMC 16→25/50 MHz) — to jsou vědomě odložené HW úpravy (čekají na
  sériový odpor + bulk kondenzátor), ne nálezy kódu; zůstávají otevřené
  v `STATUS.md`, nepatří do tohoto auditu.
- **Žádný nový HW test neproběhl** — tento běh je čistě F3 (statický přezkum),
  beze změny kódu, takže se `⬜ neověřeno na HW` netýká (nic se neopravovalo).

---

## Dodatek — druhé kolo, zúženo na „efektivitu funkcí" (2026-09-29, tentýž den)

Uživatel upřesnil zájem konkrétně na **efektivitu funkcí** (zbytečná práce,
chybějící časné ukončení, práce mimo viditelný stav). Doplňkově prověřeno:

### 9. Dispatch tiku UI — `app_gpsdo_tick()` / `app_gpsdo_tick_anim()`
Obě funkce (`app_gpsdo.c:7911`, `:8209`) používají `if/else if` řetězec podle
`s_view`, takže se na každý tik provede **jen** obsluha aktuálně otevřeného
okna — žádné volání „pro jistotu" navíc pro okna, která nejsou vidět.
`survey_accumulate()` běží bezpodmínečně na pozadí (dokumentovaný záměr —
self-survey musí sbírat i mimo své okno).

### 10. `StartFpgaTask` smyčka (`freertos_task_fpga.c`, ~20 Hz poll)
`fpga_freq_format_status()` se volá na **každou** iteraci bez ohledu na to,
jestli se něco změnilo — ale je to levné formátování krátkého řetězce a
teprve `strncmp` uvnitř krátké kritické sekce (řádky 145-152) rozhoduje
o překreslení. Formátování běží mimo kritickou sekci (správně — krátká
kritická sekce). Konzistentní s `dchg()` vzorem použitým všude jinde v UI
vrstvě: „formátuj levně, kresli draze jen při změně". Žádný nález.

### Precedens v projektu — přesně tahle třída už byla nalezena a opravena
`F-0039`/`L-0021` (`docs/audit/2026-09-11_hlavni-obrazovka.md`) je
konkrétní příklad reálné neefektivity funkce, kterou tenhle projekt už
řešil: rekonstrukce ADEV pyramidy z datalogu po bootu četla záznamy
**jednotlivě** (`datalog_read_back`, ~173 µs režie QSPI příkazu na
~7 µs přenosu dat — **25× víc režie než dat**), což dělalo **1 h 47 min**
mrtvé statistiky po každém zapnutí. Oprava přešla na dávkové čtení
(`datalog_read_bulk`, 64 záznamů/QSPI příkaz) → **~4 min**. Lekce L-0021
z toho vytáhla obecné pravidlo: *„než začneš něco optimalizovat, přečti
hlavičku funkce, kterou voláš"* — bulk cesta už tehdy existovala a byla
prověřená, jen ji původní tři návrhy oprav neobsahovaly.

**Doplněný verdikt dodatku:** vzor „levné formátování/kontrola + drahé
kreslení jen při změně" a „dispatch jen na aktivní okno" je v aplikační
vrstvě (`app_gpsdo.c`, `screen_main.c`) i v real-time smyčkách
(`freertos_task_fpga.c`) prosazený důsledně a jeho jediné dosud nalezené
porušení (F-0039) je opravené a zdokumentované jako lekce. **Ale při
křížové kontrole tvrzení dokumentace proti skutečné konfiguraci buildu
vyšel jeden konkrétní nález** (viz níže).

---

### F-0198 [S4] `CLAUDE.md` tvrdí „-O2/Release", skutečná konfigurace obou jader je `-Os`

- **Místo:** `CLAUDE.md:711` (sekce „Akcelerace / linker"):
  > „Největší CPU výhra zůstává **-O2/Release**."
- **Popis:** Tvrzení je v přímém rozporu s (a) skutečnou konfigurací
  `.cproject` a (b) se zbytkem téhož dokumentu, který na více místech
  explicitně a s naměřenými čísly říká, že Release běží na `-Os`:
  > „Stavěj **Release (`-Os`)**, ne Debug (`-O0`) — −22 % velikosti, jinak
  > stejné defines." (sekce „Build / flash")
- **Důkaz:** `CM7/.cproject` — C compiler, Release konfigurace (řádek 177):
  `option id="...optimization.level.1983355161" ... value="...value.os"`.
  Totéž pro C++ compiler Release (řádek 211, `value.os`). Debug konfigurace
  (řádek 58) má naopak `value.o0`. Žádné místo v `.cproject` (CM7 ani CM4)
  nemá `value.o2`. Jediné výskyty řetězce „-O2" v celém projektu
  (`grep -rn "\-O2\b"`) jsou ve **`STATUS.md` #12/#13/#35** a tam jde o
  **dočasný audit sweep pro statickou analýzu** (`-Wall -Wextra -Wshadow
  -O2`, aby optimalizátor odhalil víc než `-O0`/`-Os`) — ne o produkční
  build flag. Naměřené hodnoty velikosti obrazu (595 616 B → … → 559 616 B
  CM7 Release, viz „Build / flash" sekce) jsou také zdokumentované jako
  `-Os`, ne `-O2`.
- **Dopad:** Čistě dokumentační — žádný kód se tímto neřídí (build skript
  i CubeIDE čtou konfiguraci z `.cproject`, ne z komentáře v `CLAUDE.md`).
  Riziko je **budoucí**: `CLAUDE.md` je „jediný soubor, který se drží
  aktuální" (bod „MAPA DOKUMENTŮ" na začátku souboru) a někdo by mohl větu
  vzít doslova a buď (a) mylně věřit, že Release už běží na `-O2` (a tedy
  že „přepnutí na -O2" už je vyčerpaná páka), nebo (b) skutečně přepnout
  optimalizaci na `-O2` v domnění, že to jen potvrzuje stávající stav —
  na MCU s těsným rozpočtem flash (fonty samy = 35 % obrazu, viz stejný
  dokument) by to bylo **necílené kompromitování velikosti za rychlost**
  bez měření, jestli je to vůbec potřeba (CPU je dnes zdokumentovaně pod
  kontrolou: UiTask 58 % po glyph akceleraci, I2C4 back-off 91→21 %).
- **Reprodukce:** Triviální — porovnání `grep` výstupu z `.cproject` s
  textem `CLAUDE.md:711`, viz „Důkaz" výše. Žádné HW ověření potřeba.
- **Návrh opravy:** Buď (a) oprava věty na „-Os/Release" (sladit se
  zbytkem dokumentu — to je pravděpodobně původní záměr, „-O2" je
  překlep/záměna s auditním sweep-flagem ze STATUS.md), nebo (b) pokud
  autor skutečně myslel „přechod na -O2 by byl další neuplatněnou pákou",
  přeformulovat na explicitní **HYPOTÉZU/TODO** („nevyzkoušeno: -O2 by
  mohlo dál snížit CPU, cena = větší `.text`, nutno změřit obojí") —
  ne tvrdit to jako hotový fakt slovem „zůstává".
- **Riziko opravy:** nulové — čistě `docs:` commit, žádná změna `.c`/`.h`/
  `.cproject`.
- **Vztah k lekcím:** stejná třída jako `L-0008` („komentář sliboval
  chování, které zbytek nevyvrací" — tady navíc konkrétní měřitelný
  rozpor s konfiguračním souborem, ne jen s jiným blokem kódu).
- **Stav:** ✅ opraveno 2026-09-29 (`CLAUDE.md:711`) — uživatel schválil
  skupinu A. Text přepsán na „Debug (`-O0`) → Release (`-Os`)" se stručným
  zdůvodněním rozporu inline. Čistě `docs:` — žádný `.c`/`.h`/`.cproject`
  se neměnil, build ani binárku to neovlivňuje (`CLAUDE.md` se nepřekládá).
  Doplněn `🔁` záznam k **L-0008** (širší instance „tvrzení vs. ověřitelný
  zdroj pravdy", tentokrát dokumentace vs. `.cproject`, ne komentář vs.
  zbytek funkce) — nová lekce nezaložena, jde o tutéž třídu.

---

**Závěr dodatku:** 1 nový nález (F-0198, S4, čistě dokumentační).
