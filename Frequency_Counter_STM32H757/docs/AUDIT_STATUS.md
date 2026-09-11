# AUDIT_STATUS.md — stav auditu

> Aktualizuj **na začátku a na konci každého sezení**. Tenhle soubor je jediný
> zdroj pravdy o tom, co je hotové — kontext CLI sezení se nepřenáší.

**Poslední aktualizace:** 2026-09-11 (10. sezení — modul 9 F5, pak modul 10 F3)
**Fáze:** modul 1 prošel F5 a je ✅ ověřený na HW; moduly 2–6 a 8 prošly F3 **i F5**, ale
⬜ **neověřeně na HW po power-cyklu** (nic z těch oprav studený start neviděl).
**Modul 7 má jen zapsané nálezy** — F5 zatím neproběhla (F-0025…F-0031, z toho 2× S2).
**Modul 8 nemá otevřený nález.**
**F1 je hotová** — `docs/ARCHITECTURE.md` doplněn 2026-09-10 z auditů 1–5 (dluh uzavřen).
**Branch:** `audit/2026-09-09-hodiny-pwr` (vychází z `feat/web-dashboard-v12`, commit `8521130`)
**Pokračovat zde:** ⬜ **naflashovat a ověřit po POWER-CYKLU** (viz níže); pak buď F5 pro
**rozhodnout F-0028** (SDMMC_CK 32 MHz proti 25MHz limitu Default Speed — tři varianty)
a pak dobrat F-0031 (`docs:`). Souběžně čeká F5 pro modul 11 (**F-0052 je S2** — dotyková
cesta píše `g_meas_cfg` bez kritické sekce, zatímco SCPI i IPC ji mají).
🔴 **Všechny moduly už mají zapsané nálezy** (1–11); zbývají jen fáze oprav a ověření na HW.
⚠️ **F-0039 čeká na rozhodnutí uživatele** (tři varianty) a blokuje ověření F-0037 na desce.
⚠️ Modul 6 byl v tabulce původně zapsaný jako „SPI2/FPGA, QSPI, SDMMC“ — přes 3000 řádků na
jedno sezení. **SDMMC proto dostalo vlastní řádek (modul 7)**, aby se neauditovalo povrchně.
⚠️ **Týmž způsobem se 2026-09-10 rozdělil modul 8** („aplikační logika UI“ = `app_gpsdo.c` +
`screens/*` + `libui`, dohromady **14 375 řádků** bez fontů — 4× víc než modul 7). Nově:
**8 = vykreslovací řetězec** (2 247 ř.), **9 = hlavní obrazovka** (3 272 ř.),
**10 = aplikační okna, navigace, model fokusu** (9 188 ř.).
⚠️ **A modul 10 se 2026-09-11 rozdělil potřetí, přesně jak si ten řádek žádal.** `app_gpsdo.c`
má **9 031 řádků a 310 funkcí** — 2,8× modul 9. Dělící čára vede podle **třídy rizika**, ne
podle velikosti: **10 = strojovna** (navigace, model fokusu, registr tlačítek, vstup dotyk/
encoder, tiky, screensaver, varovný pruh; ≈ 2 800 ř. — sdílený stav a cross-task API, tedy
S1/S2) a **11 = jednotlivá okna** (~45 funkcí `render_*` + kreslicí helpery; ≈ 6 200 ř. —
převážně kreslení, tedy očekávané S3/S4). Oba moduly jsou v témže souboru; nálezový dokument
modulu 10 v sekci „Nezkontrolováno" přesně vymezuje, co zbývá na 11.

**Otevřené po F5** (5× S3 + 1 částečně opravený S1 z modulů 1–6; modul 7 přidal
2× S2, 4× S3 a 1× S4, které F5 zatím neprošly). Čísla v tabulce výše se **odvozují
z nálezových dokumentů** — ověř je `python tools/audit_stav.py --kontrola`:
- **F-0003** [S3] `VOSRDY` bez timeoutu — *odloženo*: leží v generovaném `SystemClock_Config()`
  bez `USER CODE` (regen by opravu smazal) a špatná mez by pustila 480 MHz dřív, než se
  ustálí regulátor. Vrátit se, až se objeví „deska občas nenaběhne“.
- **F-0007** [S3] ztráta HSE = mrtvý přístroj — *čeká na rozhodnutí o politice*: zůstat mrtvý
  ale rozlišitelně (levné), nebo nabootovat na HSI a měření označit za neplatné (drahé, mění
  časování všech sběrnic).
