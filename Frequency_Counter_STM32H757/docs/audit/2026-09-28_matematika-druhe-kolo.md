# Audit: matematické funkce měření — druhé kolo (2026-09-28)

- **Commit:** `17c216e` (HEAD větve `audit/2026-09-09-hodiny-pwr`)
- **Jádro / doména:** CM7 (`meas_math.c`, `phase_noise.c`, `meas_present.c`,
  matematické úseky `screen_main.c`, `app_gpsdo.c`, `fpga_freq.c`, `syscfg.c`,
  `setup.c`) + CM4 sdílený `scpi.c`/`ipc_scpi.c`/`httpd_min.c` (dBm převod,
  rozpočet nejistoty v `/api/state`)
- **Projité sekce checklistu:** D (souběh — hlavní ohnisko tohoto kola), E
  (chybové stavy — kontrola proti regresi F-0159/F-0160/F-0170)
- **Neprojité (a proč):** A, B, C, F, G, H — modul nesahá na hodiny/PWR,
  dual-core boot, DMA/cache, Flash/bootloader ani periferie mimo QSPI (a tu
  jen nepřímo přes `syscfg_load`/`setup_load`, obé už dřív auditované jinými
  moduly)

## Souhrn

Toto **není** audit modulu 24 od nuly — ten proběhl čtyřikrát (2026-09-26 ×3,
kritický průchod 2026-09-27, viz `2026-09-26_matematika-mereni*.md` a
`2026-09-27_matematika-kriticky.md`) a je nejprocházenější modul projektu
(36 dosavadních nálezů, ⇒ vysoká hustota pokrytí). Tohle kolo je cílený
**druhý průchod** zaměřený na to, co předchozí kola z povahy věci nemohla
systematicky zachytit: (a) checklist sekce D/E aplikovaná na CELÝ modul najednou
(dřívější kola šla spíš po správnosti vzorců), (b) kontrola modulu proti
**každé** lekci v `docs/LESSONS.md` (bod 5 postupu), ne jen proti těm, které
vznikly přímo z něj.

**Verdikt: podmíněně funkční.** Jádrová matematika (`meas_math.c`,
`phase_noise.c`, `meas_present.c`) je po čtyřech kolech prakticky bezchybná —
každá netriviální větev má vlastní selftest vektor a citovanou lekci. Ruční
rozbor `mp_fit_solve`/`mp_fit_significant`/`pn_compute`/`meas_limit_eval`
nenašel nic nového. **Jeden nový nález S3**: `syscfg_load()` (`syscfg.c`)
přepisuje `g_meas_cfg` po jednotlivých polích **bez kritické sekce** — přesně
ta třída chyby, kterou L-0018 (pokračování k 2026-09-18) popisuje jako
opravenou na **všech** tehdy známých místech (`scpi.c`, `ipc.c`, okno MATH
v `app_gpsdo.c`, `setup_load`). `syscfg_load()` je páté místo, které do
sweepu nespadlo.

---

### F-0197 [S3] `syscfg_load()` je páté místo přepisující `g_meas_cfg` bez kritické sekce — L-0018/L-0096 se nedodrželo dopočtem

- **Místo:** `CM7/Core/Src/syscfg.c:249-260` (`syscfg_load()`).
- **Popis:** Po načtení blobu z W25Q funkce zapisuje devět polí `g_meas_cfg`
  (`math_en`, `null_en`, `limit_en`, `alarm_en`, `m`, `b`, `null_ref`, `lo`,
  `hi`) jedno za druhým **přímo do globálu**, bez `taskENTER_CRITICAL()`:
  ```c
  g_meas_cfg.math_en  = b.meas_math_en ? 1 : 0;
  g_meas_cfg.null_en  = b.meas_null_en ? 1 : 0;
  g_meas_cfg.limit_en = b.meas_limit_en ? 1 : 0;
  g_meas_cfg.alarm_en = b.meas_alarm_en ? 1 : 0;
  g_meas_cfg.m        = (b.meas_m != 0.0) ? b.meas_m : 1.0;
  g_meas_cfg.b        = b.meas_b;
  g_meas_cfg.null_ref = b.meas_null_ref;
  g_meas_cfg.lo       = b.meas_lo;
  g_meas_cfg.hi       = b.meas_hi;
  if (g_meas_cfg.lo > g_meas_cfg.hi) { … prohodit … }
  ```
  Srovnej s **`setup.c:141-155`** (`setup_load()`), které dělá strukturálně
  TOTÉŽ (aplikuje uložený math/limit blok na `g_meas_cfg`) a má u toho
  doslovný komentář vysvětlující, proč se to nesmí dělat pole po poli:
  > „g_meas_cfg (5 double) commitovat ATOMICKY — je sdilene s SCPI (UartTask)
  > a IPC/syscfg (defaultTask); primy zapis pole po poli by vydal roztrzenou
  > dvojici lo/hi. Vzor shodny se scpi.c/ipc.c a s oknem MATH (F-0096, F-0052)."
  `setup_load()` proto čte `g_meas_cfg` do lokální kopie pod zámkem, mutuje ji
  a **commituje atomicky** pod druhým zámkem. `syscfg_load()` — sesterská
  funkce se stejným účelem (aplikace persistovaných math/limit hodnot),
  volaná ze stejného tasku (UiTask, přes `app_gpsdo_init()`) — tenhle vzor
  nemá vůbec.
