# STATUS — GPSDO / čítač kmitočtu (celkový přehled)

> **Společný rozcestník obou stran projektu.** Monorepo `Frequency_Counter_FPGA`
> obsahuje dva podprojekty, které spolu komunikují přes SPI. Tento soubor je
> **jediný cross-project pohled**: stav, sdílená smlouva a otevřené úkoly
> s vyznačením závislostí. Detaily NEduplikuje — odkazuje na dokumenty stran.
>
> **Poslední aktualizace:** 2026-07-18 · udržují OK2HAZ & OK2JNJ.
> Při větší změně na jedné straně aktualizuj sekci **Sdílený kontrakt** a **TODO**.

---

## Co to je
STM32H757 (host, displej + logika) měří kmitočet, který mu přes SPI dodává
FPGA (GW1NR-9) — reciproký 4-fázový čítač s front-endem MC100EP016A / MAX9601.
Reference 4×100 MHz (0/90/180/270°) generuje Si5356A z 10 MHz OCXO,
disciplinovaného z GPS (NEO-7M). Cíl = GPSDO + přesný čítač do ~1,4 GHz.

```
  GPS (NEO-7M) ──1PPS/UTC──► STM32H757 ◄──── TMP117/ADS1115/W25Q (senzory, flash)
                                │ SPI2 (64B rámec, master)
                                ▼
   OCXO 10MHz ──► Si5356A ──4×100MHz(0/90/180/270°)──► FPGA GW1NR-9
                                                          │  MC100EP016A ÷4/÷16
   RF vstup ──► MAX9601 tvarovač ──────────────────────► (reciproký čítač + TDC)
```

---

## Stav stran

### STM32H757 (`Frequency_Counter_STM32H757/`) — zralý
- Displej, FreeRTOS, senzory, GPS, RTC, W25Q flash, UI (libprim/libui/app),
  menu + okna, alarmy, IWDG, holdover — **funkční**. Detaily → `CLAUDE.md`.
- SPI driver `fpga_freq.c` (64B rámec, CRC16, polling ~20 Hz, /4↔/16 hystereze) — **hotový**.
- ⚠️ **Velké číslo na hlavní obrazovce je zatím SIMULACE.** Reálná data z FPGA
  tečou jen do UART `freq` + diag okna (`g_freq_text`/`g_freq_info`).
  **Napojení reálných dat na headline + statistiky = hlavní otevřený úkol.**
- **Nové 2026-07-16..18** (detaily → `CLAUDE.md`):
  - **Kalibrace editovatelná** (`calib.c/h`) — AD8307 slope/intercept + ADS 12V/5V
    gain přes −/+, ULOZIT persistuje do **W25Q CALIB store**; napojeno na reálný
    přepočet RF dBm i napěťových větví.
  - **Nastavení persistentní i přes power-cycle** (`syscfg.c/h`) — BKP přežije jen
    warm reset, proto zrcadlo do **W25Q CONFIG store** (debounced zápis ~1,5 s).
  - **Časová zóna** — okno „Cas", AUTO CET/CEST dle EU pravidla nebo ruční −12..+14 h.
  - **Trend až 60 dní** — decimační pyramida (×4, 9 stagí); presety 1 min…60 dní.
  - **Nová okna:** Čítač (syrový detail měření FPGA — užitečné pro SPI bring-up),
    Selftest (per-test + SPUSTIT), Komunikace (blokové schéma stavů), Cas.
  - **Si5356 reg 218 — OPRAVA ZÁMĚNY BITŮ**, viz TODO #8 (mělo dopad na diagnostiku).

### FPGA modul (`Frequency_Counter_FPGA_Module/`) — bring-up / v2.0
- Carrier board **v2.0** (KiCad) + Verilog RTL (`src/top.v`, `spi_app.v`,
  `spi_slave_phy.v`, `coarse_counter.v`, `tdc_vernier_4phase.v`) + sim testbenche
  (`sim/tb_*.sv`) + `pins.cst`/`timing.sdc` — **v repu**.
- Kalibrace fází TDC → `PHASE_CAL_DESIGN.md` (numerická LUT → IODELAY deskew).
- Osazení/wiring v2.0 → `BOARD_V20_CHECKLIST.md` (⚠️ U3 odpojené, `div_res` kritický).

---

## ⚠️ Sdílený kontrakt (= hlavní závislost mezi stranami)