- **F-0014** [S3] `ipccmd` je druhý producent SPSC ringu — *odloženo*: IPC dnes prokazatelně
  funguje (CM4 alive, ETH/web běží) a oprava sahá do živé mezijádrové cesty.
- **F-0016** [S3] `.ipc_shared` je prázdná rezervace — *odloženo*: oprava znamená zásah do
  **linker skriptů obou jader** (pravidlo 6 → jen s výslovným souhlasem).
- **F-0017** [S3] `ipc_stamp()` maže i blok CM4 — *odloženo* ze stejného důvodu jako F-0014.
- **F-0018** [S1] je opravený jen **částečně** (ztráta už není tichá); dvoufázový zápis zbývá.

Otevřené otázky na HW: crash black-box pro
`Error_Handler()` volaný **před** `MX_RTC_Init()` (modul 1) a retenční test `membench`
nad rozsahem `bg_cache` (modul 2, F-0013 — nástroj `bgcheck` už existuje).

## Přehled modulů

Stav: `nezačato` → `probíhá` → `nálezy zapsány` → `opraveno` → `komentáře hotové`

| # | Modul | Soubory | Jádro | Stav | Datum | S1 | S2 | S3 | S4 | Nálezy |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | konfigurace hodin/PWR | `main.c`, `system_*.c` | CM7 | opraveno (2 otevřené) | 2026-09-09 | 0 | 2 | 5 | 0 | [7](audit/2026-09-09_hodiny-pwr.md) |
| 2 | MPU / cache / linker | `main.c MPU_Config`, `*.ld` | oba | nálezy zapsány | 2026-09-09 | 0 | 0 | 4 | 2 | [6](audit/2026-09-09_mpu-cache-linker.md) |
| 3 | IPC CM7↔CM4 (HSEM) | `ipc.c`, `ipc_shared.h`, `ipc_cm4.c` | oba | nálezy zapsány | 2026-09-09 | 0 | 0 | 4 | 0 | [4](audit/2026-09-09_ipc-cm7-cm4.md) |
| 4 | přerušení a RTOS | `stm32h7xx_it.c`, `freertos*.c` | oba | nálezy zapsány | 2026-09-10 | 1 | 1 | 0 | 1 | [3](audit/2026-09-10_preruseni-rtos.md) |
| 5 | drivery: I2C1 + I2C4 | `i2c.c`, `*_sensors.c`, `*_ui.c`, `ft5x06.c`, `ws_panel.c` | CM7 | nálezy zapsány | 2026-09-10 | 0 | 0 | 2 | 0 | [2](audit/2026-09-10_i2c.md) |
| 6 | drivery: SPI2/FPGA + QSPI/W25Q | `fpga_freq.c`, `w25q.c`, `w25q_store.c` | CM7 | opraveno (⬜ neověřeno na HW) | 2026-09-10 | 0 | 0 | 1 | 1 | [2](audit/2026-09-10_spi-qspi.md) |
| 7 | drivery: SDMMC + FatFs | `sd_export.c`, `datalog_sd.c`, `sd_diskio.c`, `sdmmc.c`, `fatfs.c`, `bsp_driver_sd.c` | CM7 | **skupina A opravena** (2 otevřené, ⬜ neověřeno na HW) | 2026-09-11 | 0 | 2 | 4 | 1 | [7](audit/2026-09-10_sdmmc-fatfs.md) |
| 8 | vykreslovací řetězec | `prim_stm32_hal.c`, `libprim/*`, `libui/*` (bez fontů) | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-10 | 0 | 1 | 4 | 0 | [5](audit/2026-09-10_vykreslovaci-retezec.md) |
| 9 | hlavní obrazovka | `screens/screen_main.c`, `screen_main_data.c` | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-11 | 1 | 1 | 0 | 1 | [3](audit/2026-09-11_hlavni-obrazovka.md) |
| 10 | navigace, model fokusu, vstup, tiky | `app_gpsdo.c` (strojovna, ≈2 800 ř.) | CM7 | **opraveno vše** (⬜ neověřeno na HW) | 2026-09-11 | 0 | 1 | 3 | 2 | [6](audit/2026-09-11_navigace-fokus-vstup.md) |
| 11 | aplikační okna (`render_*`) | `app_gpsdo.c` (≈6 200 ř.) | CM7 | nálezy zapsány | 2026-09-11 | 0 | 1 | 1 | 1 | [3](audit/2026-09-11_aplikacni-okna.md) |

**Doporučené pořadí:** hodiny/PWR → mapa paměti/MPU/cache → IPC mezi jádry →
přerušení a RTOS → jednotlivé drivery periferií → aplikační logika.
Důvod: chyba ve spodních vrstvách se v horních projeví jako „náhodná“ nestabilita
a bez opravy základu se horní vrstvy auditují zbytečně.

