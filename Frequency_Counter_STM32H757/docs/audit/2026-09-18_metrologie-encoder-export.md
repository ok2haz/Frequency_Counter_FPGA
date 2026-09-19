# Audit: metrologie, encoder, export (modul 20 — poslední)  (2026-09-18)

- **Commit:** `3a13783` (větev `audit/2026-09-09-hodiny-pwr`), pracovní strom bez změn v kódu
- **Jádro / doména:** CM7. `meas_math.c` se linkuje **i do obrazu CM4** (viz `scpi.c`).
- **Soubory (1 449 ř. vč. hlaviček):** `meas_present.c` (404) + `.h` (162),
  `encoder.c` (153) + `.h` (61), `screenshot.c` (175) + `.h` (28),
  `phase_noise.c` (132) + `.h` (59), `autocal.c` (96) + `.h` (43),
  `meas_math.c` (75) + `.h` (61).
- **Projité sekce checklistu:** D (souběh, RTOS, zásobníky), E (meze, dělení nulou,
  ošetření chyb), G (TIM v encoder módu), okrajově A (hodiny TIM1) a C (SDRAM scratch).
- **Neprojité (a proč):** B (kromě `meas_math.c` nic neběží na CM4 a ten soubor je
  čistá logika bez periferií), F (nic nesahá na Flash přímo — `screenshot` jde přes
  FatFs, auditovaný v modulu 7), H (errata — bez revize silikonu).

## Souhrn

Poslední neauditovaný vlastní kód projektu. Je to **z velké části čistá logika a je
napsaná velmi dobře**: `meas_present.c` má Welfordovu statistiku, guardy na každé
dělení, korektní chování při `NaN` (přes `u_tot_rel > 0.0`) a selftest, který testuje
i neinicializovaný stav filtru a Jensenovu nerovnost (proč perioda potřebuje vlastní
akumulátor); `phase_noise.c` má velké buffery správně `static` s poznámkou o boot-loopu,
který je k tomu donutil; `meas_math.c` je triviální a plně pokrytý testem.

**Verdikt: funkční.** Žádný S1 ani S2. Jediný S3 není porucha běhu, ale **rozpor mezi
dokumentací a skutečností u TIM1** (`encoder.h` slibuje regen-safety, kterou projekt
už nemá — a konfigurace z `.ioc` se přitom nikdy neuplatní). Zbytek jsou S4: dvě
věty v komentářích, které neplatí, a dvě místa, kde stav píše víc úloh, než hlavička
připouští — všechno s malým nebo nulovým dopadem.

---

### F-0122 [S3] `encoder.h` slibuje, že v `.ioc` není nic z encoderu — TIM1 i PA8/PA9 tam jsou, a jejich konfigurace se nikdy neuplatní

- **Místo:** `CM7/Core/Inc/encoder.h:9-10` (tvrzení), `CM7/Core/Src/encoder.c:63-73`
  (ruční konfigurace TIM1), `CM7/Core/Src/main.c:393` (`MX_TIM1_Init()`),
  `CM7/Core/Src/tim.c:30` (generovaná konfigurace), `H757_LED.ioc:1027-1030`, `:461`, `:466`
- **Popis:** Hlavička tvrdí *„Piny si modul konfiguruje SAM (idempotentne) — v `.ioc`
  NENI nic z toho, stejny regen-safe vzor jako CS pin ve `fpga_freq_init`."* Obě
  poloviny jsou dnes nepravdivé: TIM1 **v `.ioc` je** (i s piny), CubeMX generuje
  `MX_TIM1_Init()` a `main()` ho volá. Vznikly tak **dvě konfigurace téže periferie**,
  z nichž ta z `.ioc` nikdy nenabude účinku.