**SPI: STM32 master, FPGA slave, mode 0, MSB, 8-bit. Pevný 64B full-duplex rámec.**
SCK cíl ≤6 MHz (max ~10 MHz). CRC-16/CCITT-FALSE (0x1021/0xFFFF) přes byte 0..61.
DATA payload (TYPE 0x80): `frequency_x100000` (/4), `freq16_x100000` (/16),
`edge_count`, `gate_time_ns`, `phase_status`, `error_flags`, …

- **Autoritativní specifikace v1:** `Frequency_Counter_STM32H757/CLAUDE.md`
  → sekce „FPGA strana protokolu" (tabulka offsetů, bity STATUS/FLAGS, škálování).
- **Handoff / bring-up:** `Frequency_Counter_STM32H757/FPGA_SPI_HANDOFF.md`,
  `FPGA_INSTANCE_BRIEF.md`.
- **Protokol v2 (návrh + odpověď FPGA strany):** `FPGA_PROTOCOL_V2_NAVRH.md`.

> **Pravidlo:** jakákoli změna rámce/offsetů/škálování se promítá do OBOU stran.
> Nejdřív uprav specifikaci v `CLAUDE.md`, pak obě implementace, a zapiš do TODO níže.

---

## TODO — cross-project (šipka = kdo na koho čeká)