## Souhrn nálezů

| Severity | Otevřené | Opravené | Zamítnuté (wontfix + důvod) |
|---|---|---|---|
| S1 | 1 | 1 | 0 |
| S2 | 1 | 8 | 0 |
| S3 | 7 | 21 | 0 |
| S4 | 2 | 7 | 0 |

⚠️ **Čísla nepiš ručně** — `python tools/audit_stav.py --kontrola` je odvodí z nálezových
dokumentů a při rozporu skončí nenulovým kódem (lekce **L-0014**). Sloupec „Otevřené“
zahrnuje i **částečně** opravené (dnes F-0018).

**Modul 9 — opraveno 2026-09-11 (⬜ neověřeno na HW):**
- **F-0037** [S1] frakční odchylka `y` počítaná pevným měřítkem `1e-14` → nový **jediný
  zdroj pravdy** `screen_main_frac_dev(double hz)`, používají ho `stats_sample()`
  i `stats_seed_tick()` v `app_gpsdo.c`. Lekce **L-0018**.
  ⚠️ **Účinek na desce zatím ověřit NELZE** — blokuje to F-0039 (viz níže):
  σy@1s zůstává 0, dokud běží rekonstrukce z datalogu.
- **F-0038** [S4] `rtc_time_date()` četl RTC bez ochrany proti přehoupnutí půlnoci →
  dvojí čtení se shodou (3 pokusy), stejný vzor jako `get_fattime()` ve `fatfs.c`.
- Navíc přidán řádek `STATISTIKA: sigma_y@1s` do `status` — bez něj nešla oprava F-0037
  na desce ověřit (σy byla čitelná jen z displeje).

**Modul 9 — nově otevřené (nalezeno AŽ ve fázi oprav, jako F-0036 u modulu 8):**
- **F-0039** [S2] ✅ **opraveno 2026-09-11** (⬜ neověřeno na HW), lekce **L-0021**.
  Rekonstrukce ADEV blokovala živé vzorkování ~1 h 47 min po každém bootu, protože
  rozpočet dávky byl spočítaný proti tiku 20 Hz, zatímco volající běží 1 Hz.
  🔑 Při opravě se ukázalo, že `datalog.h:203` u `datalog_read_back` **přímo varuje**
  „na průchod více záznamy použij `datalog_read_bulk`" — **včetně naměřených čísel**
  (~173 µs režie/záznam vs ~7 µs na data) — a bulk cesta už existovala a byla
  prověřená (používá ji web přes IPC). Žádná ze tří variant v nálezu ji neobsahovala,
  protože jsem hlavičku citované funkce nepřečetl.
  Zvolena **varianta „bulk + brzký konec"**: 8 dávek po 64 záznamech za tik ≈ 5 ms/tik
  → 512 zázn./s → **~4 min** místo 1 h 47 min, **kadence zůstává 1 Hz** (žádná nová
  expozice watchdogu). Navíc sonda: když nejnovější dávka nemá ani jedno použitelné
  měření, rekonstrukce se **vůbec nespustí** — což je dnešní stav (SPI link nenaběhl),
  takže dnes je blokování **nulové**. A řádek `ADEV rekonstrukce:` v `status`, bez
  kterého byla doba běhu neviditelná.
  ⚠️ **Tím se odblokovalo ověření F-0037 na desce** — σy@1s už nebude po bootu držená
  na nule.

**Modul 7 — skupina A opravena 2026-09-11 (⬜ neověřeno na HW):**
- **F-0025** [S2] tik volá `sd_export_unmount()` místo holého `f_mount(NULL,…)` → po
  vytažení a vložení karty se SD zase namountuje (dřív 30 s čekání a trvalý stav ERROR).
- **F-0026** [S2] `sd_export_busy_begin/end()` vystaveno v hlavičce; používá ho
  `screenshot_save_sd()` (osm chybových návratů → **obalka nad vyčleněným tělem**)
  i `ui_refresh_capacity()`. **Minimální varianta**, ne přesun vlastnictví unmountu —
  nález sám tu druhou označuje za střední riziko. Lekce **L-0022**.
- **F-0027** [S3] `gpio_cfg_lock()` kolem obou `HAL_GPIO_Init(GPIOC,…)` v SD cestě
  (v `sdmmc.c` přes USER CODE bloky → regen-safe).
  ⚠️ **PC8–PC12 se do `GG_PINS` ZÁMĚRNĚ nedoplnily** — hlídač by SD piny opravoval
  i v době, kdy je karta odmountovaná a mají být jinak. Vědomé rozhodnutí.