- **Důkaz:**
  - `H757_LED.ioc:1027-1030`: `TIM1.EncoderMode=TIM_ENCODERMODE_TI12`,
    `TIM1.IC1Filter=15`, `TIM1.IC2Filter=15`; `:461` `PA8.Signal=S_TIM1_CH1`,
    `:466` `PA9.Signal=S_TIM1_CH2`; `:36` `CortexM7.IPs=…,TIM1\:I`.
  - `CM7/Core/Src/tim.c:30` `void MX_TIM1_Init(void)` s `htim1`; volá se
    `CM7/Core/Src/main.c:393` — tedy **před schedulerem**.
  - `CM7/Core/Src/encoder.c:65-73` přepisuje `CR1`, `PSC`, `ARR`, `CCMR1`, `CCER`,
    `SMCR`, `CNT` **syrovými zápisy** a teprve tady se nastaví `CEN`.
    `encoder_init()` volá `freertos_task_ui.c:204`, tedy **po** schedulerem → **ruční
    konfigurace vždy vyhraje**.
  - Existuje i commit `2f5c3db` *„fix(encoder): TIM1 IC2Filter dopnen v .ioc, zrcadli
    IC1Filter"* — někdo tedy `.ioc` hodnoty opravoval v domnění, že na nich záleží.
    Nezáleží: `encoder.c:68-69` nastaví `IC1F = IC2F = 15` bez ohledu na `.ioc`.
- **Dopad:** Za běhu **žádný** — encoder funguje, protože ruční konfigurace je
  poslední. Vada je v tom, co to udělá s příští úpravou:
  1. Kdo změní filtr / encoder mód v CubeMX, **nezmění nic** a bude hledat proč.
  2. Kdo se spolehne na větu v hlavičce, bude si myslet, že regen na encoder nesahá —
     přitom regen přepíše `tim.c` i pinu PA8/PA9 v `MX_GPIO_Init`.
  3. `MX_TIM1_Init()` běží v bootovní cestě a jeho výsledek se zahodí.
- **Reprodukce:** Změnit `TIM1.IC1Filter` v CubeMX, regenerovat, přeložit — chování
  encoderu se nezmění (hodnota z `encoder.c` ji přepíše). Staticky doložitelné výše.
- **Návrh opravy:** Rozhodnout, **která** z těch dvou cest je ta pravá, a druhou zrušit:
  - **(a) Ponechat ruční konfiguraci** (dnešní skutečnost) → opravit hlavičku, aby
    nelhala, a do `CUBEMX_CHECKLIST.md` dopsat, že TIM1 v `.ioc` je jen **rezervace
    pinů** a jeho parametry se neuplatňují. Levné, bez rizika, `.ioc` se nedotýká.
  - **(b) Přejít na CubeMX cestu** (`HAL_TIM_Encoder_Init` + `HAL_TIM_Encoder_Start`)
    a ruční zápisy zrušit. Čistší, ale sahá na funkční HW cestu a na `.ioc` —
    pravidlo 6, tedy jen s výslovným souhlasem.
  ⚠️ **Nedělat obojí zároveň** a nenechat to tak, jak to je — dvě pravdy o jedné
  periferii jsou přesně `L-0018`.
- **Riziko opravy:** (a) žádné (jen komentář + checklist); (b) střední — mění se
  inicializace fungující HW cesty a `.ioc`.
- **Vztah k lekcím:** **`L-0028`** (věta v komentáři je testovatelná — tahle neplatí),
  **`L-0018`** (dvě místa konfigurující touž věc), **`L-0038`** (kontrakt firmware ↔ `.ioc`
  nehlídá nikdo). Z opravy vznikla **`L-0062`**.
