# CLAUDE_ARCHIV — co bylo vyřazeno z CLAUDE.md při redukci 2026-08-29

> **Účel:** bezpečnostní síť. Nic tady není závazné pro současný kód — je to **historie,
> kontext a zdůvodnění**, které se z hlavní poznámky odstranilo kvůli délce. Když v `CLAUDE.md`
> něco chybí a ukáže se to jako potřebné, obnov to odsud (nebo z plné zálohy níže).
>
> **Plná záloha CLAUDE.md před redukcí:**
> - soubor `docs/.CLAUDE.md.pre-trim.bak`
> - git commit `2bd75740` (poslední úprava CLAUDE.md před redukcí, 2026-08-26)
>
> **Kam se přesunul obsah, který NENÍ ztracený:**
> - referenční tabulky (hodiny, DSI/LTDC/TC358762, framebuffer/MPU, SDRAM mapa, FreeRTOS tasky,
>   64B protokol, W25Q region mapa, seznam `s_view`) → `docs/HW_REFERENCE.md`
> - „proč" a pravidla, která z těch sekcí plynou, zůstala v `CLAUDE.md`

---

## 1. Věty „k dohledání v git historii" (odstraněny — git je prohledávatelný)

### UI vrstva — historie
> **Historie:** dřívější ručně psané `gfx.c`/`touch_ui.c` UI i pokus o **LVGL v9** obrazovku
> (`lv_port_disp.c`, `ui_main_screen.c`, vendored `Middlewares/Third_Party/lvgl`) byly
> **odstraněny** a nahrazeny libprim/libui/app. K dohledání v git historii.

### Akcelerace / linker — ITCM/DTCM
> **Pozn.: dřívější ITCM/DTCM sekce (`.itcm_text`/`.dtcm_data`) v linkeru + kopírovací smyčka
> v `main.c` USER CODE 1 byly odstraněny** — využíval je jen smazaný gfx hot-path, po jeho
> odebrání zůstaly prázdné (kopie byla no-op). K dohledání v git historii, kdyby bylo potřeba
> ITCM zrychlení vrátit.

### Animace — zvýraznění změněné číslice v headline
> **Zvýraznění změněné číslice v headline — ODSTRANĚNO 2026-07-25** (na přání uživatele).
> Smazán celý freq-flash mechanismus (`screen_main_freq_flash_tick`, `freq_seg_draw_color`,
> `s_first_frac_seg`, `FREQ_FLASH_FRAMES`), samostatný přepínač `g_digit_anim_enabled`
> (+ globál, BKP_DR6 bit9, syscfg pole `digit_anim_en`, `ANIM_DIGIT_TOGGLE_RECT` tlačítko + touch)
> i persist (magic bump `"SCF5"`→`"SCF6"`). Číslice měřeného kmitočtu se překreslují bez
> jakéhokoli podbarvení. K dohledání v git historii.

### SPI2 / 74HC595
> **POZOR: SPI2 dřív používal `ShiftRegister_SendByte` (74HC595) přes PB12 — odstraněno**,
> PB12 je teď CS k FPGA. V IOC/main.h je PB12 pořád pod starým názvem **`SPI2_RCK`**
> (a PB4 = `SPI2_RES`, nepoužitý). — *(v CLAUDE.md ponechána jen zkrácená verze: pin se v .ioc
> jmenuje `SPI2_RCK`, CS si konfiguruje `fpga_freq_init` sám)*

### Off-screen canvas
> Off-screen canvas API odstraněno (nepoužité). ⚠️ **Freeze NEBYL bufferem** — byl to `printf`
> v SensorsTasku (malý stack); buffer má CPU dopad ~0 % (dirty-rect).
> — *(v CLAUDE.md ponecháno jen: „Freeze nebyl bufferem — byl to printf v malém tasku.")*

---

## 2. „Dřív to bylo jinak" — nahrazeno jen aktuálním stavem

### Footer RUN/STOP
Odstraněno: *„Dřív to bylo obráceně (label = stav), což mátlo."* — aktuální chování (label = AKCE,
červené STOP při běhu / zelené RUN při stopu, podbarvení kmitočtu při STOP) zůstává v CLAUDE.md.

### SEQ-staleness heuristika FPGA
> **Dřívější SEQ-staleness heuristika ODSTRANĚNA** (falešně hlásila stale u nízkých kmitočtů,
> kde se reciproké okno legitimně protáhne) — teď se věří FPGA flagu.
Aktuální stav (věří se `error_flags` bit1 SIGNAL_LOST) zůstává.

### Trend fullscreen — layout
Odstraněno: *„s_layout_old / *_v1 smazán"*, *„A/B větev 2026-08-22 odstraněna — TODO #14"* —
aktuální stav (dvě rozložení HYBRIDNÍ/KLASICKÉ přepínatelná v okně DISPLEJ, KLASICKÉ = zamrzlá
větev) zůstává.

### Pilulky v headeru — vývoj velikosti
Odstraněno: *„30→36→42→46 px"* postupné bumpy. Aktuální: `UI_DIM_PILL_H=46`.
Odstraněno: *„`HDR_PILL_LIMIT` (590, sníženo z 640…)"* → ponecháno jen `HDR_PILL_LIMIT=590` + důvod
(dvouřádkový CPU blok CM7/CM4).

### CAL pilulka
Odstraněno: *„~67 px místo dřívější „CAL 4 min" pilulky (~90 px)"* → ponecháno jen že CAL je
kompaktní ribbon chip, poslední v pořadí, při přetlaku vypadne první.

---

## 3. Datované „HOTOVO / IMPLEMENTOVÁNO" stopy (stav je i tak popsaný; data → git / STATUS.md)

V CLAUDE.md byly desítky vsuvek typu „— HOTOVO 2026-07-20 (v0.4.0)", „IMPLEMENTOVÁNO 2026-08-01,
čeká na HW", „✅ ověřeno na HW 2026-08-15 průchodem `HW_OVERENI_PRUCHOD.md`", „(2026-08-06)",
„(2026-08-26, druhá vlna)" apod. Ty byly **zkráceny** (ponecháno max. „✅ ověřeno HW" nebo
„KÓD HOTOVÝ, NEOVĚŘENO NA HW" tam, kde to mění, jak se k věci chovat). Kompletní datovanou
kroniku nese `STATUS.md` (TODO tabulka s čísly úkolů) a git log.