- **F-0029** [S3] `export_body()` kontroluje `f_close()` **i hlavičkový `f_write()`**.
- **F-0030** [S3] debounce detekce karty: dotaz oddělen od aktualizace
  (`datalog_sd_det_tick()` volá **jediná** úloha). Lekce **L-0023**.
- **Ověření:** build 0 varování, `audit.py` 92 OK/0/2, `.text` 599 176 → **599 272 B**
  (+96); v obrazu `datalog_sd_det_tick` (68 B), `sd_export_busy_begin/end`,
  `gpio_cfg_lock` nově volán z `HAL_SD_MspInit` i `sd_dat_pullup_enable`,
  `screenshot_save_sd` obepíná tělo dvojicí busy.
- **Zbývá:** **F-0028** (potřebuje rozhodnutí — 32 MHz vs. 25MHz limit Default Speed)
  a **F-0031** (`docs:`, ale body 2/3/5 se odvozují od hodnoty, kterou určí F-0028).

**Modul 11 — nálezy zapsány 2026-09-11 (F3; fáze oprav NEproběhla):**
Verdikt **podmíněně funkční**. Okna se chovají jako kreslicí kód — přesně jak modul 10
předpovídal. ⚠️ **Nebyl to přezkum řádek po řádku** (≈6 200 ř. na jedno sezení nejde);
proběhl **rizikově cílený průchod** podle tříd chyb, které projekt už prokazatelně vyrobil.
Rozsah i to, co se nehledalo, vymezuje sekce „Rozsah a metoda" v nálezovém dokumentu.
- **F-0052** [S2] okno MATH přepisuje `g_meas_cfg` (pět polí typu `double`) **bez kritické
  sekce**, zatímco `scpi.c:953-955` i `ipc.c:539-543` kolem téhož globálu kritickou sekci
  mají — a `ipc.c:526` dokonce v komentáři **výslovně počítá se souběžnými zápisy z UI**.
  🔴 Rozhodující je, že **tatáž funkce `meas_math_capture_null()` má tři volající a dva
  z nich pracují nad lokální kopií**; jen `app_gpsdo.c:8639` píše přímo do globálu.
  V obrazu jsou `lo`/`hi` dvě samostatné instrukce `vstr`. Nejmenší pásmo je 0,001 Hz,
  takže inverze `lo > hi` (→ falešný FAIL → 4 pípnutí) je dosažitelná. `L-0012`.