- **Stav:** **opraveno 2026-09-19** — varianta **(b)**, rozhodnutá uživatelem
  („opravit ioc aby bylo konzistentní"). `.ioc` je jediný vlastník parametrů TIM1.
  - 🔑 **Měřením se ukázalo, že riziko je nižší, než nález odhadoval.** `MX_TIM1_Init()`
    vyrábí **bit za bit tentýž stav registrů** jako ruční zápisy: `Prescaler=0` → `PSC=0`,
    `Period=65535` → `ARR=0xFFFF`, `ENCODERMODE_TI12` → `SMS=011`,
    `IC1Filter=IC2Filter=15` → `IC1F=IC2F=15`, `ICSELECTION_DIRECTTI` → `CC1S=CC2S=01`.
    Přechod tedy **není změna chování**, ne „střední riziko" jak nález psal.
  - **Jediný rozdíl:** `HAL_TIM_Encoder_Start` navíc nastaví `CC1E/CC2E`, které byly
    nulové. Počítání to nemění (encoder mód bere `TI1FP1`/`TI2FP2` přes slave-mode
    controller, `CCxE` jen hradluje zápis do `CCRx`, který nečteme), ale je to **jediná
    věc, kterou je nutné ověřit na HW**: UART `enc`, jedna západka = `kroku=1`.
  - ⚠️ **`.ioc` se nakonec měnit NEMUSELO** — jeho hodnoty už odpovídaly. Nekonzistentní
    byl kód (dvě konfigurace) a hlavička. Uživatel dal souhlas se zásahem do `.ioc`
    (pravidlo 6), ale nebyl potřeba, takže se `.ioc` nedotklo nic.
  - **Konfigurace pinů v `encoder_init()` ZŮSTÁVÁ**, a je to ta podstatná část opravy:
    generovaný `HAL_TIM_Encoder_MspInit()` nastaví PA8/PA9 stejně, ale **bez
    `gpio_cfg_lock()`** — a `GPIOA` sdílí CM4 (ETH: PA1/PA2/PA7). Naivní odstranění
    duplikátu by zrušilo jediný zápis chráněný proti závodu jader, tedy přesně třídu
    vady, která shodila displej (PG8) i síť (PG11).
  - **Přidán guard `htim1.Instance != TIM1`** (vzor **L-0009**): kdyby regen vyhodil TIM1
    z `.ioc`, encoder se nezapne místo startu nenakonfigurovaného timeru.
  - Opraveno i `encoder.h:9-10` (nepravdivá věta) a celá sekce Encoder
    v `CUBEMX_CHECKLIST.md`, jejíž nadpis tvrdil „⬜ NENÍ V IOC".
  - ⬜ **neověřeno na HW** — viz kritérium `CC1E/CC2E` výše.

---

### F-0123 [S4] `bmp_header()` má komentář „sdílí ji obě cesty, aby se formát nemohl rozejít" — USB cesta ji nevolá

- **Místo:** `CM7/Core/Src/screenshot.c:29-43` (`bmp_header` + komentář),
  `:74-84` (`screenshot_emit_bmp` staví hlavičku inline), `:139` (jediný volající)
- **Popis:** Komentář nad `bmp_header()` tvrdí, že ji používají **obě** exportní cesty
  právě proto, aby se formát BMP nemohl rozejít. Ve skutečnosti ji volá **jen** cesta
  na SD kartu; USB cesta si těch deset řádků opisuje znovu.
- **Důkaz:**
  - `:30` *„Sdili ji obe cesty (USB i SD), aby se format nemohl rozejit."*
  - `:139` `bmp_header(hdr, rowbytes * SS_H);` — **jediné** volání v souboru
    (grep mimo `screenshot.c` nenašel žádné další).
  - `:74-84` — `screenshot_emit_bmp()` má vlastní `memset` + `hdr[0]='B'` + sedm `le32`,
    tedy doslovný duplikát těla `bmp_header()`.
  - ✅ Dnes se **neliší**: obě varianty zapisují totéž (`54+imgsize`, 54, 40, `SS_W`,
    `SS_H`, planes 1, 24 bpp, `imgsize`) — ověřeno porovnáním řádek po řádku.
- **Dopad:** Dnes nulový. Riziko je v budoucnu: kdo opraví `bmp_header()` (např. doplní
  rozlišení v px/m nebo změní bpp), bude podle komentáře věřit, že opravil obě cesty —
  a USB export začne produkovat jiný soubor než SD. Že jde o **diagnostický export**,
  to zhoršuje: rozdíl se pozná až u někoho, kdo ten snímek otevře.
- **Reprodukce:** `grep -n "bmp_header" screenshot.c` → definice + **jedno** volání.
- **Návrh opravy:** `screenshot_emit_bmp()` volá `bmp_header(hdr, imgsize)` a inline
  verzi zrušit (~10 řádků dolů). Komentář pak bude pravdivý. Alternativa (nechat
  duplicitu a opravit komentář) je horší — `L-0018` říká slučovat, ne dokumentovat dvě
  pravdy.
- **Riziko opravy:** nízké; obě varianty jsou dnes prokazatelně shodné, takže
  sjednocení nemění výstup. ⚠️ Ověřit, že `imgsize` vs. `filesize` se nepoplete —
  `bmp_header` si `54 +` přičítá **sama**.
- **Vztah k lekcím:** **`L-0028`** (třetí výskyt v projektu), **`L-0018`**.
- **Stav:** otevřeno

---

### F-0124 [S4] Výčet uživatelů SDRAM scratche v `screenshot.c` vynechává `membench` — tedy toho destruktivního

- **Místo:** `CM7/Core/Src/screenshot.c:102-105` (komentář), `:108` (`SS_SCRATCH`)
- **Popis:** Komentář u scratche `0xC0400000` vyjmenovává, kdo tu paměť ještě používá,
  a uvádí **jen** UART `sdram write/read`. Chybí `membench`, který tentýž blok
  **destruktivně přepisuje** pěti vzory a retenčním testem.
- **Důkaz:**
  - `screenshot.c:102-104`: *„Sdílí ho jen UART příkaz `sdram write/read` (ruční
    diagnostika), takže ke kolizi může dojít jen tím, že si uživatel oba příkazy pustí
    zároveň…"*
  - `CM7/Core/Src/membench.c:65`: `#define SDRAM_TEST_ADDR 0xC0400000u`,
    `:66` `SDRAM_TEST_SIZE (512u * 1024u)` — tatáž adresa, destruktivní zápis.
  - `CM7/STM32H757BITX_FLASH.ld:68` to ví lépe než ten komentář:
    *„0xC0400000  4M  MPU R1 WBWA   scratch (membench, screenshot)"*.
- **Dopad:** **Žádný na běh** — a je poctivé to říct: `membench`, `screenshot` i
  `sdram write/read` běží **výhradně z UartTasku** (ověřeno: `freertos_task_uart.c:1519`
  a `:1528` jsou jediní volající screenshotu; UI cesta benchmarku jde přes
  `g_membench_req` → `membench_service()` taktéž v UartTasku), takže serializace, o
  kterou se komentář opírá, platí i pro `membench`. Vada je jen v tom, že **závěr je
  správný z neúplného výčtu** — a příští čtenář, který bude uvažovat o přesunu
  screenshotu jinam, dostane neúplný seznam toho, co si na scratch sahá.
- **Reprodukce:** `grep -rn "0xC0400000" CM7` → tři uživatelé, komentář zná dva.
- **Návrh opravy:** `docs:` — doplnit `membench` do výčtu a zdůraznit, že je
  **destruktivní**; uvést, že serializaci zajišťuje UartTask pro všechny tři.
- **Riziko opravy:** žádné.
- **Vztah k lekcím:** **`L-0022`** (u sdíleného zdroje vyjmenuj **všechny**, kdo ho
  používají — a čím se liší). Týž vzor jako F-0115 v modulu 19.
- **Stav:** otevřeno

---

### F-0125 [S4] `encoder_set_div()` píše stav, který podle hlavičky vlastní UiTask

- **Místo:** `CM7/Core/Src/encoder.c:142-147` (`encoder_set_div`),
  kontrakt `CM7/Core/Inc/encoder.h:12-13`,
  volající `CM7/Core/Src/freertos_task_uart.c:1260` a `CM7/Core/Src/syscfg.c:270`
- **Popis:** Hlavička říká: *„`encoder_poll()` volá VÝHRADNĚ UiTask (~100 Hz smyčka).
  Modul nemá žádný zámek — jeden čtenář stačí."* `encoder_set_div()` ale zapisuje
  `s_div` **a `s_rem`** (akumulátor kroků, který jinak patří výhradně `encoder_poll`),
  a volá se z **UartTasku** i z `syscfg_load()`.
- **Důkaz:**
  - `encoder.c:145-146` — `s_div = d; s_rem = 0;`
  - `encoder.c:96-101` — `encoder_poll` dělá nad `s_rem` read-modify-write
    (`s_rem += d; … s_rem -= det * s_div;`) s body preempce uprostřed.
  - `freertos_task_uart.c:1260` `encoder_set_div((uint8_t)d);` (příkaz `enc div N`),
    `syscfg.c:270` `encoder_set_div(b.enc_div);` (obnova nastavení při bootu).
  - Žádná z proměnných není `volatile` ani chráněná.
- **Dopad:** **Nepatrný.** Nejhorší následek je ztracený nebo pokažený zbytek kroků,
  tedy **jedna miscountovaná západka** v okamžiku, kdy uživatel ručně mění dělič —
  což je operace, po které stejně následuje kalibrační otáčení. `syscfg_load()` běží
  při bootu, kdy UiTask encoder ještě nepolluje. Poškození paměti nehrozí.
  Uvádím to proto, že věta *„modul nemá žádný zámek — jeden čtenář stačí"* je
  **tvrzení o všech volajících**, a to dnes neplatí (`L-0054`).
- **Reprodukce:** `HYPOTÉZA — ověřit:` okno je několik instrukcí, prakticky
  nepozorovatelné. Ze zdrojáku doložitelné (dva zapisovatelé, dvě úlohy).
- **Návrh opravy:** Buď (a) `docs:` — do hlavičky dopsat, že `encoder_set_div()` smí
  volat i jiná úloha a proč je to neškodné (a `s_div` označit `volatile`), nebo
  (b) stejný vzor jako u `g_membench_req` — UART nastaví požadavek, `encoder_poll`
  ho aplikuje. (b) je čistší, ale pro tenhle dopad neúměrné.
- **Riziko opravy:** (a) žádné, (b) nízké.
- **Vztah k lekcím:** **`L-0054`** („jeden vlastník" je tvrzení o VŠECH volajících),
  **`L-0023`**.
- **Stav:** otevřeno

---

### F-0126 [S4] `g_autocal` píšou dvě úlohy a `autocal_format_full()` je dotaz, který mutuje

- **Místo:** `CM7/Core/Src/autocal.c:30-47` (`autocal_run`), `:55-57`
  (`autocal_format_full` volá `autocal_run`), volající `CM7/app/app_gpsdo.c:9244`
  (UiTask) a `CM7/Core/Src/freertos_task_uart.c:1531` (UartTask)
- **Popis:** Globál `g_autocal` (devět polí) zapisuje `autocal_run()`, který volají
  **dvě úlohy**: UiTask (tlačítko AUTO-CAL v okně Kalibrace) a UartTask (příkaz
  `autocal`). Nikde není řečeno, kdo ho vlastní. Navíc `autocal_format_full()` vypadá
  jako formátovací dotaz, ale při `!ran` **spustí celé měření**.
- **Důkaz:**
  - `autocal.c:32-46` — devět samostatných přiřazení do `g_autocal`, bez kritické sekce.
  - `app_gpsdo.c:9244` `autocal_run();` (obsluha tapu, UiTask),
    `freertos_task_uart.c:1531` `autocal_run();` (UartTask).
  - `autocal.c:57` — `if (!g_autocal.ran) autocal_run();` uvnitř `autocal_format_full`.
  - ✅ `autocal_summary()` má `static char s[64]`, ale **jediného volajícího**
    (`app_gpsdo.c:9250`), takže o sdílený buffer nejde; délka nejhoršího řetězce je
    58 B včetně NUL, takže se do 64 B vejde.
- **Dopad:** **Prakticky nulový.** Souběh dvou běhů může dát smíchanou sadu verdiktů,
  jenže oba běhy čtou tentýž `g_sensors[]`, takže se výsledky liší nanejvýš o jeden
  vzorek senzoru. Nic se neukládá ani neřídí — je to zobrazení. Uvádím to kvůli
  konzistenci s tím, jak projekt tuhle třídu jinde řeší, a kvůli tomu, že
  „dotaz, který mutuje" je vzor, na kterém už projekt jednou stál (`L-0023`,
  `datalog_sd_card_present`).
- **Reprodukce:** stisknout AUTO-CAL v okně Kalibrace a současně poslat `autocal`
  přes UART. Rozdíl nebude pozorovatelný.
- **Návrh opravy:** `docs:` — do `autocal.h` dopsat, že `g_autocal` smí přepsat
  kterákoli ze dvou úloh a proč je to neškodné; u `autocal_format_full()` uvést, že
  **spouští měření**, ne jen formátuje. Kód měnit není potřeba.
- **Riziko opravy:** žádné.
- **Vztah k lekcím:** **`L-0023`** (dotaz, který mění stav), **`L-0054`**.
- **Stav:** otevřeno

---

## Co bylo zkontrolováno a je v pořádku

**E — meze, dělení nulou, NaN** (`meas_present.c`, `phase_noise.c`):
- `mp_period_s` (`hz > 0`), `mp_period_sample_s` (`mul/edges/gate_ns != 0` **a** `n > 0`),
  `mp_deviation` (`nominal == 0` guard), `mp_nominal_auto` (`hz <= 0`),
  `mp_fit_solve` (`n < 3`, `dx <= 0 || dx < 1e-30`, `dy > 0` pro `r`) — **každé dělení
  má guard**. ✅
- `mp_budget`: `gate_s <= 0` se přepíše na 1.0; `digits` se počítá jen když
  `u_tot_rel > 0.0` — 🔑 **to zároveň korektně odchytí `NaN`** (každé porovnání s NaN je
  false → větev `else` → `digits = 15`), takže `(int)` konverze nikdy nedostane NaN,
  což by bylo UB (`L-0034`). ✅
- `mp_filt_add` kontroluje `win` **před** použitím (`% f->win`), takže vynulovaný
  (neinicializovaný) stav nedělí nulou — a selftest to explicitně testuje (`:352-353`). ✅
- `pn_compute`: `n < PN_NFFT` → `off = n - PN_NFFT` nikdy nezáporné; smyčka binů je
  ohraničená `max_pts`; `fk > 0` (k ≥ 1); `log10` má guard `sphi > 0.0`. ✅

**D — zásobníky** (`L-0035`, měřeno nad `.elf`, ne odhadem): `pn_compute` alokuje
FFT pole `double re[64]`/`im[64]` = 1 kB na zásobníku a volá ho **UiTask**
(`screen_main.c:1327`). 🔑 **Volající si přitom velké buffery drží `static`**
(`chron[STAT_N]`, `pts[PN_NBINS]`), takže celková hloubka zůstává ~1,3 kB proti
**5 268 B volného zásobníku UiTasku** (změřeno v modulu 11). `mp_filt_add` 132 B,
`encoder_poll` 28 B, `screenshot_emit_bmp` 60 B. ✅
`pn_selftest` má **všechna** pole `static` s poznámkou, proč (boot loop 2026-08-29,
defaultTask 2560 B) — přesně vzor, který `CLAUDE.md` předepisuje. ✅

**D — souběh, který je v pořádku:** `screenshot` sdílí `s_row[2400]` mezi USB a SD
cestou; **obě jdou výhradně z UartTasku** (`freertos_task_uart.c:1519`, `:1528`) a ten
je zpracovává sériově — ověřeno grepem, ne z komentáře. ✅ `screenshot_save_sd` je
**obálka** nad vyčleněným tělem, takže `sd_export_busy_begin/end` drží přes **všech osm**
chybových návratů (přesně jak žádá `L-0022` / F-0026). ✅

**G — TIM1 v encoder módu** (`encoder.c:65-73`) přepočítáno proti RM0399:
`CCMR1` = `CC1S=01` (bity 1:0), `IC1F=15` (7:4), `CC2S=01` (9:8), `IC2F=15` (15:12);
`SMCR = 0x3` = `SMS=011` = encoder mode 3 (obě hrany obou kanálů) — **odpovídá**
`ENC_COUNTS_PER_DETENT = 4`. ✅ Rozdíl čítače se počítá v `uint16` a teprve pak
přetypuje na `int16`, takže přetečení `0xFFFF→0x0000` se ošetří samo. ✅
Piny se konfigurují pod `gpio_cfg_lock()` — správně, PA8/PA9/PC13 leží na portech,
kam sahá i CM4 (třída #219/#208). ✅

**Formátování:** v celém modulu **žádné `%f`/`%e`/`%g`** (nano.specs by nevytiskl nic)
a žádné `fmt_fixed(..., >=4)`. `autocal_summary` skládá nejhorší řetězec na 58 B
do bufferu 64 B. ✅

**Metrologická správnost** (namátkou přepočteno):
- Welford v `mp_stats_add` je kanonický tvar `m2 += d*(x - mean_new)`. ✅
- `mp_budget` sčítá příspěvky **kvadraticky** a selftest to ověřuje trojicí 3-4-5. ✅
- `mp_period_sample_s` počítá periodu **přímo z `gate_ns`/`edges`**, ne přes kmitočet —
  a selftest dokládá, proč to není totéž (Jensenova nerovnost: `mean(1/f) ≠ 1/mean(f)`,
  vektor {1, 3} Hz). 🔑 To je přesně ta třída chyby, kterou projekt zná z `L-0018`. ✅
- `pn_compute` normalizuje jednostranné PSD `2/(fs·Σw²)` a převádí `Sφ = (f0/f)²·Sy`;
  selftest ověřuje **argmax na binu čistého tónu** i to, že zdvojnásobení `f0` zvedne
  `L(f)` o 20·log10(2) ≈ 6,02 dB. ✅
- `meas_limit_eval` má meze **inkluzivní do PASS** a selftest testuje obě hranice. ✅
  ⚠️ Při `lo > hi` vrací vždy LO/HI a nikdy PASS — což je přesně ten „trvalý FAIL"
  z F-0052/F-0096; od `2253ac4` to ošetřuje sanitizace na vstupu, takže tady už
  nález není.

## Nezkontrolováno / omezení tohoto běhu

- **Nic z tohoto modulu neběželo na HW v rámci auditu** — všechny nálezy jsou statické.
  F-0122 je doložitelný z `.ioc` + `tim.c` + `main.c` bez desky.
- **Numerická stabilita `mp_fit_*` při dlouhých řadách** se neposuzovala do hloubky:
  akumulace `sxx`/`sxy` (sumy, ne body) je vědomý kompromis za `O(1)` paměť a při
  velkých `x` (uptime v sekundách) ztrácí `n·Sxx − Sx²` část platných číslic
  katastrofickým krácením. Pro diagnostiku driftu to stačí, ale **kdyby se z prokladu
  měla počítat metrologická hodnota**, patří tam centrovaný tvar (`x − x̄`).
  Není to nález — je to poznámka k budoucímu použití.
- **Přesnost twiddle rekurence ve `pn_fft`** (`cr/ci` se násobí v cyklu místo
  `cos/sin` na každý krok) nebyla kvantifikována; pro `N = 64` je akumulovaná chyba
  pod rozlišením zobrazení.
- **`screenshot` tearing u USB cesty** je známé a v kódu přiznané omezení
  (`screenshot.c:64-67`), ne nález — SD cesta ho řeší kopií do scratche.