Dotčené sekce (výběr): Metrologická vrstva, Vývoj bez FPGA desky, HEADLINE = REÁLNÁ DATA (#1),
Datalog, SD karta, Benchmark pamětí, Prahový monitor, okna GRAFY / PREHLED KANALU / MATH-LIMITY /
SELF-SURVEY / SESTAVY / KVALITA GPS / PRŮVODCE KALIBRACÍ, Web rozšíření v12/v13, Barevná schémata,
Animace, reorganizace Nastavení/Menu.

---

## 4. Reorganizace Nastavení/Menu — 1. iterace (2026-08-13), překonaná 2. iterací

Ponechána jen 2. iterace (Nastavení = čistý rozcestník 3×4, Menu = nástroje). 1. iterace níže
je jen pro dohledání, proč některá okna migrovala:

> **⚠️ REORGANIZACE 2026-08-13 (1. iterace): Nastavení = rozcestník konfigurace, Menu = nástroje.**
> Do okna **Nastavení** se z Menu přesunuly **Čas, Alarmy, Kalibrace, Animace** a přibyla
> **Síť** (`s_view=35`). Pravá polovina Nastavení je teď **mřížka 2×5** (`SETNAV_X0/X1=410/600`,
> w=182, y=72/140/208/276/344, h=62 = 7,3 mm — těsně nad projektovým minimem): Vzhled ·
> Jazyk · SIT · CAS · ALARMY · KALIBRACE · ANIMACE · REFERENCE · O PRISTROJI · SESTAVY.
> Levá polovina zůstává přímé ovládání (zvuk/jas/auto-dim). Uvolněné 4 sloty v Menu jsou
> **placeholdery** (`ACT_FREE`) — ⚠️ dotyk na ně **NEdělá `nav_push`**, jinak by se BACK
> zanořoval do prázdna.

Také překonáno: staré `app_gpsdo_render_settings` (s_view=7) dvousloupcové rozvržení s přímými
ovládači — dnes je Nastavení mřížka a ovládače jsou v okně DISPLEJ / ALARMY. Popis s_view=7 v
CLAUDE.md byl zkrácen.

---

## 5. Opakované věty (v CLAUDE.md ponechány 1×, jinde vyškrtnuty)

- *„Každý partial redraw MUSÍ začít clear (fill/blit REPLACE), jinak copy-forward problikává."*
  — v původním textu ~5×. Nově: 1× v ZLATÝCH PRAVIDLECH + 1× v sekci Triple buffering.
- *„RTC registry výhradně z defaultTask."* — bylo ~4×. Nově: ZLATÁ PRAVIDLA + sekce RTC.
- *„Headline + statistiky jsou pořád SIMULACE (#2)."* — bylo u ~8 oken. Nově: ZLATÁ PRAVIDLA +
  u konkrétních míst jen krátce „(sim, viz #2)".
- *„žádný spin > 10 ms v defaultTask/UiTask/FpgaTask"* — bylo ~6×. Nově: ZLATÁ PRAVIDLA +
  sekce IWDG.
- *„blokující práce jen z UartTasku (nemonitorovaný watchdogem)"* — bylo ~5×.

---

## 6. Duplicitní protokolová specifikace

Sekce **„## FPGA strana protokolu"** (64B rámec, tabulka TYPE, STATUS/FLAGS bity, DATA payload
tabulka, škálování, model, formát kmitočtu) v podstatě opakovala obsah sekce „## FPGA čítač
kmitočtu". Kanonická verze je teď v **`docs/HW_REFERENCE.md`**. V `CLAUDE.md` zůstala sekce
„FPGA čítač kmitočtu" (STM strana + chování driveru) a odkaz na HW_REFERENCE.

---

## 7. Web rozšíření v12 / v13 + revize (2026-08-24 .. 26) — plný blow-by-blow

V CLAUDE.md zkráceno na ~20 řádků (load-bearing pasti + odkaz na `WEB_UI_PLAN.md`). Plné znění:

  - **Web rozšíření v12 (2026-08-24) — KÓD HOTOVÝ, NEOVĚŘENO NA HW; kompiluje se
    `-Wall -Wextra` bez varování na obou jádrech.** Šest bodů (#1–#6):
    - **#1 Ovládání MATH/LIMITY/NULL/CAS ze SPA** — nová karta pošle SCPI (`CALC:MATH:M/B/STAT`,
      `CALC:NULL:ACQ`, `CALC:LIM:LOW/UPP/STAT`, `SYST:DATE/TIME`) **týmž `POST /api/scpi`**, co
      konzole. **Žádný nový server kód** — reuse existujícího `set_cfg` (podmínka `web_ctrl_en`
      + Basic Auth). Tlačítka se zamknou při zakázaném ovládání jako segmenty.
    - **#2 mDNS `gpsdo.local`** — **ručně psaný responder v `lwip_app.c`** (vendorovaný lwIP
      `mdns` modul chybí, jen hlavičky). `LWIP_IGMP=1` (lwipopts), `NETIF_FLAG_IGMP` v
      `ethernetif.c`, MAC `PassAllMulticast` + `igmp_joingroup_netif(224.0.0.251)` při link UP,
      UDP 5353 → odpovídá na A-dotaz. ⚠️ **BEST-EFFORT** (jako `gps glonass`): závisí na tom, že
      MAC přijme multicast a IGMP join projde; selže-li cokoli, CM4 běží dál bez mDNS.
    - **#3 SSE push** (`GET /api/stream`) místo 1 Hz pollingu — **držené spojení**, `httpd_min_poll()`
      (z hlavní smyčky CM4, throttle ~20 Hz) posílá `data: <state json>` při novém `seq_meas`.
      ⚠️ **SPA má automatický fallback na 1 Hz poll** (EventSource `onerror`/timeout) → i kdyby
      SSE selhalo, dashboard jede. `HTTPD_MAX_CONN` 3→**5** kvůli drženým spojením.
    - **#4 Alarmy/prahy/selftest** ve `/api/state` (nová karta STAV) — počítadla `g_alarm_*`,
      stav prahů `g_mon_*_bad`, `g_selftest_res`, `sys_level`. Čteno **přímo ze snapshotu**
      (`scpi_src_t` je nemá).
    - **#5 GPS sky plot** (`GET /api/sats`, SPA fetch ~3 s) — polární graf z `snap.gps_sats[24]`.
      ⚠️ `ipc_sat_t` (v `ipc_shared.h`, bez `gps.h`) drží **shodný layout s `gps_sat_t`** — hlídá
      **7 `_Static_assert`** (`offsetof` každého pole + velikost + `IPC_GPS_MAX_SATS==GPS_MAX_SATS`)
      v `ipc.c`, publikace je pak `memcpy`.
    - **#6 Dlouhá historie 24h/7d/30d + CSV** (`GET /api/log?win=…&n=48`) — **datalog vlastní jen
      CM7** (W25Q), takže data tečou přes **nový IPC kanál `ipc_datalog_xfer_t`** (SRAM4, mimo
      snapshot): CM4 zapíše požadavek (from/count/step, decimace) + zvedne `req_gen`; CM7
      `ipc_datalog_service()` (defaultTask, blokující `datalog_read_back`) naplní `rec[]` a nastaví
      `resp_gen`. HTTP odpověď je **ODLOŽENÁ** — dokončí ji `httpd_min_poll` (mode `HCONN_LOG`,
      timeout 2 s). Jen **jeden transfer souběžně** (503 jinak). SPA přepíná graf mezi živým
      oknem (buffer prohlížeče) a datalogem přes `src()`; **CSV export** (tlačítko) uloží
      zobrazená data (bez zpětného lomítka v SPA → `String.fromCharCode(10)`). ⚠️ Datalog
      neukládá 12V/5V/VREF ani MCU/FPGA teploty → v dlouhých oknech ty řady chybí.
    ⚠️ **Rozsah snapshotu vzrostl** (alarmy + 144 B družic) → `HTTPD_BODYBUF_MAX` 1536→**4096**
    (JSON historie ~2,7 kB). Test: doplnit do `HW_OVERENI_PRUCHOD.md`.
  - **Revize webu (2026-08-26)** — nalezené a opravené chyby + rozšíření:
    - 🔴 **Dlouhá okna byla skoro prázdná, když log ještě nemá tolik historie.** Krok decimace
      se počítal jen z požadovaného okna (30 dní / 48 bodů = každý 5400. záznam), takže při
      dvoudenním logu se našly ~4 body. `ipc_datalog_service()` teď krok **zmenší** tak, aby
      vytěžil celou dostupnou historii; SPA pak hlásí **skutečně pokrytý rozsah** z časových
      značek (ne požadované okno) a upozorní „v logu zatím není celých X".
    - 🔴 **Osa X v detailu grafu počítala stáří ve VZORCÍCH a značila je jako sekundy.** V režimu
      datalogu je vzorek `step×10 s`, takže okno 24 h se popsalo jako „48 s". Teď se osa odvozuje
      z časových značek (`DL.t`), v živém okně zůstává vzorek = 1 s.
    - Prázdný datalog → „zapni datalog"; `t_unix == 0` (RTC nesrovnané z GPS) se hlásí zvlášť.
    - Popisky grafů byly v režimu datalogu **lživé** („Zdroj: poll 1 Hz") → teď rozlišují zdroj
      a připomenou, které řady datalog neukládá (12 V/5 V/VREF, MCU/FPGA teploty).
    - **Detail i pro GPS okno** (klik na kartu/sky plot): velký sky plot s elevačními kružnicemi,
      souhrn a **tabulka družic** seřazená podle C/N0. ⚠️ `drawZoom` přepíná `viewBox`/
      `preserveAspectRatio` — sky plot potřebuje **čtvercový** poměr, grafy jsou záměrně roztažené.
    - `/api/state` nově nese **polohu** (`lat`/`lon` z `gps_lat_e7` celočíselně, `alt_m`, `hdop`).
      ⚠️ Formátuje se **z e7 celočíselně**, ne `%f`.
    - ⚠️ **SPA vyrostla 29 → ~62 kB `.rodata`** (CM4 obraz ~195 kB z 1 MB) a `s_hconn` zabírá
      ~25 kB RAM (5 spojení × 4 kB body buffer) → CM4 `.bss` ~83 kB ze 128 kB.
  - **Vylepšení webu (2026-08-26, druhá vlna):**
    - 🔴 **`IPC_F_SIM` — emulovaná data se na webu tvářila jako reálná.** Doplněno: volný bit
      `IPC_F_SIM` ve `flags` (snapshot NEroste → `IPC_VERSION` beze změny), pole `sim_active`
      v `scpi_src_t` (plní **oba** backendy), `DIAG:SIM?` v parseru, `"sim"` v JSON, badge „EMULACE".
    - **Cache SPA (ETag + 304):** ⚠️ **ETag = čas překladu `httpd_min.c`** (`__DATE__ __TIME__`),
      ne verze firmwaru. `Cache-Control: no-cache` (ne `no-store`). Parser umí `If-None-Match`.
    - **Historie měření přeživá F5** (`localStorage`, ukládá se 15 s + na `beforeunload`).
      ⚠️ **Obnoví se jen když je mezera od posledního vzorku < 30 s** (Allan chce rovnoměrné τ0).
    - **Detail i pro karty TEPLOTY a NAPÁJENÍ** + **rozpad po řadách** (`seriesTable`): každý
      senzor/větev vlastní řádek (aktuální/min/max/rozkmit/σ). ⚠️ hlavička = `data-hd`, ne třída.
  - **Min/max OBÁLKA + delší řady (v13, 2026-08-26):**
    - 🔴 **Prostá decimace výkyv MEZI vzorky neukáže.** `ipc_log_rec_t` nese i `freq_min/max_x100000`
      (min/max v bucketu) → SPA je kreslí jako **pásmo** (`envPoints`).
    - ⚠️ Čte se **dávkově**: `IPC_LOG_SCAN_BUDGET` (128) záznamů/tik, `ipc_datalog_service` je
      stavový automat, `resp_gen` až po dokončení.
    - ⚠️ **Nad `IPC_LOG_SCAN_MAX` (20 000) se bucket VZORKUJE** a odpověď to přizná (`resp_full_env`
      → `full_env` → SPA „PODVZOREK"). Strop ~1,6 s čtení.
    - **Delší řady sešitím dávek**: SPA posílá až `DLCHUNKS`=4 požadavky s posunem `from` → až
      192 bodů. Po sobě, ne souběžně (datalog kanál jeden). HTTP timeout 2 → **8 s**.
  - **Grafika (2026-08-26):** headline **zrcadlí displej** (`fmtFreqHtml`); záblesk při NOVÉM
    měření; **koncový bod křivky** = aktuální hodnota. ⚠️ Koncový bod je **HTML overlay, ne SVG**
    (`viewBox` roztažený `preserveAspectRatio: none` → SVG kruh by byl elipsa).

---

## 8. Dvoujádro / IPC — plné původní znění (před 2. redukcí 2026-08-29)

V CLAUDE.md zhuštěno; každý 🔴/⚠️ trap i aktuální chování tam zůstalo, ubrala se jen
narativní historie (per-verze bumpy, „jak jsme se sem dostali", UI rozměry boxů). Plné znění:

✅ **CM4 BĚŽÍ A IPC ROUND-TRIP FUNGUJE — ověřeno na HW 2026-08-14** (`status` → `CM4: alive (IPC heartbeat), stall x0`;
header „4:xx%"). Obousměrný link je tím HW-ověřený: CM4 přijal snapshot (ověřil magic+verzi, jinak by mlčel)
**a** publikuje heartbeat zpět. Option bytes byly správně už z výroby (`BCM4=1`, `BOOT_CM4_ADD0=0x810`),
bank2 flashnutá.
- 🔴🔴 **PODMÍNKA: ŽÁDNÁ AKTIVNÍ LADICÍ SONDA. Testuj CM4 VÝHRADNĚ po čistém power-cyklu bez debug session.**
  Připojený debugger rozbíjí boot handshake CM7↔CM4 (HSEM/`D2CKRDY`) → boot gate CM7 vyprší →
  `g_cm4_absent=1` → „4:off" (vypadá to jako „CM4 nebootuje", ale CM4 je v pořádku). Táž sonda dělá
  **falešné HardFaulty** (`HF@24000000`, `HFSR=0x80000000` = DEBUGEVT: leftover flash breakpoint přes
  FPB). **Postup po každém flashi: Terminate debug session → Remove All Breakpoints → úplný power-cycle**
  (ne NRST). Teprve pak je `status` vypovídající. Platí i pro měření CPU (viz „CPU zátěž NEMĚŘ přes sondu").
- Návrh + revize `NAVRH_ARCHITEKTURA_CM7_CM4.md` §11; bring-up postup `DUALCORE_BRINGUP_CHECKLIST.md`
  (SRAM4 clock, .ioc regen pozor). ⚠️ Ta část o „nutnosti nastavit option bytes" je pro tuhle desku
  **bezpředmětná** — jsou správně (ověřeno čtením přes CubeProgrammer CLI).
- **Sdílená paměť = SRAM4 / D3 `0x38000000`, 64 KB** (`ipc_shared.h`, `g_ipc = *(volatile ipc_shared_t*)IPC_BASE` —
  **shodná adresa pro obě jádra**). Magic „IPC1" + verze + size (CM4 ověří po bootu). ⚠️ **MPU region 2 na
  CM7 = NON-CACHEABLE + SHAREABLE** (`main.c MPU_Config`); bez toho by CM7 cache viděla stará data. CM4 nemá
  D-cache ani MPU pro SRAM4 → ordering visí **jen na `__DMB()`** (shareability se neshoduje — viz bring-up §3).
- **Snapshot CM7→CM4** (seqlock, `seq` liché = zápis): `ipc_publish` **event-driven** (na nové měření
  `seq_meas` NEBO ≥2 Hz heartbeat — 2 Hz fixní by podvzorkoval FPGA ~4 měř/s), **JEN reálná data**
  (`fpga_freq_get_last`/`gps_get`/`g_sensors`/`g_calib`/health). ⚠️ **Statistika sigma/offset/drift ZÁMĚRNĚ
  neplněná** dokud headline = simulace (#2). **Payload dnes nese** (plná instrument-state → CM4 obslouží
  SCPI/web `MEAS:VOLT?`/`SYST:TEMP?`/`MEAS:POW?` **bez `g_sensors`/`g_calib`**, které na CM4 nejsou): teploty
  OCXO/deska/MCU/FPGA, napětí 12V/5V/VREF/VBAT/Vc, RF mV + AD8307 kalibrace, Si5356, kanál, `sens_valid` maska,
  Math/limit cfg mirror, `ui_cfg` (brána/kanál/RUN), ETH stav (link/IP/PHY), alarmy/prahy/selftest, GPS družice
  `gps_sats[24]` a datalog transfer kanál `ipc_datalog_xfer_t` (dlouhá historie 24h/7d/30d + CSV).
  **`IPC_VERSION` = 13** (aktuální layout v `ipc_shared.h`; historie bumpů v2→v13 v gitu).
  **⚠️⚠️ Při změně `IPC_VERSION` se MUSÍ přeflashnout OBĚ banky.**
  - **⚠️ Nesoulad bank je prakticky NEVIDITELNÝ — `4:--` to NENÍ.** CM4 při neshodě jen přestane
    přijímat snapshot (`s_ready=0`), ale **heartbeat volá dál a bez podmínky** → `ipc_cm4_alive()`
    (magic + růst heartbeatu) hlásí živou CM4 a header svítí `4:xx%`, jako by bylo vše v pořádku
    (jediný příznak: LED_2 nereaguje na GPS fix). **`cm4_ipc_version`** (razítkováno v každém
    heartbeatu, přežije samostatný reset CM7 + `memset` v `ipc_init`) to zviditelní: System Health
    `CM4:IPCv<x>!=<y>` červeně + UART `status` → `⚠ IPC NESOULAD`. ⚠️ **Detekce funguje jen dokud se
    nemění layout PŘED `cm4` blokem** (`snap`/`cmd`/`resp`) — pak si jádra přestanou rozumět už
    v adrese (týká se to bumpů, kde snapshot roste, jako v12 → nutno flashnout obě banky naslepo).
  - **Předletová pojistka:** `scripts/build.sh` varuje, když je některý obraz **starší než
    `ipc_shared.h`** (typicky „přeložil jsem jen jedno jádro") — ověřeno, že varování skutečně padne.
  - **System Health řádek CM4 (v6):** do boxu 156 px se při mono_16 vejde **15 znaků**, takže
    „CM4:OK NET:down" je přesně na doraz a **nic dalšího připojit nelze**. Ukazuje se proto vždy ten
    údaj, který v daném stavu něco říká: dokud link nejede (tj. do lwIP ve F5) je „NET:down" konstanta
    bez informace, kdežto **`CM4:OK PHY:C131`** je živý důkaz, že CM4 mluví s LAN8742A přes MDIO
    (= kritérium F3). Jakmile link naběhne, přebírá `NET:UP`; `ETH:--` = init na CM4 neprošel (amber).
    Totéž podrobněji v UART `status` → `ETH(CM4): init OK, PHY ID 0x0007C131 (LAN8742A)`.
- **`sens_valid` (v4, 2026-08-13):** maska platnosti hodnot ve snapshotu, **bitové pozice ZÁMĚRNĚ shodné
  se `SCPI_V_*`** → CM4 backend udělá `src->valid = snap.sens_valid` a chová se bit za bit jako CM7 na USB.
  Shodu hlídá 14 `_Static_assert` v `ipc.c` (+ 8 dalších pro `SCPI_CFG_*` vs `IPC_CFG_*`) — ty dvě hlavičky
  se jinak nepotkají v jedné translation unit, takže rozejití by nikdo nechytil. Do v3 se **neplatná napětí
  publikovala jako 0** (nerozeznatelné od skutečné nuly) a **neplatné teploty jako poslední dobrá hodnota**
  bez příznaku → `MEAS:VOLT?` by přes USB vrátilo `9.91E37` a přes TCP nulu = dvě různé pravdy o tomtéž
  přístroji. Hodnota se teď publikuje vždy (poslední dobrá se hodí pro trendy), ale **bez bitu se nesmí
  servírovat jako měření**. `t_fpga_c100` do v3 ve snapshotu vůbec nebylo → `SYST:TEMP? FPGA` nešlo na CM4 zodpovědět.
  Seqlock/ring helpery jsou **parametrizované ukazatelem** (`ipc_snap_wr/rd_*`,
  `ipc_ring_cmd/resp_*`) + `g_ipc`-vázané zkratky → `ipc_selftest` běží nad **lokální** instancí (žádný race s publisherem).
- **CM4 konzument** (`CM4/Core/Src/ipc_cm4.c`): `ipc_cm4_read` (seqlock, **bounded retry ≤8** — CM4 se nesmí
  zaseknout, + **per-read kontrola magicu** kvůli neseqlocknutému memsetu v `ipc_init` při bootu),
  **`ipc_cm4_cm7_alive(now_ms)`** (sleduje růst snapshot `seq` → **CM4 nedůvěřuje starým datům při zamrzlém CM7**,
  §11.4/§11.9 — jinak by servíroval stará data jako živá), `ipc_cm4_heartbeat` (živost → CM7). Zapojeno v CM4
  `main.c` smyčce; **LED_2 svítí při GPS fixu ze snapshotu** (jen když CM7 žije) = viditelný důkaz round-tripu.
  ⚠️ **`ipc_shared.h` sdílen RELATIVNÍM include** `"../../../CM7/Core/Inc/ipc_shared.h"` (CM4/CM7 sourozenci → regen-safe,
  žádná změna include path).
- **CM7 čte CM4 heartbeat:** defaultTask `ipc_cm4_alive()` → `g_cm4_alive` + `ipc_cm4_cpu_pct()` →
  `g_cm4_cpu_pct` → **CPU blok headeru** (2026-08-14): **`4:xx%`** = živý, s **reálnou zátěží CM4**
  (CM4 si ji měří sám přes DWT — busy/total cykly za ~1 s okno, clock-agnosticky, publikuje v heartbeatu;
  dnes ~0 %, protože CM4 skoro nic nedělá — naskočí s ETH/SCPI) / `4:--` (D2 ready, IPC ticho) /
  `4:off` (nenabootoval). **stall:CM4:** hrana alive→dead →
  log `stall:CM4` + `g_cm4_stall_count` (UART `status`). ⚠️ **CM7 se kvůli mrtvému CM4 NERESETUJE** (§11.4) a
  **NESAHÁ na crash black-box** (ten = příčina resetu CM7); CM4 se zotaví vlastním IWDG2.
- **Boot gate** (`main.c` Boot_Mode_Sequence_1/2, HSEM + `RCC_FLAG_D2CKRDY`): timeout → `g_cm4_absent=1`,
  **NEspadne do Error_Handler** (degradovaný běh, UART `[BOOT] CM4 nenabehl`). VTOR z auto-remapu
  (`USER_VECT_TAB_ADDRESS` zakomentovaný) → boot řídí **option bytes**.
- **D2 SRAM split** (linkery, regen-safe): **SRAM1 128K → CM7** (`RAM_D2 @0x30000000/128K`; CM7 do D2 nic
  nelinkuje, jen diagnostický `ram write/read`), **SRAM2 128K → CM4** (`RAM @0x10020000/128K`, CM4-alias)
  a **SRAM3 32K → ETH DMA** (`ETH_D2 @0x30040000/32K`, sekce `.eth_dma`).
  ⚠️ Kolize „obě jádra celý D2" byla latentní do ETH; teď disjunktní.
  - ⚠️ **CM4 `RAM` zkrácena 160K → 128K (2026-08-22, ETH F3).** SRAM3 se vyčlenila deskriptorům; kdyby
    `RAM` dál sahala přes SRAM3, linker by tam umístil `.bss`/`.data` a **tiše přepsal ETH deskriptory**
    (stejná fyzická paměť, jen jiná adresa → o kolizi neví). CM4 zabírá ~15 KB flash / ~4 KB RAM.
  - ⚠️ **`ETH_D2` je SYSTÉMOVÁ adresa `0x30040000`, ne CM4 alias `0x10040000`.** ETH DMA je AHB master
    a D2 SRAM vidí na `0x30xxxxxx`; na CM4-only alias nedosáhne. CM4 CPU na systémovou adresu dosáhne
    taky, takže stačí jedna adresa pro obě strany. Bez vlastní sekce skončily `.RxDescripSection`/
    `.TxDescripSection` z generovaného `eth.c` jako **orphan v `.data` na `0x10020010`** (a ještě se
    tahaly z flash). Kontrola: `nm H757_LED_CM4.elf | grep DscrTab` → musí být **`30040000 B`**.
    CM4 nemá D-cache → žádná cache maintenance kolem deskriptorů (výhoda oproti ETH na CM7).
- **Ethernet + lwIP na CM4 (F5, 2026-08-22 — KÓD HOTOVÝ, NEOVĚŘENO NA HW).** `NO_SYS=1`
  (bare-metal raw API, žádný RTOS na CM4) + **DHCP klient**. lwIP zaveden **ručně** z
  `STM32Cube_FW_H7_V1.13.0` (v `.ioc` LWIP **není**): `Middlewares/Third_Party/LwIP`
  (`src/core`, `core/ipv4`, `netif/ethernet.c`) + `Drivers/BSP/Components/lan8742`; glue
  `CM4/LWIP/Target/ethernetif.c` (adaptovaný ST `LwIP_HTTP_Server_Raw`) a
  `CM4/LWIP/App/lwip_app.c` (`lwip_app_init/process`). ⚠️ Jméno `lwip_app.c` (ne `lwip.c`)
  je zvolené tak, aby budoucí CubeMX „Generate Code" s LWIP nekolidoval.
  - 🔴🔴 **`LWIP_RAM_HEAP_POINTER` MUSÍ ZŮSTAT NEDEFINOVANÝ — nevracet pevnou adresu.**
    ST příklady mají `#define LWIP_RAM_HEAP_POINTER (0x30004000)`, protože na **jednojádrovém**
    H7 je D2 SRAM1 volná. Tady SRAM1 podle rozdělení D2 patří **CM7**, takže halda lwIP ležela
    v cizí paměti a cokoli, co do SRAM1 zapsalo (`membench` cíl „SRAM1 D2", UART `ram write`),
    **shodilo CM4 natrvalo** (IWDG2 je vypnutý). Bez toho define si lwIP alokuje `ram_heap`
    jako statické pole v `.bss` CM4 → SRAM2, tedy do vlastní paměti. Kontrola:
    `nm H757_LED_CM4.elf | grep ram_heap` → musí být **`1002xxxx`**, ne `3000xxxx`.
    Cena: `.bss` CM4 +14 kB (MEM_SIZE) → ~65 kB ze 128 kB, pořád velká rezerva.
  - ⚠️ **`ethernetif.c` NESMÍ duplikovat generovaný `eth.c`:** deskriptory, `EthHandle`
    i `HAL_ETH_MspInit` z ST příkladu jsou odstraněné, používá se `heth` a `low_level_init`
    ETH **znovu neinicializuje** (jen čte `heth.gState`) → `eth.c` zůstává netknutý regenerací.
  - ⚠️ **MAC se bere z `heth.Init.MACAddr`, ne z `ETH_MAC_ADDR*`** — HW filtr programuje
    `MX_ETH_Init` (00:80:E1:…), zatímco makra v `hal_conf` nesou 02:00:…; při rozejití by
    ARP odpovídal špatnou adresou a spojení by tiše nefungovalo.
  - ⚠️ **`ETH_RX_BUFFER_SIZE` musí = `heth.Init.RxBuffLen` (1536)** — ST příklad má 1000,
    DMA by psala za konec bufferu.
  - ⚠️ **Žádná cache maintenance** (`SCB_InvalidateDCache_by_Addr` odstraněno): CM4 nemá
    D-cache a CMSIS ji pro něj ani nedefinuje. Přesně proto ETH patří na CM4.
  - ⚠️ **Smyčka CM4 je rozdělená na rychlou a pomalou část.** Dřív končila `HAL_Delay(800)`
    → lwIP obsloužen 1×/800 ms, RX ring (4 deskriptory) by přetekl a ping měl RTT ~1 s.
    Teď: `lwip_app_process()` + `iwdg2_kick()` **každou iteraci (~1 ms)**, IPC snapshot +
    heartbeat + ETH stav na **5 Hz**, LED_2 stavovým automatem místo blokujících delayů.
  - **DHCP startuje/zastavuje link callback**, ne natvrdo — jinak by klient posílal DISCOVER
    do odpojeného kabelu a po zapojení čekal na svůj backoff (lwIP ho zvedá na desítky s).
  - **Paměť:** RAM 30 KB/128 KB (SRAM2), `.eth_dma` 12,7 KB/32 KB (SRAM3: deskriptory +
    zero-copy RX pool 8×1568 B). CM4 obraz 12,9 → **84,6 KB**.
  - ⚠️ **Statická IP zatím NEJDE** — okno SÍŤ (s_view=35) ji ukládá do syscfg, ale **IPC
    snapshot ta pole nenese**, takže se k nim CM4 nedostane. Vždy jede DHCP.
- **cmd ring (CM4→CM7) + config sync (v3):** `IPC_CMD_CFG_SET` (key `IPC_CFG_*` + `arg`/`double argd`) → CM7
  `ipc_service` aplikuje Math/limity na `g_meas_cfg` (`ipc_cfg_apply`; commit jen při reálné změně, kritická
  sekce). Čtení zpět = **cfg mirror ve snapshotu** (`math_m/b/null_ref/lim_lo/hi` + flagy) → CM4 obslouží `CALC:`
  readbacky + `CALC:DATA?/LIM?` bez `g_meas_cfg`. NULL:ACQ používá reálný FPGA kmitočet.
  ⚠️ `ipc_cmd_t` rozšířen o `key`+`double argd` (aby `lo/hi/m/b` nesly plný rozsah, ne jen uint32) → `IPC_VERSION` 3.
  Ostatní příkazy (GATE/RUN/CHAN/LOG) dozrají se SCPI/web na CM4 (dnes status=1).
- **SCPI je DATA-SOURCE nezávislé** (`scpi.c/h`, 2026-08-10): parser+handlery čtou z `scpi_src_t` (instrument-state
  + validity bity `SCPI_V_*` + akce `set_cfg`/`read_log`), NE z globálů. **CM7 backend** `scpi_src_load_cm7`
  (`#if CORE_CM7`) plní z `g_sensors`/`gps_get`/`fpga_freq`/`g_calib`/`g_meas_cfg`/datalog; `scpi_process` =
  wrapper (USB volající beze změny).
  - **SCPI/web na CM4 (W2–W5, plán W0–W5 dokončen; historie v git + `WEB_UI_PLAN.md`).**
    `scpi.c` + `meas_math.c` (fyzicky v `CM7/Core/Src/`) se linkují i do CM4 obrazu (explicitní
    `subdir.mk` v `CM4/Debug/SCPI/`, `-I CM7/Core/Inc`); běží tam SKUTEČNĚ, ne jen se překládají —
    CM4 pustí `scpi_selftest()`/`httpd_min_selftest()` za bootu a výsledek hlásí přes IPC (v7/v9)
    do UART `status` (CM4 nemá konzoli → IPC je jediný kanál na ověření, jako PHY ID u F3).
    ⚠️ **Na CM4 vrací `DISPlay:*` a `SYST:DATE/TIME` SCPI-99 `-241 "Hardware missing"`** (`#else`
    větev mimo `#if CORE_CM7`) — displej i RTC registry jsou fyzicky jen na CM7 a IPC cestu nemají
    ani mít nemají (vzhled/jas nejsou „přístroj", `WEB_UI_PLAN.md` 1.6). Bez té větve build CM4 spadl.
    - **Sdílený `ipc_scpi.c` (OBĚ jádra):** `ipc_scpi_src_from_snap()` (snapshot → `scpi_src_t`, čistá)
      + `ipc_scpi_set_cfg()` (SET → `IPC_CMD_CFG_SET` do cmd ringu). ⚠️ **Žádné `osDelay`/`HAL_Delay`
      v tomto souboru** (jádrově neutrální — nečeká na odpověď).
    - **TCP 5025 (`scpi_tcp.c`)** raw lwIP, pool 4 spojení (bez mallocu). ⚠️ `scpi_src_t` se plní
      **živě pro každý příkaz** (klient pošle druhý o minuty později), ne jednou při připojení.
    - **HTTP port 80 (`httpd_min.c`)** vlastní HTTP/1.1 (ne vendorovaný lwIP `httpd`/`fs.c` —
      `makefsdata` by přidal build krok, stejný důvod jako hand-rolled SCPI). ⚠️ **`GET /api/state`
      staví JSON přes `scpi_src_t`, NE přímo ze snapshotu** → validita (`SCPI_V_*`→`null`) je táž
      logika jako SCPI, ne druhá kopie. Čísla bez `%f` přes `fmt_scpi_hz_d` (sdíleno se `scpi.c`).
    - **W0 vypínač ovládání = `web_ctrl_en` (IPC v8):** `src.set_cfg = web_ctrl_en ? ipc_scpi_set_cfg : NULL`
      → zakázané ovládání spadne do **existující** NULL-guard ochrany parseru (`-230`), žádná nová cesta.
    - **Basic Auth (IPC v10, `web_user`/`web_pass`):** `POST /api/scpi` vyžaduje `web_ctrl_en` **A** platné
      jméno/heslo (`check_auth`, bajtové porovnání, prázdné heslo nikdy neprojde). ⚠️ **TCP 5025 Auth nemá**
      (VISA raw socket nezná HTTP hlavičky) → spoléhá jen na `web_ctrl_en`.
    - 🔴 **SPA (`SPA_HTML[]` v `.rodata`, `GET /`) — pravidla pro editaci** (aby zůstal C řetězcový literál
      bez build kroku): **žádná dvojitá uvozovka ani zpětné lomítko uvnitř** → v JS **žádné regexy**
      (oddělovač tisíců/`trim` ručně); atributy z `innerHTML` **bez uvozovek** (`class=cell`) → víchodnotový
      stav přes `data-` atributy (`data-st=bad`) + CSS selektor; interaktivita `addEventListener`+`data-`,
      ne `onclick=`. ⚠️ **Barvy křivek = CSS proměnné `--c0..--c3` nastavené TŘÍDOU** (`class='ln s0'`),
      ne literální `stroke=` (jinak se graf při změně palety nepřebarví). ⚠️ **`spec(kind)` = jeden zdroj
      pravdy** grafu (série+měřítko+formát) pro náhled i detail. 🔴 **Asynchronní odesílání** (`pump_send`
      + `tcp_sent`): tělo JSON/SCPI **vždy v connection-owned `c->bodybuf`**, NE ve sdíleném scratchi
      (souběžná spojení by si přepsala tělo); SPA stránka je `.rodata` konstanta (sdílet bezpečné).
    - **Grafy staví klient z vlastního pollingu** (buffer max 3600 vzorků = 1 h @1 Hz): kmitočet jako
      odchylka od průměru okna, napájení jako % od nominálu (jinak 12 V zploští zbytek). ⚠️ **Historie
      byla jen v prohlížeči** (F5 ji zahodí) — dlouhá historie z datalogu přibyla ve v12 (#6, níže).
    - ⚠️ **SVG:** `viewBox='0 0 100 100'` + `preserveAspectRatio='none'` (souřadnice = %), popisky os
      jako HTML overlay (ne `<text>`, roztáhl by se), body přes `setAttribute('points',…)`.
    - **`rf_dbm`, ALLAN, DRIFT počítá klient z REÁLNÝCH měření.** 🔴 `sigma_tau`/`offset`/`drift` ze
      snapshotu se **záměrně nepoužívají** (CM7 je neplní — zdroj je simulace headline, `ipc.c`);
      servírovat je jako měření by porušilo zlaté pravidlo. Vzorky se berou **podle `seq_meas`** (jinak
      by 1 Hz poll započítal tentýž výsledek víckrát → σy nesmyslně NÍZKÁ) a buffer se **zahodí při změně
      brány** (Allan chce rovnoměrné τ0). Overlapping ADEV z fází `x[k+1]=x[k]+y[k]·τ0`, τ0 = skutečný
      rozestup měření; drift = lineární proklad s korelací r (|r|<0,5 → neprůkazný, nekreslí se). ⚠️ **Pozor
      na rozdíl proti displeji:** headline na displeji je pořád simulace (STATUS #2), web servíruje reálná
      FPGA data → bez FPGA desky displej ukazuje kmitočet a **web správně `null`** (ne chyba webu).
      Když kmitočet chybí, SPA **vypíše důvod** (STOP / SPI DOWN / ztráta signálu) místo prázdna.
- **Rozšíření 2026-08-13 (jen nad poli, která `scpi_src_t` UŽ má → CM7 i budoucí CM4 se chovají
  IDENTICKY, bez bumpu `IPC_VERSION`):** `SYST:CAP?`, **`SYST:ERR:ALL?`** (vyprázdní celou frontu
  jedním dotazem; ⚠️ po výpisu chyb **nepřipojuje** koncovou `0,"No error"` — ta se vrací jen
  u prázdné fronty), **`STAT:PRES`** (fakticky no-op, protože OPER/QUES *enable* registry nemáme —
  ale MUSÍ se přijmout, jinak inicializační `*RST;*CLS;STAT:PRES` z VISA/IVI ovladače skončí
  chybou a zaplní frontu), `SENS:FUNC?` → `"FREQ"`, `SENS:ROSC:SOUR?` → `INT`, **`SENS:ROSC:LOCK?`**
  (⚠️ hodnotí jen LOS_CLKIN bit3 + PLL_LOL bit4 — **bit2 LOS_XTAL je na této desce trvale 1**),
  **`MEAS:PER?`** (perioda = 1/f), `MEAS:FREQ:STAL?` (1 = měření nedůvěryhodné),
  **`SYST:TEMP:ALL?`** + **`MEAS:VOLT:ALL?`** (agregáty = 1 round-trip místo 4/5 — na TCP to bude znát).
  ⚠️ **Perioda má 15 des. míst (femtosekundy):** při 1,4 GHz je perioda 714 ps, takže i pikosekundový
  krok by byl 0,14 % — pro čítač nepoužitelné. Zlomek se tiskne **po dvou 32bitových půlkách**
  (`%07lu%08lu`), protože 15 cifer se do `unsigned long` nevejde a newlib-nano neumí `%llu`.
- **SET příkazy (2026-08-15) — SCPI přestalo být read-only.** Nově `SENS:FREQ:GATE <s>`
  (presety 0,1/1/10/100 s, tolerance 1 %), `SENS:FREQ:CHAN <0|1>`, **`INIT`/`INIT:IMM`**
  (RUN), **`ABOR`** (STOP), **`READ?`** (= INIT + FETCh, jednořádkové měření pro VISA/IVI)
  a `INIT:CONT?` readback. ⚠️ **Vláknový most:** stav měření (`st` v `screen_main.c`) vlastní
  UiTask, SCPI běží v UartTasku → SET zapíše jen **požadavek** (`g_ui_cfg_req` +
  `g_ui_cfg_req_pend`, kódování jako `g_ui_cfg`) a UiTask ho aplikuje v `app_gpsdo_tick_clock`
  přes `screen_main_apply_cfg_req()` (překreslí footer + zónu čísla, persist do BKP).
  Aplikace běží **před** `s_view` guardem, aby příkaz nezmizel, když je uživatel v jiném okně.
  ⚠️ `SENS:FREQ:GATE?`/`CHAN?` vracejí **nastavenou** hodnotu (SCPI set/readback kontrakt);
  skutečně změřené okno z FPGA rámce je nově na **`SENS:FREQ:GATE:ACTual?`**.
- **`SYST:DATE`/`SYST:TIME` (2026-08-15):** dotaz čte `g_rtc_text`, SET jde přes **request-most**
  (`g_rtc_set_*` + `g_rtc_set_pend`) — RTC registry vlastní výhradně defaultTask, který požadavek
  aplikuje v `rtc_app_tick` **před** `rtc_try_sync()`, takže při GPS fixu má poslední slovo GPS.
  ⚠️ Ručně zadaný čas **nenastavuje** `s_synced` ani BKP magic → UI dál správně hlásí „no GPS".
  Smysl to má jen bez antény.
  Klíče `SCPI_CFG_GATE/CHAN/RUN` mají protějšky `IPC_CFG_*` (hlídají `_Static_assert`);
  rozšíření výčtu nemění layout → `IPC_VERSION` beze změny.
  - ✅ **W1 HOTOVO (2026-08-23), ověřeno na HW přes web:** `ipc_ui_cfg_apply()` (v `ipc.c`, oddělená
    od čistého `ipc_cfg_apply` pro Math/limity) napojuje `IPC_CFG_GATE/CHAN/RUN` na **tentýž most**
    `g_ui_cfg_req`+`g_ui_cfg_req_pend` → `screen_main_apply_cfg_req()` v UiTasku, jaký používá SCPI
    přes USB — včetně identického bitového balení. RUN/STOP, brána i kanál z CM4 tedy fungují
    (`IPC_CMD_LOG` → `datalog_set_enabled`).
  - 🔴 **⚠️ PAST, na kterou se přišlo až HW testem přes web (2026-08-23) — SLEPÝ READBACK.**
    Zápis fungoval, ale **`SENS:FREQ:GATE?` / `CHAN?` přes TCP/HTTP vracely pořád `0.1` a `0`**,
    takže to navenek vypadalo jako „brána a vstup nejdou nastavit". Příčina: snapshot nesl jen
    `channel_id`/`gate_ns` = **co ohlásil FPGA rámec** (a při mrtvém SPI linku jsou nulové), ale
    **nenesl NASTAVENÍ** (`g_ui_cfg`). `ipc_scpi_src_from_snap` proto `set_gate_idx` neplnil vůbec
    (zůstal 0 z `memset`) a `set_chan` bral z `channel_id`. USB cesta byla přitom správně
    (`scpi_src_load_cm7` dekóduje tytéž bity z `g_ui_cfg`) → **dvě různé pravdy o tomtéž přístroji**,
    přesně to, čemu má `sens_valid` + `_Static_assert` disciplína bránit. Opraveno v **IPC v11**:
    snapshot má `ui_cfg` (bývalý `_pad_s` → velikost beze změny) a obě jádra ho dekódují stejně.
    **Poučení: readback musí číst NASTAVENÍ, ne poslední naměřenou hodnotu** — jinak se chyba
    projeví teprve tehdy, když měření neběží, a vypadá jako porucha zápisu.

---

## 9. Benchmark pamětí — plné původní znění (před 2. redukcí 2026-08-29)

V CLAUDE.md zhuštěno na ~30 řádků (všechny 🔴 pasti zachovány). Plné znění:

### Benchmark pamětí (`membench.c/h`, okno PAMETI s_view=43, UART `membench`) — 2026-08-23
Rychlost zápisu/čtení **a hlavně hledání chybných bitů** napříč všemi paměťmi. Vstup:
dlaždice **PAMETI >** v Nastavení (obsadila poslední volnou buňku mřížky 3×4) → tlačítko
**BENCHMARK**; nebo UART `membench` (vypíše tabulku).

⚠️ **Sloupec „testovano" je `testovaný blok / kapacita čipu`, ne kapacita.** Testovat jde
jen to, co nikdo nepoužívá, a ten rozdíl je často řádový — u SDRAM **4 MB z 32 MB** (zbytek
drží framebuffery a linker sekce `.sdram`), u W25Q **32 kB z 64 MB**. Původně sloupec ukazoval
jen testovaný blok pod hlavičkou „velikost" a četlo se to jako kapacita paměti (nahlášeno
při HW testu 2026-08-23) — proto se od té doby zobrazují **obě** čísla.

**Co se testuje a proč právě to** (6 cílů): **DTCM** `0x20000000` 64 kB ze 128 kB (linker sem
nic neumisťuje — ověřeno v mapfile; TCM se z principu necachuje), **AXI SRAM** vlastní statický
32 kB buffer z 512 kB (jediný cíl bez volné oblasti — je tam `.bss`/haldy), **SRAM1 D2**
`0x30001000` 64 kB ze 128 kB (CM7 do D2 nic nelinkuje; CM4 sedí v SRAM2 `0x30020000` a SRAM3
`0x30040000` — ověřeno v CM4 mapfile, žádný překryv), **SDRAM** `0xC0400000` **512 kB z 32 MB**
(scratch v MPU region 1, sdílený s UART `sdram write/read` a se `screenshot`), **interní FLASH
bank1** `0x08000000` 256 kB z 1 MB **jen čtení**, **W25Q QSPI** `W25Q_BENCH_BASE` 32 kB ze 64 MB.
- **SRAM4 / D3 se netestuje.** Do 2026-08-23 se testovala její „volná" horní půlka
  (`0x38008000`, nad `sizeof(ipc_shared_t)`). Odebrána proto, že SRAM4 je paměť, ve které **žije
  mezijaderné spojení**; hnát do ní sekundy provozu je proti pravidlu modulu („testuje se
  výhradně paměť, kterou nikdo jiný nepoužívá") — argument „horní půlka je volná" platí
  o **adresách**, ne o sběrnici. Přínos 16 kB je proti riziku nulový.
- 🔴🔴 **SRAM1 „volná" NEBYLA — poučení pro přidání dalšího cíle (vyřešeno HW 2026-08-23).**
  `membench` shazoval CM4, protože halda lwIP ležela na pevné adrese `0x30004000` (SRAM1 =
  CM7 podle rozdělení D2), zděděné z `LWIP_RAM_HEAP_POINTER` v `lwipopts.h`. Benchmark ji
  přepsal → CM4 spadla a s vypnutým IWDG2 už nenaběhla. Opraveno smazáním toho define
  (lwIP `ram_heap` teď v `.bss` CM4 = SRAM2). **Pravidlo: „linker sem nic neumisťuje" NENÍ důkaz
  volné paměti** — pevně zadrátovaná adresa v middlewaru se v mapfile neprojeví; při přidání cíle
  grepni i **absolutní adresy ve zdrojích obou jader**. ⚠️ Týž problém má i UART `ram write`
  (80 kB od `0x30001000`). ⚠️ **Přiřazení viníka:** `g_cm4_alive` má ~3 s okno → smrt CM4 spadne
  na zrovna běžící (poslední/nejdelší) cíl → mylně obviní W25Q. Viníka určuj čtením **syrového
  `g_ipc.cm4.heartbeat`** (5×/s) a označením **jen prvního** postiženého cíle.
- **`__HAL_RCC_C1_D2SRAM1_CLK_ENABLE()`, ne společná varianta** — na dvoujádrovém H7 má každé
  jádro vlastní sadu povolovacích bitů (`RCC_C1->AHB2ENR` vs `RCC->AHB2ENR`) a nás zajímá jen
  přidělení pro CM7.
- ⚠️ **Během SDRAM fáze může displej krátce trhat** — LTDC čte framebuffer z téhož čipu přes
  tentýž FMC. Při 4 MB se obraz **rozsypal** (~46 MB/s potřebuje LTDC) → blok zmenšen na 512 kB
  (~0,4 s) a ustupuje se scheduleru po 32 kB.
- ⚠️ **Interní FLASH se ZÁMĚRNĚ nikdy nezapisuje** — erase/write do banky, ze které se
  zároveň vykonává kód, zastaví sběrnici; druhá banka patří CM4. Místo bitových chyb se
  obraz přečte **dvakrát s invalidovanou cache** a součty se porovnají → to odhalí
  **nestabilní čtení**, ne trvale špatný bit (trvalou vadu na H7 hlásí ECC flash sama).
- **Vzory** (`pat_word`, čistá funkce indexu — verify si hodnotu **dopočítá znovu**): `0x00`/`0xFF`
  (stuck-at bit), `55/AA` (zkrat mezi sousedními datovými linkami), **adresa v adrese**
  (chyba ADRESNÍCH linek), PRNG (data-závislý crosstalk).
- 🔴 **Cache maintenance je tu otázka SPRÁVNOSTI, ne rychlosti.** Bez `clean` po zápisu by
  data zůstala v D-cache a do paměti se vůbec nedostala; bez `invalidate` před čtením by
  verify přečetl zpátky právě tu cache. **Vada paměti by se pak NIKDY neprojevila a
  benchmark by hlásil OK na rozbité RAM.** Dělá se jen u cacheable cílů (AXI/SRAM1/SDRAM).
- **Maska chybných bitů (`err_bitmask`)** — ukáže rovnou na konkrétní datovou linku (samý bit 7
  → jedna vadná dráha), spolu s adresou první neshody.
- 🔴 **Rozlišovací diagnostika (přidána 2026-08-23 po prvním HW běhu).** Souhrn „N chybných
  bitů" řekne, ŽE je něco špatně, ale ne CO — první běh vrátil 8,2 M chybných bitů v SDRAM.
  Doplněno: **`pat_err[]`** (chyby po vzorech — jen „adresa" = vadné adresní linky), **`first_err_got`/
  `want`** (přečteno vs čekáno — cizí *platná* hodnota = překryv adres), **`alias_off`** (test
  adresních linek, unikátní hodnota na každou mocninu dvou), **`retain_err`** (retenční test JEN
  pro SDRAM: zapiš, počkej 1 s, teprve pak ověř — pomalý refresh běžný test neodhalí; clean cache
  PŘED čekáním, invalidate PO něm).
- 🔴🔴 **NEUZAVŘENÝ NÁLEZ: ADRESY V SDRAM SE MOHOU OPAKOVAT PO 2 MB — intermitentní.** Zápis na
  `0xC0400000 + 2 MB` občas skončí na `0xC0400000`. **Objevuje se a mizí mezi běhy** (vrátilo se
  i při zdravé CM4) → **marginální/studený spoj**. Odpovídá nefunkční adresní lince **`FMC_A9` =
  `PF15`** (řádkový bit 9 ↔ `HADDR[21]`); firmware v pořádku → podezření na **HW (pájka `PF15`↔A9)**.
  **Prozvonit `PF15`**, až bude deska otevřená. Retence po 1 s = 0 chyb → refresh OK (⚠️ `REFRESH_COUNT`
  1835 vs výpočet 371 nesedí, `fmc.c`, ověřit zvlášť).
  - 🔴 **Nejdražší důsledek — `membench` ho PŘÍMO TESTUJE.** `FB2` (`0xC0200000`) se od `FB0`
    (`0xC0000000`) liší **právě jen v `HADDR[21]`** (ten podezřelý bit); totéž canvas pool vs `FB1`.
    Kdyby sdílely paměť, triple buffering by byl fakticky double. `fb_alias` (reverzibilní sonda)
    hlásí `!! FB0 … a FB2 … SDILEJI PAMET` nebo `(framebuffery se navzajem NEprekryvaji)`.
    **Než sáhneš na cokoli v zobrazovacím řetězci, přečti si tenhle řádek.**
- 🔴 **Bezpečnostní pojistka (`sdram_safety_check`) — proto, že překryv existuje.** Když se adresy
  opakují, „testuju jen vyhrazenou oblast" přestává platit. Před každým během se jednoslovnou
  **reverzibilní** sondou ověří, že testovaný blok nesdílí buňku s FB0/FB1/FB2/canvas/`.sdram`;
  při kolizi se test **přeskočí**. Změřená vzdálenost překryvu blok zároveň zkrátí.
- ⚠️ **Velikost SDRAM testu 4 MB → 512 kB:** při 4 MB se **rozbíjelo zobrazení** — ne přepisem FB
  (hlídá pojistka), ale **propustností FMC** (LTDC potřebuje ~46 MB/s). 512 kB = ~0,4 s.
- ⚠️ **Vlákna:** celý běh trvá jednotky sekund → **jen v UartTasku** (jediný nehlídaný watchdogem).
  UI tlačítko nastaví `g_membench_req`, `membench_service()` v UartTasku vykoná; ustupuje scheduleru
  (`osDelay(1)`) mezi vzory a cíli.
- ⚠️ **D2/D3 SRAM mají vlastní hodinový signál** (`RCC_AHB2ENR`), CubeMX ho pro CM7 nezapíná —
  `membench_run` volá `__HAL_RCC_D2SRAM1_CLK_ENABLE()`. Bez toho by region četl samé nuly.
- **W25Q scratch** (`W25Q_BENCH_BASE`, 8 sektorů = 32 kB) leží **za flight recorderem** ve volných
  2/3 DATA regionu. Jen **2 vzory** (adresa + 55/AA) — každý stojí celý erase cyklus. ⚠️ NOR flash
  umí jen 1→0 → **před každým vzorem erase**.
- 🔴 **Past při návrhu okna (HW test 2026-08-23): karta si kreslí VLASTNÍ hlavičku na baseline
  `rect.y + UI_DIM_CARD_PAD_Y + 16` = `rect.y + 25`.** První verze měla záhlaví sloupců na 86 při
  kartě od y=62 → popisek karty ležel přes „pamet / velikost / zapis". **Pravidlo: první vlastní
  řádek obsahu musí začínat aspoň ~30 px pod `rect.y` karty s hlavičkou.** Svislý rozpočet okna
  je v komentáři u `MEMB_ROW0`.
- ⚠️ **Řetězce ze snapshotu se čtou s `%.Ns`, ne holým `%s`** — `phase`/`msg` přepisuje UartTask
  ve chvíli, kdy je UiTask kreslí. Stejný důvod jako `strncpy` u `g_rtc_text`.
- ⚠️ **Jak číst naměřené rychlosti** (Debug/`-O0`): pořadí zápis DTCM > AXI > SRAM1 D2, čtení
  stejné (DTCM zero-wait-state). Zápis > čtení je normální. ⚠️ **Kdyby všechny vyšly shodně nebo
  DTCM pomaleji než AXI, měří se režie smyčky, ne paměť.** `SPEED_UNROLL` (po 8) + jeden součet
  na čtveřici (**neměnit bez změření**). ⚠️ **SDRAM měř nejlepší z `SPEED_PASSES`=3** (sdílí FMC
  s LTDC). ✅ Sanity: W25Q čtení ~4,46 MB/s = strop pollovaného QSPI.
- **Rychlost se měří zvlášť od ověřování**: zápis + samostatný čtecí průchod (součet do `volatile`,
  jinak GCC smyčku vyhodí); porovnávání běží až potom, neměřené. RAM přes **DWT CYCCNT**, QSPI přes
  `HAL_GetTick` (erase ve stovkách ms by CYCCNT při 480 MHz přetekl).

---

## 10. SD export — plné původní znění (před 2. redukcí 2026-08-29)

V CLAUDE.md zhuštěno; každý 🔴🔴/⚠️ trap zůstal. Plné znění:

### SD export (`sd_export.c/h`) — mount/unmount + CSV
- **Dělba podle blokování (kritické):** `sd_export_tick()` je **levný** (čtení GPIO + případný
  rychlý `f_mount(NULL)`) → volá ho **defaultTask** ~2 Hz. `sd_export_mount()` a
  `sd_export_run()` **BLOKUJÍ** (`HAL_SD_Init` desítky–stovky ms, zápis souboru sekundy) →
  **VÝHRADNĚ z UartTasku**, který není hlídaný watchdogem.
- **Auto-UNmount ano, auto-mount NE.** Odmountování při vytažení je instantní (jen zahodí
  ukazatel), takže se vejde do defaultTasku. Mount je blokující → dělá se explicitně.
- **UART: `sd` / `sd mount` / `sd unmount` / `sd export [N]`** → `GPSDO.CSV` (oddělovač **`;`**
  kvůli českému Excelu; čas jako **unix sekundy**; kmitočet bez `%f` — celé Hz + 5 desetin).
  Zapisuje se **chronologicky** (nejstarší první), `datalog_read_back(0)` je nejnovější.
- ⚠️ **Tlačítko v UI zatím není** — běželo by v UiTasku (watchdog) a potřebovalo by worker;
  stejné omezení jako `calib_save()`.
- **FatFs zapnutý 2026-08-11** (CubeMX: FATFS → SD Card, `Detect_SDIO = PE3`). `BSP_SD_Init()`
  sám kontroluje přítomnost karty a pak volá `HAL_SD_Init` + 4-bit → karta se inicializuje
  **líně přes FatFs**, proto je `MX_SDMMC1_SD_Init()` vyřazená správně.
  ⚠️ **`FIL`/`DIR`/`FILINFO` NIKDY na stack tasku** — při `_FS_TINY=0` nese `FIL` vlastní 512B
  sektorový buffer (~560 B). Na stacku dělal ze `selftest_body` 1176 B a z `export_body` 920 B,
  takže po SD operacích klesla rezerva UartTasku na ~660 B (změřeno `status` 2026-08-15).
  Řešení = `static FIL` (funkce běží výhradně z UartTasku a nejsou vnořené) → `sd_export_selftest`
  spadl na **56 B**. RAM_D1 má ~386 KB volných, takže je to zadarmo.
  `_USE_LFN=0` → jen 8.3 jména (`GPSDO.CSV` vyhovuje); `_FS_TINY=0` → `FIL` má vlastní 512B
  buffer → **`sd_export_run()` má 808 B rámec** (UartTask 4 kB, základ ~1,3 kB → ~1,8 kB rezerva).
- 🔴🔴 **`sd_diskio.c`: `ENABLE_SD_DMA_CACHE_MAINTENANCE` = 0 a `ENABLE_SCRATCH_BUFFER` VYPNUTÝ**
  (oba v USER CODE, přežijí regen). Blokující `HAL_SD_ReadBlocks/WriteBlocks` na H7 **NEpoužívají
  IDMA** — přehazují data procesorem přes FIFO (`SDMMC_ReadFIFO()` ve smyčce). A protože
  `BSP_SD_ReadBlocks_DMA`/`WriteBlocks_DMA` jsou **přepsané na tuhle blokující variantu**
  (`sd_export.c`, `__weak` v `bsp_driver_sd.c`), neteče přes IDMA vůbec nic.
  - **Cache maintenance je pak u čtení PŘÍMO ŠKODLIVÁ**: data píše CPU (dirty v D-cache),
    `SCB_InvalidateDCache_by_Addr` je bez zápisu zpět zahodí → přečtou se nuly. Přesně proto
    `sd fs` hlásil „karta není naformátovaná" u karty, kterou `f_mount` v pořádku namountoval.
  - **Scratch („slow path", bere se pro každý buffer nezarovnaný na 32 B — což `FIL` na zásobníku
    je) má v ZÁPISOVÉ větvi dvě chyby ST**: `Invalidate` se volá *před* `memcpy` a `Clean` chybí
    úplně (na kartu jde starý obsah RAM), a čeká se na `READ_CPLT_MSG`, přestože zápis posílá
    `WRITE_CPLT_MSG`. Projev: `f_write`+`f_close` projdou, ale adresářová položka se nezapíše →
    `f_open(FA_READ)` vrátí **FR_NO_FILE**. Bez scratche jde všechno fast-path a chyby jsou mimo hru.
  - ⚠️ **Kdyby se někdy vracelo ke skutečnému IDMA, musí se zapnout OBOJE — a napřed opravit ty
    dvě chyby.** Pravidlo: **cache maintenance patří VÝHRADNĚ k `_DMA`/`_IT` variantám.**
- **`BSP_SD_GetCardState()` je přepsaná na „polite polling"** (`osDelay(1)`, když karta není ready).
  `sd_diskio.c` na ni čeká v několika **těsných smyčkách bez yieldu** s `SD_TIMEOUT` = **30 s**;
  bez toho by zaseklá karta na 30 s vyhladověla UiTask → mrtvý dotyk + IWDG. `SD_TIMEOUT` je mimo
  USER CODE (regen ho vrátí), tahle funkce je `__weak` → jde přepsat bezpečně.
- 🔴🔴 **`SDMMC1_IRQHandler` MUSÍ existovat a NVIC být povolený** (`stm32h7xx_it.c` USER CODE 1 +
  `HAL_NVIC_SetPriority(SDMMC1_IRQn, 5, 0)` v `BSP_SD_Init`). `sd_diskio.c` dělá **každý** přenos
  přes `BSP_SD_ReadBlocks_DMA` a čeká na zprávu z `SDQueueID`, kterou posílá **jedině** ta obsluha.
  Bez ní čekal každý `f_mount`/`f_read` celých `SD_TIMEOUT` = **30 s** → UartTask vyhladověl UiTask
  → **IWDG reset**. Priorita **5**, obsluha volá FreeRTOS API.
- **⚠️ PŘEVZATO Z POSTUPU FRANTIŠKA (`C:\Claude_obecne\SD_franta.md`, rozchodil SD na TÉMŽE HW):**
  - **`BSP_SD_Init()` je přepsaný** v `sd_export.c` (generovaná verze je `__weak` → regen-safe).
    Důvod: `HAL_SD_Init()` na konci volá `HAL_SD_ConfigWideBusOperation(Init.BusWide)`, takže
    s `Init.BusWide = 4B` proběhne přepnutí **uvnitř identifikace** — a když selže, **spadne celá
    inicializace**, přestože karta v 1-bit režimu funguje. Proto identifikace běží **vždy 1-bit**
    a na 4 bity se přepíná až potom, s fallbackem. ⚠️ V `.ioc` musí `SD_4_bits_Wide_bus` ZŮSTAT —
    jen díky němu `HAL_SD_MspInit` nakonfiguruje piny D1–D3; liší se pouze runtime `Init.BusWide`.
  - **`hsd1.ErrorCode` je „sticky"** (HAL ho přiřazuje přes `|=`) a `ConfigWideBusOperation` na konci
    kontroluje jeho celý obsah → **před každým přepnutím se musí vynulovat**, jinak i úspěšný
    fallback vrátí zděděnou chybu.
  - **`disk.is_initialized[0] = 0` při unmountu** (`ff_gen_drv.c` si pamatuje, že `disk_initialize()`
    už proběhl, a podruhé ho nezavolá) — jinak mount bez karty skončí `FR_DISK_ERR` místo čistého
    `FR_NOT_READY`.
  - **Deskový erratum:** externí pull-up, který měl být na **CMD**, je omylem na **CLK**. CMD (PD2)
    proto **musí mít interní pull-up** — v našem `.ioc` už `GPIO_PULLUP` je ✅.
  - ⚠️ **Nepoužívat `-fsyntax-only`** na rychlou kontrolu — zastaví překlad před middle-endem, takže
    celá třída varování (`-Wformat-truncation`, `-Wmaybe-uninitialized`, `-Wstringop-*`) vůbec nevznikne.
- ⚠️ **Dvě nezávislé detekce téhož pinu (PE3):** `datalog_sd_card_present()` (debounced, app API)
  a `BSP_SD_IsDetected()` (raw, používá ho FatFs uvnitř). Nekolidují — jen čtení, stejný pin.
- **✅ ROZHODNUTO 2026-08-11: W25Q je autoritativní úložiště, SD je JEN EXPORT** (`sd_export.c/h`).
  W25Q uveze ~600 dní; SD slouží k vytáhnout a přečíst na PC. Odpadá **kontinuita `seq`**, **přepis
  MBR** i **ztráta dat při vyjmutí**.
- **⚠️⚠️ `DATALOG_SD_RAW_OK` = 0 a takové zůstane** → `probe()` vrací false, SD backend je inertní.
  RAW blokový zápis by šel od offsetu 0 = **LBA 0 = MBR karty**. **Nezapínat.**
- **🔴 Boot bez karty NESMÍ zabít přístroj:** `MX_SDMMC1_SD_Init()` má na selhání `Error_Handler()`
  a `HAL_SD_Init` selže vždy bez karty. Proto se v USER CODE **vyplní handle a hned `return`**.
  ⚠️⚠️ **Holý `return;` NESTAČÍ:** `HAL_SD_MspInit()` začíná `if (sdHandle->Instance == SDMMC1)`,
  takže s `Instance == NULL` se **nezapnou hodiny SDMMC1 ani nenakonfigurují piny** (nutno vyplnit
  handle před returnem).
- **✅ SD hardware ověřen na HW (2026-08-12, přes GDB):** `BSP_SD_Init()` = `MSD_OK`, karta **SDHC
  14,5 GB**, `CardState = TRANSFER`, `CLKCR = 0x4002` (4-bit, 16 MHz). **Tím padá podezření na STATUS #69.**
- **✅ Card-detect PE3 FUNGUJE, polarita LOW = karta zasunuta** (vyřešeno mechanicky 2026-08-13 —
  chyba byla v kontaktech slotu). `sd force on` / `sd det invert on` = jen nouzový únik.
- **UART příkazy SD:** `sd diag`, `sd test` (zápis 8 kB + zpětné čtení), `sd det [invert on|off]`,
  `sd force [on|off]`, `sd mount`/`unmount`, `sd export [N]`.
- **Okno SD KARTA (`s_view=37`)** — dlaždice v Nastavení. Živý stav + tlačítka **PŘIPOJIT↔ODPOJIT**
  (label = AKCE), **TEST** (verify 8 kB + propustnost 512 kB → KB/s), **EXPORT CSV**, **FORMAT
  = DVOJÍ potvrzení** (`s_sd_fmt_stage` 0→1→2, auto-zrušení po `SD_FMT_TIMEOUT_S`=6 s) →
  `f_mkfs("",FM_FAT32,…)`. ⚠️ **`_USE_MKFS=1` musí zůstat.** UART: `sd format yes yes`.
  ⚠️ **Tlačítka jen nastaví `g_sd_req`** — blokující práci dělá UartTask v `sd_export_service()`.
  Kapacitu počítá taky UartTask (`f_getfree`), UI čte snapshot `sd_export_ui_info()`.
- **`get_fattime()`** (`CM7/FATFS/App/fatfs.c` USER CODE) čte **`g_rtc_text_local`**. ⚠️ Nevolá
  `HAL_RTC` — běží z UartTasku a RTC registry patří defaultTasku.
- ⚠️ **Souběh:** `sd_export_tick()` (defaultTask) při vytažení karty odmountovává, ale export/test
  běží v UartTasku → příznak **`s_busy`** auto-unmount po dobu blokující operace přeskočí.
  **Nastavuje se VÝHRADNĚ ve wrapperu** (`sd_export_selftest`/`sd_export_run`), tělo je vyčleněné
  do `selftest_body`/`export_body` → `s_busy = true; r = body(); s_busy = false;` (první verze ho
  neuklidila na chybových cestách = natrvalo vypnutý auto-unmount). ⚠️ **Nový `return` patří do těla, ne do wrapperu.**
- **⚠️ IDMA + D-cache (nejspíš příčina STATUS #69):** IDMA **nedosáhne na DTCM** a AXI SRAM je
  **cacheable WB**. Proto `sd_hal_*` používá **statický bounce buffer v `.bss` (RAM_D1) zarovnaný
  na 32 B** + clean/invalidate, ne buffer volajícího (`blk_read`/`blk_write` mají `uint8_t blk[512]`
  na nezarovnaném stacku — cache maintenance nad ním by poškodila okolní data).

---

## 20. Poznámka k `STATUS.md`

Uživatel navrhoval přesun datovaných záznamů do `STATUS.md`. `STATUS.md` je ale už teď 358 řádků
a je to **cross-project** soubor (obě strany + SPI kontrakt + TODO). Datované „HOTOVO" stopy z
CLAUDE.md se proto **nezkopírovaly do STATUS.md** (většina jeho TODO tabulky je stejně už nese) —
jsou pokryté tímto archivem + git logem. Pokud je chceš mít i ve STATUS.md, řekni.