- **F-0053** [S3] `fmt_fixed()` zná mez 1–3 desetiny, ale `default:` tiše zahodí desetinnou
  část. Dnes na to nikdo nešlape (ověřeno), je to past pro příští volání — a **už jednou
  kousla** (σ hlásila vždy „0 Hz", STATUS #132). `L-0015` + `L-0017`.
- **F-0054** [S4] kontrola, kterou `CLAUDE.md` na tu past předepisuje, **nemůže být nikdy
  zelená**: grep matchuje i komentáře varující před pastí a čte i `CM7/Debug/*.list`.
  6 shod ve zdravém stromě → skutečné volání se v nich utopí. `L-0011` + `L-0020`.

**Modul 10 — opraveno vše 2026-09-11 (⬜ neověřeno na HW):**
Verdikt **podmíněně funkční**. Nic tu neshodí přístroj: modul nemá jediné volání `HAL_*`,
žádný DMA buffer (vše v AXI SRAM, ověřeno `nm`) ani čekací smyčku bez meze. Vada je
soustředěná do **účetnictví fokusu a do diagnostiky** a má jednoho jmenovatele — `s_view` je
rozvětvené **pěti** nezávislými tabulkami (46 `case` + 32 větví + 10 + 10 + 57 testů).
- **F-0046** [S2] `btnreg_sync_focus()` ukládá do `s_focus` **surový index do registru
  tlačítek**, zatímco zbytek modelu čte spojený prostor `ln + index` (`enc_paint` dělá
  `idx -= ln`). V pěti oknech se seznamem (MENU, MĚŘENÍ, NÁSTROJE, FUNKCE, NÁPOVĚDA) tedy
  ukazuje na jiný prvek — a přes `focus_store()` se ta špatná hodnota **trvale uloží**.
  🔴 Komentář nad tou funkcí přitom říká, že existuje proto, aby se obě ovládací cesty
  nerozešly (`L-0008`).
- **F-0047** [S3] `g_ui_view`/`g_ui_view_changes` (diagnostika „otevřelo se okno?") nepokrývá
  **17 ze ~45 oken** — včetně **hlavní obrazovky, MENU, MĚŘENÍ, NÁSTROJŮ, FUNKCÍ a NÁPOVĚDY** —
  protože ta se kreslí přes `window_prep()`, ne `window_first()`. A protože si drží předchozí
  hodnotu, **aktivně lže**: uživatel otevře MENU, `status` ukáže staré okno → vypadá to jako
  nepřijatý dotyk, tedy přesně ten mylný závěr, kterému měla zabránit (`L-0011`).
- **F-0048** [S3] `nav_push()` při plném zásobníku (6 míst, nejhlubší dnešní cesta 5) položku
  **tiše zahodí** a `nav_back()` pak vede jinam. Registr tlačítek v témže souboru to dělá
  správně (`s_btnreg_ovf` → `status`); tady chybí (`L-0017`).
- **F-0049** [S3] `render_view()` nezná okna 49 (FUNKCE), 50 (NÁPOVĚDA) a 13 (modal) → po
  obnově I2C4 (`app_gpsdo_touch_dead`) vyhodí uživatele na hlavní obrazovku. Přímý důsledek
  F-0050. ✅ Ověřeno, že `nav_back()` je v pořádku: všech 13 `nav_push` cílů `case` má.
- **F-0050** [S4] pět dispatch tabulek nad `s_view`. **Kandidát na odložení** — plošný refaktor
  je dražší než vada a `render_view` vs. screensaver se liší z doloženého důvodu; levnější je
  kontrola (odvozená tabulka + `_Static_assert`, nebo skript do `check_lessons.sh`).
- **F-0051** [S4] paměť fokusu per okno (zadání UI §7) se přeskočí, když se uživatel do okna
  vrátí **bez použití encoderu** — `focus_load()` má jediné volání, schované za
  `s_shown_view != s_view`, a `s_shown_view` nuluje jen obsluha encoderu.
⚠️ **F-0046, F-0047 a F-0051 sahají na totéž účetnictví** („kde jsem" + „kde je fokus") a mají
společnou opravu (jednotné `view_set()`); opravovat je odděleně by se pletlo.

✅ **Skupina B opravena 2026-09-11 jedním zásahem** (F-0046 + F-0047 + F-0051), lekce **L-0019**:
- `view_set(uint8_t)` = **jediné místo, kde se mění `s_view`**; nese diagnostiku okna i paměť
  fokusu. Všech **53** přiřazení `s_view = N;` jde tudy, `window_first()` diagnostiku už neplní.
- `cur_list()` = **jediný zdroj** mapování `s_view → seznam`; díky němu má `btnreg_sync_focus()`
  konečně `ln` a počítá ve spojeném prostoru (`ln + i`).
- `s_focus_shown` (dřív funkční statik `s_shown_view`) je file-scope a nuluje ho `view_set()`,
  takže paměť fokusu funguje i při navigaci prstem.
- ⚠️ **Sentinely `s_view = 0xFF` / `-1` zůstaly záměrně mimo** — nejsou to přechody na jiné okno.
- Ověřeno: build 0 varování, `audit.py` 92 OK/0/2 (gcc 14.3.1), `.text` 598 248 → **598 448 B**
  (+200), v obrazu `view_set` 76 B s **53 volajícími** a v `app_gpsdo_handle_touch`
  inlinovaný `bl enc_items_n` → **`add r0, r2`** (= `ln + i`). Detekce v `zakazane_vzory.txt`.
✅ **Skupina A opravena 2026-09-11:**
- **F-0048** — `s_nav_peak` + `s_nav_ovf` + řádek `UI: navigace (ZPET) max N/6` v `status`
  (tentýž vzor, jaký v témže souboru už měl registr tlačítek); `NAV_DEPTH` ze `sizeof`.
  ⚠️ **Pole zůstává na 6 záměrně** — nález sám říká, že zvětšení není oprava; teď je
  rezerva měřitelná, takže bump na 8 je následný krok, až `max` doleze na 6.
- **F-0049** — doplněn `case 49` a `case 50`. 🔑 U okna **13 (modal restartu) padlo
  rozhodnutí: neobnovovat** a vést do MENU — tlačítko NE už tam vede taky, takže obě cesty
  zrušení dialogu končí stejně, a potvrzení destruktivní akce se nemá samo vynořit.
  Okna 8 a 11 zůstávají mimo dispatch záměrně a je to u `default:` napsané.

✅ **Skupina C (F-0050) vyřešena KONTROLOU, ne sjednocením** — lekce **L-0020**:
`scripts/check_lessons.sh` nově hlásí okno s `view_set(N)` bez `case N:` v `render_view()`
i okno živě tikané, které `render_view` nezná. **Pět tabulek v kódu zůstává** (plošný
refaktor sahá na každé okno v kódu, který funguje); odstraněna je tichost, ne duplicita.
🔑 Provedena **pozitivní kontrola**: nad kopií bez `case 49`/`case 26` obě větve zazněly.
⚠️ První verze kontroly se kotvila na dopřednou deklaraci `render_view` a „nenašla" nic —
přesně proto se pozitivní kontrola dělá (táž past jako `-fanalyzer` + `-fsyntax-only`).

**Ověření skupin A+C:** build 0 varování, `audit.py` 92 OK/0/2, `.text` 598 448 → **598 656 B**
(+208), v obrazu `app_gpsdo_nav_stats` (32 B), oba nové řetězce a skoky `render_view` →
`app_gpsdo_render_func` / `_help` / `_menu`.
⬜ **Modul 10 celý čeká na flash + POWER-CYKLUS.**

**Modul 6 — opraveno 2026-09-10 (⬜ neověřeno na HW):**
- **F-0023** [S3] `w25q.c` nekontroloval adresu proti kapacitě čipu → `range_ok()` na začátku
  `w25q_read` / `w25q_write` / `w25q_erase_sector`; bez součtu `addr + len` (přetečení).
  Commit `1f69ca9`, lekce **L-0015**.
- **F-0024** [S4] ignorované návraty SW resetu v `w25q_init` → **zdůvodněno v kódu**, ne
  vyhodnoceno: `cmd_only` selhává jen na straně hosta a výsledek resetu už hlídá
  následující kontrola JEDEC ID. Commit `d7dbd69` (`docs:`, chování se nemění).

**Modul 1 — opraveno:** F-0001 (`pwrclk_check()` v USER CODE ověřuje dosažený stav napájení
a hodin, výstup do `status`), F-0002 (NMI zapisuje výpadek HSE do black-boxu, kind 7),
F-0004 (jen dokumentace: I2C je ~50 kHz, ne ~100), F-0005 (pojistka v `check_lessons.sh`).

**Modul 1 — otevřené:**
- **F-0003** [S3] čekání na `VOSRDY` bez timeoutu — **vědomě odloženo.** Leží v generovaném
  `SystemClock_Config()` (regen by opravu smazal) a špatně zvolená mez by pustila 480 MHz
  dřív, než se regulátor ustálí, tedy riziko rozbít fungující desku. Vrátit se k tomu,
  pokud se objeví „deska občas nenaběhne“.
- ~~**F-0006** [S3] `WRHIGHFREQ`~~ — ✅ **UZAVŘENO 2026-09-10**: `status` hlásí `WRHIGHFREQ=3`,
  tedy nenulovou (ne reset default). Flash má zpoždění naprogramované, nález padá.
- **F-0007** [S3] ztráta HSE = mrtvý přístroj bez rozlišitelné diagnózy — čeká na rozhodnutí
  o politice (zůstat mrtvý, ale rozlišitelně / nabootovat na HSI a měření označit za neplatné).
- ~~S4 (z opravy F-0002): řádek `HSE: CSS…` ve `status`~~ → převzato modulem 4 jako **F-0019**.

**Stav ověření: ✅ OVĚŘENO NA HW po power-cyklu (2026-09-10).**
`status` po studeném startu hlásí `NAPAJENI/HODINY: OK SYSCLK 480 MHz HCLK 240 MHz WRHIGHFREQ=3`
→ **F-0001 i F-0006 uzavřeny** (kontrola napájení/hodin funguje; `WRHIGHFREQ=3` je nenulová,
tedy ne reset default — Flash má naprogramované zpoždění a nález F-0006 tím padá).
`SDRAM refresh: SDRTR=371, ve zdrojaku 371` a `SDRAM cteni: rpipe=1 HCLK | I/O kompenzace READY (CSI ok)`.

✅ **Problikávání displeje (#237/#238/#72) VYŘEŠENO 2026-09-10 — příčina byla ČTECÍ CESTA FMC.**
`ReadPipeDelay = 0` + nikdy nezapnutá I/O kompenzační cela. Naměřeno po opravě:
`membench` **0 chybných bitů** (bylo 3 338 207), retence **0** (bylo 496 068), překryv adres
zmizel, `LTDC podtečení` **0/1000** (bylo 1217/1000), displej po power-cyklu v pořádku.
⚠️ Paměť ani `FMC_A9`/`PF15` vinné nebyly — nová položka v tabulce „HW obviněn a byl nevinný“.
⚠️ Poučení z cesty k tomu je **L-0011** (převzal jsem hypotézu, kterou nabídl nástroj,
místo abych přečetl jeho čísla) — stálo to jeden flash cyklus a jednu vrácenou změnu.

## Log sezení

| Datum | Modul | Co se udělalo | Nové lekce |
|---|---|---|---|
| RRRR-MM-DD | — | inicializace kitu | — |
| 2026-09-09 | hodiny/PWR | F3 přezkum, 7 nálezů (2×S2, 5×S3). Přepočítán celý hodinový strom vč. odvozených frekvencí konzumentů (FMC/SDCLK, LTDC, ADC, SPI123, SDMMC, timery) — sedí až na I2C. Kód neměněn. | zatím žádná (lekce se zapisují až po opravě, F5) |
| 2026-09-09 | hodiny/PWR | F5 opravy: 4 nálezy uzavřeny v 5 commitech (2× `docs:`, 2× `fix:`, 1× `docs:` na komentář). ⚠️ Obě opravy firmwaru jsou zatím jen **přeložené** (build 0 varování, `audit.py` v baseline, `.text` 594 504 → 595 368) — **na HW po power-cyklu NEOVĚŘENO**, viz L-0010. F-0003 vědomě odloženo. | L-0006 … L-0009 |
| 2026-09-09 | hodiny/PWR | Reakce na hlášení „po power-resetu se rozbije displej“: doloženo, že opravy do hodin **nezapisují**, a symptom dohledán jako otevřené #141/#237/#238. `pwrclk_check()` přesto přesunuta až za bring-up displeje. Doplněn power-cyklus do ověřovacího řetězce. | L-0010 |
| 2026-09-09 | MPU/cache/linker | F3 přezkum, 6 nálezů (4×S3, 2×S4), verdikt **funkční**. Mapa 32 MB SDRAM, 4 MPU oblasti a umístění objektů ověřeny proti obrazu (`nm`), ne proti zdrojáku. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-09 | IPC CM7↔CM4 | F3 přezkum, 4 nálezy (4×S3), verdikt **funkční**. Seqlock, SPSC ringy i čtenář na CM4 přečteny řádek po řádku — v jádru protokolu chyba není; nálezy jsou invarianty držené jen komentářem. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | hodiny/PWR + SDRAM | ✅ **HW ověření po power-cyklu.** F-0001 a F-0006 uzavřeny ze `status`. Problikávání displeje vyřešeno: příčina byla čtecí cesta FMC (`rpipe=0` + vypnutá I/O kompenzace), ne obnova a ne vadná paměť — `membench` 0 chybných bitů, LTDC podtečení 0/1000. Uzavřeno STATUS #237/#238/#72. | L-0011 |
| 2026-09-10 | přerušení a RTOS | F3 přezkum, 3 nálezy (1×S1, 1×S2, 1×S4), verdikt **podmíněně funkční**. Priority ISR, grouping, timebase i hooky v pořádku; stacky změřeny z běžícího přístroje (`stats`). Obě funkční vady jsou diagnostika, která selže právě při poruše. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | drivery I2C | F3 přezkum, 2 nálezy (2×S3), verdikt **funkční**. Modul s nejdelší historií incidentů je dnes dobře ošetřený; oba nálezy jsou o tom, že se dodržené pravidlo neuplatnilo všude. **Navíc doplněn `docs/ARCHITECTURE.md`** z auditů 1–5 → F1 uzavřena. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | F5 opravy (moduly 2–5) | Opraveno 10 nálezů ve 3 commitech: F-0008, F-0011, F-0015 (pojistky), F-0018 částečně, F-0019, F-0020, F-0021, F-0022 (tiché vady zviditelněny), F-0009, F-0010, F-0012 (dokumentace). Build 0 varování, audit.py baseline, `.text` 597 232 → 597 488. **⬜ neověřeno na HW.** Rozšířen `audit-modul` o fázi F5. | L-0012, L-0013 |
| 2026-09-10 | drivery SPI2/FPGA + QSPI | F3 přezkum, 2 nálezy (1×S3, 1×S4), verdikt **funkční**. Oba drivery jsou blokující (grep na DMA/IT prázdný → sekce C odpadá). Klíčové zjištění: **`NOLINK` není přičitatelný ovladači na CM7** — CS boot level, AFCNTR, časování i CRC gate jsou v pořádku. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-11 | hlavní obrazovka | F3 přezkum, 2 nálezy (1×S1, 1×S4), verdikt **podmíněně funkční**. Vykreslovací část je v dobrém stavu — `gate_same` je nasazený podle vlastního pravidla (na každý tik, vlastní `reps` na kartu) a každý partial redraw začíná neprůhledným clearem. Vada je v **metrologii**: **F-0037 [S1]** — frakční odchylka `y` se počítá pevným měřítkem `1e-14`, které platí jen pro `frac=7` a `f0=10 MHz`; obojí je dynamické. Táž veličina se přitom v `app_gpsdo.c:7899` počítá SPRÁVNĚ a sype se do TÉŽE ADEV pyramidy. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | F5 opravy (modul 8, 2. dávka) | **F-0036** opraven variantou „mez v ms + zrušení přenosu + čítač nejdelšího čekání“ (commit `d3a099e`). 🔑 Ten čítač hned vyvrátil můj vlastní odhad: nejdelší legitimní čekání je **~61 ms**, ne ~12 ms — byl jsem 5× vedle a první verze s mezí 100 ms by byla HORŠÍ než původní stav (nově se přenos při vypršení ruší). Po změření mez 500 ms, rezerva ~8×. Na desce `chyb 0, timeout 0, max cekani 56,3 ms`. **F-0032** uzavřen dokumentací (`CLAUDE.md` + hlavička `prim_stm32_hal.c`) — oprava kódem by zhoršila F-0036, viz odůvodnění v nálezu. Modul 8 tím nemá otevřený nález. | rozšíření L-0016 |
| 2026-09-10 | F5 opravy (modul 8) | Skupina A: **F-0033** (chyby DMA2D se přestaly mazat naslepo + 3 počítadla a řádek `DMA2D:` ve `status`), **F-0034** (počítadlo přeskočených glyfů + řádek `FONTY:`), **F-0035** (`__DSB()` před startem DMA2D, ověřeno v disassembly). Build 0 varování, `audit.py` 92/0/2, `.text` 597 520 → 597 832. 🔑 **Oprava F-0033 hned odhalila nový nález F-0036** [S2]: hlídací mez v `d2d_wait()` vyprší i na legitimním celoobrazovkovém přenosu (14 vypršení za 25 s, roste s kreslením) a DMA2D se pak přeprogramuje za běhu. **F-0032 a F-0036 zůstávají otevřené.** ⬜ neověřeno na HW po power-cyklu. | L-0016, L-0017 |
| 2026-09-10 | vykreslovací řetězec | F3 přezkum, 4 nálezy (4×S3), verdikt **funkční**. **Modul 8 nejdřív rozdělen** (14 375 ř. → 8/9/10, viz poznámka nahoře). Nic dnes nekreslí špatně; všechny nálezy jsou latentní pasti a chybějící diagnostika. Nejzávažnější F-0032: pravidlo „partial redraw musí začít clear“ platí jen pro neprůhledné barvy, `sw_fill` obchází `mark_dirty`. Ověřeno rozborem indexů, že copy-forward nikdy nepíše do scanovaného bufferu, a že `keep[96]` v dedupu sedí přesně na mez. Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | drivery SDMMC + FatFs | F3 přezkum, 7 nálezů (2×S2, 4×S3, 1×S4), verdikt **podmíněně funkční**. Blokující CPU/FIFO cesta místo IDMA je doložitelně správné rozhodnutí; ručně skládaný init obchází dvě vendor smyčky s timeoutem ~49 dní. Slabiny jsou v životním cyklu okolo mountu: **výměna karty za běhu je rozbitá deterministicky** (F-0025) a auto-unmount z defaultTasku umí smazat FatFs semafor drženy jiným taskem (F-0026). Umístění všech bufferů ověřeno `nm` nad `.elf`. Dvě falešné stopy prověřeny a zavrženy (BusFault přes `disk_status`, L-0007 přes `HAL_RCCEx_PeriphCLKConfig`). Kód neměněn. | zatím žádná (F5 nebyla) |
| 2026-09-10 | F5 opravy (modul 6) | F-0023 mez proti kapacitě W25Q (`fix:` `1f69ca9`), F-0024 zdůvodnění ignorovaných návratů (`docs:` `d7dbd69`). Před zásahem ověřeno, že žádný volající na hranici neleží. Build 0 varování, `audit.py` 92/0/2, `.text` 597 488 → 597 520 a mez `cmp.w r0, #67108864` dohledána v disassembly. **Přeložen i CM4/Release** — obraz byl starší než `ipc_shared.h` (assert z F-0017), takže `build.sh` varoval na možný nesoulad bank. **⬜ neověřeno na HW.** | L-0015 |