- **Důkaz:** `grep -n "g_meas_cfg\s*=\|g_meas_cfg\." CM7/**/*.c` ukazuje pět
  zapisovatelů: `ipc.c:635`, `scpi.c:1065`, `setup.c:156`, `app_gpsdo.c:9090`
  (všechny čtyři `taskENTER_CRITICAL(); g_meas_cfg = <lokální kopie>;
  taskEXIT_CRITICAL();`) a `syscfg.c:249-259` (bez zámku, přímý zápis pole po
  poli). `g_meas_cfg.limit_en`/`alarm_en` čte `alarm_tick()` (`alarm.c:382`,
  **defaultTask**, Normal priorita); `g_meas_cfg.lo`/`hi`/`limit_en` čte
  `meas_limit_eval(&g_meas_cfg, …)` z `app_gpsdo.c:8512` (**UiTask**, stejný
  task jako `syscfg_load`, tam bez rizika). `syscfg_load()` běží z
  `app_gpsdo_init()` (`app_gpsdo.c:351-368`, volané `window_prep()` →
  `app_gpsdo_render_main()`, chráněné příznakem `s_inited` — proběhne jen
  **jednou**, hned na startu UiTasku, PŘED prvním vykreslením). UiTask má
  prioritu **BelowNormal**; defaultTask (kde běží `alarm_tick()`) má
  **Normal** — vyšší prioritu, takže FreeRTOS ho může preemptivně spustit
  kdykoli uprostřed UiTasku, včetně přesně mezi dvěma po sobě jdoucími zápisy
  do `g_meas_cfg` v `syscfg_load()`.
- **Dopad:** Okno je úzké (jen boot, jen během devíti po sobě jdoucích
  zápisů — řádově desítky ns) a nejhorší pozorovatelný důsledek je mírný:
  `alarm_tick()` čte jen `limit_en`/`alarm_en` (bity, ne `lo`/`hi`), takže
  roztržený stav nejhůř způsobí, že se jedno vyhodnocení alarmu (throttled na
  200 ms) přeskočí nebo mylně spustí — ne trvalý FAIL jako u F-0052/F-0096
  (tam šlo o roztrženou dvojici `lo`/`hi` čtenou přímo v `meas_limit_eval`,
  což `alarm_tick` nedělá). Nejde tedy o stejně těžký dopad jako předlohy,
  ale o **stejnou třídu vady** v místě, které mělo být opravou pokryté a
  nebylo — klasifikováno S3 stejně jako F-0096 (menší riziko/frekvence než
  F-0052, ale reálná nekonzistence s dokumentovaným vzorem).
- **Reprodukce:** `HYPOTÉZA` — vyžaduje trefit preempci UiTasku defaultTaskem
  přesně mezi dvěma konkrétními instrukcemi při bootu; na běžícím přístroji
  prakticky neopakovatelné bez instrumentace (breakpoint na `syscfg.c:254`
  by navíc sám o sobě boot ovlivnil). Statický důkaz (asymetrie vůči
  `setup_load()` a dokumentovanému vzoru) je ale jednoznačný bez měření.
- **Návrh opravy:** stejný vzor jako `setup_load()` — načíst `g_meas_cfg` do
  lokální kopie (netřeba ani pod zámkem, protože se čte jen pro `if (lo>hi)`
  swap, ale konzistence s ostatními místy mluví pro zámek i tady), aplikovat
  devět polí na lokální kopii, commitnout pod `taskENTER_CRITICAL()`.
  Minimální diff, žádná změna formátu blobu, žádný bump magicu.
- **Riziko opravy:** nízké — mění se jen tělo `syscfg_load()`, syscfg_load
  běží jednou při bootu, žádný jiný kód na časování tohoto úseku nespoléhá.