| # | Úkol | Strana | Závislost / blokátor |
|---|------|--------|----------------------|
| 1 | **Reálná data FPGA → headline + statistiky** (místo simulace) | STM32 | ⬅ potřebuje FPGA vydávající platné 64B rámce (link bring-up) |
| 2 | **SPI link bring-up na HW** (`RX0:FF` = FPGA nebudí MISO) | FPGA + STM32 | obě — CS/SCK/MISO, config load z flash, viz `CLAUDE.md` diagnostika |
| 3 | **Kalibrace fází TDC** (numerická LUT → IODELAY) | FPGA | `PHASE_CAL_DESIGN.md`; STM32 pak zobrazí `phase_status` PH:F/F |
| 4 | **Protokol v2** (rozšíření rámce) | obě | `FPGA_PROTOCOL_V2_NAVRH.md` — nejdřív dohodnout, pak obě strany |
| 5 | Osazení + oživení carrier v2.0 | FPGA | `BOARD_V20_CHECKLIST.md` |
| 6 | ✅ **Datalogging stability do W25Q DATA regionu — HOTOVO 2026-07-20 (v0.4.0).** | STM32 | `datalog.c/h` = append-only **kruhový** log, **32 B / 10 s → ~600 dní** v DATA regionu (63,9 MB), pak přepis nejstaršího. Záznam: f, teploty OCXO+deska, OCXO Vc, RF (**syrové mV**, ne dBm — kalibrace se může změnit), GPS lock/sat/HDOP, flags, **CRC16**. Pozice zápisu se po bootu **odvodí ze `seq`** (žádná metadata → přežije výpadek napájení). Tick v **defaultTask** s krátkým QSPI mutex timeoutem (10 ms) — při obsazené flash vzorek zahodí, watchdog nezdrží. **Úložiště za abstrakcí `datalog_backend_t`** → SD karta připravená (`datalog_sd.c`, `probe()`=false + 5bodový plán dodělání: SDMMC1 v .ioc, cache koherence, 512B RMW, FatFs, hot-unplug). Okno Datalog (s_view=17) je nově **živé** + tlačítko ZAPNOUT/VYPNOUT (persist v syscfg, ⚠️ magic `"SCFG"`→`"SCF2"` = jednorázový reset nastavení). UART `datalog [on\|off\|erase\|dump]`. Selftest **7/7** (`datalog_selftest`). ⚠️ Kmitočet bere z `fpga_freq_get_last()`, tj. **REÁLNÝ z FPGA** (ne ze simulace hlavní obrazovky) — dokud neběží SPI link (#2), bude v záznamech **0** a užitečné budou jen teploty/Vc/GPS. Smysluplný obsah tedy až po #2. |
| 7 | Reálná kalibrace RF (AD8307 slope/intercept) → CALIB store | STM32 | **infrastruktura HOTOVÁ** (`calib.c/h`, okno Kalibrace, persist do W25Q) — zbývá jen **změřit skutečné hodnoty** na HW a zadat je |
| 8 | **Si5356 „LOS_CLKIN" — VYŘEŠENO 2026-07-18: byla to FW záměna bitů, HW je V POŘÁDKU.** Kritická validace odhalila, že reg 218 má dle **AN565**: bit2 = **LOS_XTAL**, bit3 = **LOS_CLKIN** (FW měl bit2 mylně jako LOS_CLKIN). Pozorovaný status `0x04` = bit2 = LOS_XTAL — krystal XA/XB na desce **není osazen** (piny uzemněné dle datasheetu) → bit je **trvale 1 a benigní**. Skutečný LOS_CLKIN (bit3) = **0** → **TTL buzení CLKIN detektoru plně vyhovuje** (dřívější teorie „TTL 2,4 V < VIH 2,64 V" tímto empiricky vyvrácena — VIH je garanční mez, ne skutečný práh detektoru). Důkazy (4 nezávislé): AN565 tab. reg 218 + datasheet Fig. 7 (změřené sloupce) + schéma XA/XB→GND (#PWR086) + CBPro mapa `{6,0x04,0x1D}` maskuje přesně bit2 (LOS_XTAL) interrupt. **FW opraveno**: definy bit2/bit3, LOS_CLKIN(bit3)=ČERVENÁ (⚠️ AN565: **PLL_LOL se při fyzické ztrátě vstupu neasertuje** — bit3 je hlavní indikátor ztráty reference), bit2 ignorován. **Žádná HW změna není potřeba** — LMK1C1104/LVC1G17 bezpředmětné pro LOS (s bufferem by 0x04 svítil dál!). **Verifikace po flashi**: odpojit 10 MHz → status `0x08/0x0C`+červené „LOS CLKIN!", připojit → `0x04`+zelené „LOCK OK". Volitelná hygiena: scope-check overshoot ≤3,63 V na pinu 4. (Pozn. trvale platné: 74ACT/HCT jen 4,5–5,5 V.) | — vyřešeno (FW) | flash + 1min verifikace |
| 9 | ✅ **HOTOVO 2026-07-18: detekce přetečení zásobníku zapnuta + UartTask zvětšen** (audit) | STM32 | **1)** `configCHECK_FOR_STACK_OVERFLOW=2` + `configUSE_MALLOC_FAILED_HOOK=1` — obě byly NEDEFINOVANÉ → FreeRTOS je bral jako 0 → hooky ve `freertos_hooks.c` se nikdy nevolaly (crash black-box „stack:Task" byl mrtvý kód). Nastaveno v **`FreeRTOSConfig.h` USER CODE Defines** = regen-safe, stejný vzor jako `configGENERATE_RUN_TIME_STATS`. ⚠️ **Proto je NENASTAVUJ v CubeMX GUI** (vzniklo by dvojí `#define`). Ověřeno preprocesorem: `vApplicationStackOverflowHook` je nyní v `tasks.c` 2× (dřív 0×). **2)** UartTask 512→**1024 words** (2→4 KB) v `.ioc` `FREERTOS_M7.Tasks01` **i** `freertos.c` — rámec `UartTask_run` má při `-O0` (Debug = co se flashuje) 1256 B, tj. 61 % z původních 2048 B. Stacky celkem 17,4 KB z 32 KB heapu (15,4 KB rezerva). |
| 10 | 🔶 **Ověřit na HW, že detekce přetečení zásobníku zabírá — PŘIPRAVENO 2026-07-20, čeká na flash.** | STM32 | Původní návrh (dočasně přidat `waste[]` do kódu, build, flash, odstranit) nahrazen **runtime UART příkazem `stacktest yes`** — žádný rebuild, test se spustí z konzole kdykoli. Záměrně přeteče stack **UartTasku** (`volatile char waste[3600]` — jen o ~760 B, aby se stihl zavolat hook dřív, než by se rozsypal sousední heap na HardFault), pak `osDelay(1)` → kontrola stack patternu při přepnutí kontextu → `vApplicationStackOverflowHook` → `crash_blackbox` do BKP_DR3..5 → `__disable_irq()` + spin → bez heartbeatů **IWDG reset do ~4 s**. **Ověření po restartu:** System Health / UART `status` musí ukázat **„Reset: … stack:UartTask"**. Vyžaduje přesně `stacktest yes` (samotné `stacktest` jen vypíše nápovědu — ochrana proti překlepu). ⚠️ Bez VBAT baterie testuj **warm resetem** (BKP nepřežije power-cycle). **Zbývá: flashnout v0.4.0 a příkaz spustit.** |
| 11 | ✅ **UI: dotykové cíle a typografie pro 4,3" panel — HOTOVO 2026-07-19 (vč. 2b).** | STM32 | Panel je **4,3" 800×480 = 8,54 px/mm (217 DPI)**. Hotovo (1.–4. vlna, detaily v `UI_SIZES.md`): hit-slop + rozvržení hlavní obrazovky + Nastavení/Čas/Kalibrace ovladače 56→64 px + Kalibrace přeskládána; pilulky 30→36→42→**46 px**; karta Allan font 14→**16**. **(2b) HOTOVO**: globální bump `mono_16`→`18`/`sans_16`→`18` na **~48 místech** (Diagnostika, GPS, Health, Senzory, Paměť, O přístroji, boot splash, Reference, Kalibrace, Čítač, histogram/trend overlaye, komunikační diagram) — každé ověřeno tabulkou fontů na **šířku i skutečnou výšku glyfu** (`oy` konkrétního znaku, ne nominální `ascent` fontu — ten je worst-case pro celou znakovou sadu a zbytečně by zamítl bezpečné případy, jak se ukázalo u komunikačního diagramu). **6 míst vědomě ponecháno na mono_16/sans_16** (komentář `TODO #11(2b)` u každého v kódu): GPS Vet/Fix čítače, GPS HDOP/PDOP, Diagnostika `g_freq_info`, Health „Reset:" řádek — všechny přetékají svůj box i při současném fontu, bump by to zhoršil; main-old větev (`draw_stat_card` pro `s_layout_old`) — na zamrzlé referenční obrazovce se záměrně nedělají další úpravy, viz #14. |
| 12 | ✅ **UI: odstranit duplicity v `app_gpsdo.c` — HOTOVO 2026-07-19 (A1/A2/A3/A4 vše).** | STM32 | **(A1):** prolog okna (`app_gpsdo_init`+`prim_set_target`+`prim_reset_clip`, 24 míst) sjednocen na `window_prep()`/`window_first(view_id)` — mechanická náhrada, chování beze změny. **(A2):** `fmt_temp`/`fmt_d1`/`fmt_fN`/`fmt_minmax` sdílí `fixed_split()` (čistá aritmetika) + `fmt_fixed()` (jedna hodnota→text); `fmt_minmax` skládá dvě hodnoty do jednoho `snprintf` (ne přes mezibuffery — to by kaskádovitě rozjelo GCC `-Wformat-truncation`, ověřeno a opraveno). **(A3):** `dval`/`dtext`/`dtext_c` sdílí `dtext_a(pos,baseline,boxw,v,col,font,align)`; 1px nesrovnalost v geometrii boxu sjednocena. **(A4) dokončeno:** celoplošná karta měla 5 variant geometrie — 3 skutečně duplicitní páry (5+5+**2**, poslední pár — Čítač/Čas — nalezen až při dodatečné kontrole) mají jméno (`DG_CARD_FULL_A`/`_B`/`_C`), zbylé 3 výjimky (O přístroji, Kalibrace, Komunikace) mají opravdu unikátní obsah — pojmenování jednorázové konstanty by nebylo DRY, záměrně ponechány inline. Ověřeno `-Wall -Wextra -Wshadow -O2`: 0 nových warningů (stejná množina `-Wstringop-truncation` jako před revizí, viz #13). |
| 13 | ✅ **Audit velikostí cache v `dchg()` — HOTOVO 2026-07-19: potvrzeno false positive.** | STM32 | Překlad `-O2` hlásí **~54× `-Wstringop-truncation`** v `dchg` (`strncpy(cache, now, n-1)`). Zkontrolováno **6 namátkových vzorků** napříč souborem (`c_utc[26]`/`c_loc[34]` v okně Čas, `c_spi[68]` v Diagnostice, `c_mute/c_f/c_g[12]` v Alarmech) — ve **všech** je cache buffer (`c_*[N]`) záměrně **stejně velký jako zdrojový `snprintf` buffer** (`c_spi[68]` ↔ zdroj `sig[68]`, `c_mute[12]` ↔ zdroj `b[12]` atd.), takže `strncpy` nikdy reálně neořízne — GCC jen nepočítá s tím, že oba buffery jsou párované, a hlásí teoretické maximum zdrojového typu (`%s`/`%ld` bez znalosti reálného obsahu). **Závěr: false positive z principu návrhu, ne z nedbalosti** — vzor „cache = velikost zdroje" je konzistentní přes celý soubor. Nepotřebuje další zásah; ⚠️ při přidávání NOVÉHO `dchg` volání do budoucna přesto dodržet stejný vzor (cache stejně velká jako zdrojový buffer), jinak by hrozilo tiché zamrznutí pole (dvě různé hodnoty se stejným ořízlým prefixem = "beze změny"). Počet varování mírně kolísá (54↔55) mezi buildy kvůli GCC duplicitní diagnostice u rozbalených smyček (stejný řádek nahlášen 2×) — neznamená to novou chybu, ověřeno dohledáním konkrétních zdrojových řádků. |
| 14 | **Odstranit dočasnou A/B srovnávací větev hlavní obrazovky** (zavedeno 2026-07-19) | STM32 | Kvůli vizuálnímu porovnání starého (pre-4,3") a nového layoutu hlavní mřížky na HW existuje **dočasný přepínač**: `screen_main_toggle_layout()`/`screen_main_layout_is_old()` v `screen_main.c` (`s_layout_old`, `render_body_grid_v1`/`_v2`, `render_right_column_v1`, dispatch v `draw_stat_card`), footer tlačítko slotu 0 je **dočasně "Main SW"** místo skutečného přepínače PERIOD/FREQ (viz `footer_button_def` case 0 a `app_gpsdo_handle_touch` b==0 v `app_gpsdo.c` — obojí označeno `⚠️ DOCASNE`). ⚠️ **Default přepnut na STARÝ layout** (`s_layout_old = true`, 2026-07-19) — je to zamrzlá referenční verze, na které se **nedělají žádné další úpravy** (proto zůstala i mimo #11(2b) bump, viz `draw_stat_card`); veškerý další vývoj hlavní obrazovky cílí na `_v2`. **Až padne rozhodnutí, které rozvržení zůstane:** (1) smazat `_v1` funkce + `s_layout_old` + dispatch v `draw_stat_card`, (2) vrátit `case 0` ve `footer_button_def` na `MODE_NAME[st.mode ? 0 : 1]` a smazat `b==0` větev v touch handleru (příslušné komentáře v kódu obsahují přesné původní znění), (3) tohle udělat **PŘED** #12/A4 (dořešení zbylé geometrie karet — už jen 3 legitimní výjimky, viz #12), ať se neduplikuje práce na dvou paralelních layoutech najednou. |
| 15 | ✅ **Menu rozšířeno na 3×4 (12 dlaždic) — HOTOVO 2026-07-19, 3 sloty volné pro budoucí funkce.** | STM32 | `MENU_ITEMS` má nově 12 pozic (bylo 9): 9 stávajících funkcí beze změny obsahu, **4. řádek = 3× "Placeholder N"** (`ACT_PLACEHOLDER`, no-op). Až bude jasné, co tam patří, stačí v `MENU_ITEMS` (app_gpsdo.c) přejmenovat label a přidat `case` do `menu_activate()` — geometrie (248×76 px dlaždice, 8,9 mm výška) i touch handler (přeskakuje `nav_push` pro placeholdery) jsou hotové. |
| 18 | 🔶 **Náhodné watchdog resety („jednou za čas") — diagnostika doplněna 2026-07-20, příčina zatím NEPOTVRZENÁ.** | STM32 | Prostý IWDG reset byl **němý** — RSR řekl jen „watchdog", ne který task přestal koupat. Doplněno: **(1)** `watchdog_supervise` před vypršením IWDG zapíše do crash black-boxu (BKP_DR3..5, kind 3) jméno tasku se starým heartbeatem → po restartu `stall:UiTask` / `stall:FpgaTask` / `stall:BOTH`; **(2)** UART `status` nově vypisuje příčinu resetu + crash text + RSR + heap + CPU + **volný stack všech 5 tasků** + stav datalogu (dřív jen „RUNNING" — proto se to špatně honilo). **Opraven konkrétní nález:** `w25q.c wait_ready()` čekal na dokončení erase (50–400 ms, timeout 1000 ms) v **čistém spinu bez yieldu** → erase volaný z defaultTasku (Normal) úplně vyhladověl UiTask (BelowNormal) a defaultTask se sám nedostal na `watchdog_supervise`; nově `osDelay(1)` mezi dotazy. Týká se i `calib_save` (UiTask) a syscfg auto-save, tj. **existovalo už před v0.4.0**. Také `datalog find_head()` (16352 QSPI čtení při bootu) nově yielduje po 512 blocích. **Zbývá: flashnout, po dalším resetu přečíst `status` a podle `stall:<task>` dohledat viníka.** |
| 17 | ✅ **UI: RUN/STOP obráceně + červený STOP + podbarvení kmitočtu; okraje mřížky 12→4 px — HOTOVO 2026-07-20 (v0.4.0).** | STM32 | **(1)** Footer slot 1: label = **AKCE**, ne stav — při běhu **červené „STOP"** (`UI_BUTTON_STOP`, nová varianta v libui + `btn_stop_*` v tmavé i světlé paletě), při zastaveném zelené „RUN". **(2)** Při STOP se zóna velkého kmitočtu podbarví `UI_COLOR_FREQ_STOP_BG` (poloprůhledná červená ~15 %) → měření zjevně stojí. Alfa vyřazuje DMA2D fast-path, ale při STOP neběží 20Hz `tick_freq`, takže se kreslí jen při plném renderu a při stisku (CPU ~0); přepnutí volá nové `screen_main_redraw_freq_area()`, protože per-segment dirty cesta by podklad nepřekreslila. **(3)** `SCR_MAIN_GRID_MARGIN` 12→**4** px v **obou** layoutech (v1 i v2) — 12 px po stranách byl čistý nevyužitý pruh. Zisk 16 px se dělí dle ratia: Allan 364→**372**, pravý sloupec (trend/drift) 398→**406**; vnitřek stat karty 125→128 px = větší rezerva na mono_18. ⚠️ **Neověřeno na HW** (poslední flashnutý build byl v0.3.0). |
| 16 | ✅ **Kompletní revize necommitnutého UI balíku — HOTOVO 2026-07-19, 3 nálezy opraveny.** | STM32 | Křížová kontrola všech dnešních změn (nic z toho neběželo na HW) + kompilační sweep VŠECH zdrojáků app/libui/libprim. **(1)** Řada pilulek v headeru kolidovala s **clear zónami sekundového redrawu hodin/data** (x=648/644 — dosud se ověřovalo jen proti textu hodin na 674; datová zóna s pilulkami výšky 46 koliduje i svisle) → přidán `HDR_PILL_LIMIT`+fit-check `hdr_pill_fit` (komentář to sliboval odjakživa, kód nedělal), mezery pilulek finálně 4/5. **(2)** Fit-check by v holdover stavech vyřadil právě HOLD pilulku → **HOLD přesunut před CAL** (pořadí = důležitost; CAL je statický placeholder). Ověřeno simulací všech stavů přes tabulky fontů. **(3)** `fmt_fixed`/`fmt_temp`/`fmt_minmax` ztrácely znaménko u hodnot −0,99..−0,01 (pre-existující ve všech 4 původních kopiích) → opraveno `-` předponou. **Bonus:** ověřen mezitímní CubeMX regen (všechny USER CODE přežily, diffy jen EOL — regen-safe návrh fungoval; ztracen jen komentář na generovaném řádku freertos.c, hodnota drží z .ioc), `Backup/` složky do `.gitignore`, mrtvá `SCR_MAIN_SMALL_CARD_H` pryč, zastaralé komentáře srovnány. Detaily → `UI_SIZES.md` 6. vlna. ⚠️ **Pořád platí: nic není ověřeno na HW displeji** — všechna geometrie je počítaná (tabulky fontů), první flash ukáže realitu. |

---

## Kde co hledat (mapa dokumentů)

**Kořen repa:** `CONTRIBUTING.md` (workflow, IOC-owner pravidlo, **§7 dual-core flash**) ·
`STATUS.md` (tento soubor) · `tools/make_release_image.ps1` (combined CM7+CM4 image).

> ⚠️ **STM32H757 = dual-core. Flashuj OBĚ banky** (CM7 `@0x08000000` + CM4 `@0x08100000`)
> a měj `BCM4=1` v option bytes, jinak černý displej. Detaily → `CONTRIBUTING.md` §7.

**STM32 (`Frequency_Counter_STM32H757/`):** `CLAUDE.md` (bible) ·
`CUBEMX_CHECKLIST.md` · `FPGA_INSTANCE_BRIEF.md` · `FPGA_PROTOCOL_V2_NAVRH.md` ·
`FPGA_SPI_HANDOFF.md` · `NAVRH_ARCHITEKTURA_CM7_CM4.md` · `USB_CDC_PLAN.md` ·
`CM7/GPSDO_UI_README.md`.

**FPGA (`Frequency_Counter_FPGA_Module/`):** `BOARD_V20_CHECKLIST.md` ·
`PHASE_CAL_DESIGN.md` · `FPGA_MEAS_VALIDATION.md` (validace metod: rozpočet chyb,
drift/kompenzace, koherentní limitace, placement GW1NR-9C; 2026-07-19) ·
`FPGA_module_schematic.pdf` · `src/` (RTL) · `sim/` (testbenche).