- **Vztah k lekcím:** `L-0018` (pokračování 2026-09-18, F-0052+F-0096) —
  tohle je jeho **třetí opakování** stejné třídy ve stejném globálu, jen
  v pátém, dosud nezkontrolovaném volajícím. `L-0026`/pravidlo 5 z
  `docs/templates/LESSON.md` („po přidání lekce zkontroluj zbytek projektu
  na stejný vzor") — přesně tahle kontrola se u `syscfg.c` v 2026-09-18
  evidentně nestihla.
- **Stav:** otevřeno — čeká na schválení opravy uživatelem (§F5 workflow).

---

## Co bylo zkontrolováno a je v pořádku

- **`meas_math.c`** (celý soubor, 97 řádků): `meas_math_apply`, `meas_limit_eval`
  (NaN-safe od F-0160, ověřeno negovanými porovnáními `!(y >= lo)`),
  `meas_math_capture_null` — všichni tři reální volající (`ipc.c:574`,
  `scpi.c:351`, `app_gpsdo.c:9073`) operují nad **lokální kopií** `meas_cfg_t`
  a commitují ji atomicky; žádný nepíše přímo do `g_meas_cfg` uvnitř funkce.
- **`meas_present.c`** (celý soubor, 558 řádků): `mp_stats_*` (Welford,
  korektní i pro n<2), `mp_period_sample_s` (guard `mul!=0 && edges!=0 &&
  dt_ps!=0`, degradace na `1/f` je bezpečná), `mp_nominal_auto`,
  `mp_deviation` (guard `nominal==0`), `mp_budget` (guard `gate_s<=0` →
  `valid=0`, F-0159), `mp_fit_add`/`mp_fit_solve`/`mp_fit_significant`
  (centrované akumulátory F-0169, NaN-safe F-0170, autokorelace F-0173,
  `T95_2S[df-1]` index ověřen v mezích `df∈[1,30]` díky `ne>=3.0` guardu
  před castem na `uint32_t`). `mp_ad8307_dbm`/`mp_ad8307_slope_ok` (F-0165,
  jediný zdroj) — ověřeno, že žádné volací místo (`app_gpsdo.c` 6×, `scpi.c`
  2×, `httpd_min.c` 1×, `ipc.c` mirror) nemá vlastní duplicitní vzorec;
  webová `drawRf()` v SPA jen zobrazuje už spočtené `s.rf_dbm`, nepočítá
  znovu.
- **`phase_noise.c`** (celý soubor, 198 řádků): `pn_fft` (radix-2, in-place,
  bez alokace), `pn_compute` — meze smyčky `for (int k=0;k<nb;k++)` a
  `nb = min(PN_NFFT/2-1, max_pts)` brání přetečení `out[]`; segmentová smyčka
  `off -= PN_NFFT/2` zůstává vždy v `[0, n-PN_NFFT]`, tedy uvnitř `y[]`.
  Volající `screen_main_phase_noise()` (`screen_main.c:1526`) používá
  `static` buffery (`chron[STAT_N]`, `pts[PN_NBINS]`) — bezpečné i na
  UiTasku, `n = s_y_count <= STAT_N` odpovídá kapacitě `chron[]`.
- **`fpga_freq_hires_mul/_uhz/_hz`, `fpga_freq_dt_ticks`** (`fpga_freq.c`):
  přepočítáno ručně — `fpga_freq_hires_uhz`'s dlouhé dělení na 6 desetin
  nepřeteče (`rem<t`, `t` max ~8,6e9 pro 21,5s okno, `rem·10` bezpečně
  v `uint64_t`); `fpga_freq_hires_mul`'s pojistka `edges > 4e9/MUL[i]`
  brání přetečení násobku před porovnáním s referencí.
- **Souběh `g_meas_verdict`** (`volatile uint8_t`, jediný zapisovatel
  `app_gpsdo.c:8512` v UiTasku, čtenáři `alarm.c` třikrát) — jednoslovný
  přístup je na Cortex-M7 atomický, `volatile` je přítomné, žádný nález.
- **Regresní kontrola proti F-0159/F-0160/F-0170/F-0173/F-0186/F-0187/F-0193**
  (nejnovější lekce modulu) — žádná z chráněných cest se od kritického
  průchodu 2026-09-27 nezměnila mimo `fpga_freq.c` (F-0195/F-0196, viz
  `2026-09-28_hw-test-f0194-f0195-f0196.md`), ty jsou pokryté samostatně a
  ověřené na HW.

## Nezkontrolováno / omezení tohoto běhu

- **F-0197 je HYPOTÉZA co do reprodukce** — mechanismus (chybějící kritická
  sekce) je doložený ze zdrojového kódu jednoznačně, ale vyvolat ho na živé
  desce (přesné načasování preempce při bootu) realisticky nejde bez
  instrumentovaného buildu.
- Neprocházel jsem znovu řádek po řádku `app_gpsdo.c` okno MATH/LIMITY
  (~150 řádků UI) ani `scpi.c`/`ipc_scpi.c` SCPI handlery pro `CALC:*` —
  ty prošly F3 auditem už 2026-09-11/19 a critickým průchodem 2026-09-27;
  tohle kolo se soustředilo na to, co předtím prokazatelně proklouzlo
  (souběh nad `g_meas_cfg`), ne na plošné opakování.
- `rtc.c` (LSE disciplinace, `rtc_lse_*`) je v rozsahu modulu 24 jako
  „matematický úsek", ale letos už prošla samostatným auditem (modul 17,
  `docs/audit/2026-09-17_cas-alarmy-watchdog.md`) — nekontrolováno znovu.
