# TODO — prioritní, na zítra (2026-08-12)

> Nedokončené věci z relace 2026-08-11 (boot-loop fix → audity → SD/FatFs) + nové zadání.
> Řazeno podle toho, co blokuje co. Čísla `#nn` odkazují na `STATUS.md`.

---

## P0 — blokuje všechno ostatní

- [x] ✅ **Build rozchozen 2026-08-12** — `make exit=0`, vznikl `.elf` (text 664 340 B, data 956 B,
      bss 1 891 632 B) i `.hex`. Ověřeno, že v `.elf` jsou `HAL_SD_Init`, `f_mount`, `f_open`,
      `sd_export_run`, `datalog_sd_card_present`, `BSP_SD_IsDetected`.
      **V kódu ani v `.ioc` nebyla chyba** — `.project` měl všech 64 `<link>` správně.
      Zastaralé byly **generované makefily v `Debug/`**; F5 ani Clean je nepřegenerovaly.
      Dopsáno ručně (postup v `CUBEMX_CHECKLIST.md`): include v `Debug/makefile`,
      `subdir.mk` pro FatFs + HAL SD, a hlavně **`objects.list`, který nemá v makefile pravidlo**.

- [x] ✅ **Build drží i po regeneraci z IDE** — ruční zásahy do `Debug/` si IDE opravdu přepsalo
      (potvrzeno), takže je nahradil **`CM7/makefile.defs`**: hook `-include ../makefile.defs`
      (řádek 43 `Debug/makefile`) plní `USER_OBJS`, které jde na linkovací řádku **mimo
      `objects.list`**. Nezávislé na modelu IDE. Ověřeno `make exit=0` + symboly v `.elf`.

- [x] ✅ **Reimport projektu zabral 2026-08-12** — IDE model postavený od nuly, nové soubory zná
      (`subdir.mk` má `hal_sd`, FatFs `subdir.mk` vzniklo, `objects.list` má FatFs objekty).
      Zaplata **`CM7/makefile.defs` odstraněna** — projevila se 117× `multiple definition`,
      což byl signál „už není potřeba", ne chyba. Build z IDE i z CLI: `exit=0`, 0 chyb.

- [x] ✅ **Test „boot bez karty" PROŠEL 2026-08-12** — displej naběhne, žádné blikání LED_1.
      Ošetření přes USER CODE v `MX_SDMMC1_SD_Init()` drží.

- [x] ✅ **SD hardware ověřen (GDB, 2026-08-12)** — `BSP_SD_Init() = MSD_OK`, karta **SDHC 14,5 GB**,
      `CardState = TRANSFER`, `CLKCR = 0x4002` (4-bit, 16 MHz). **STATUS #69 tím padá.**
      ⚠️ Cesta k tomu vedla přes chybu, kterou jsem si sám udělal: holý `return;` v USER CODE
      nechal `hsd1.Instance == NULL`, takže `HAL_SD_MspInit()` (`if (Instance == SDMMC1)`) vůbec
      nezapnul hodiny SDMMC1. Opraveno — handle se teď vyplní a přeskočí se jen `HAL_SD_Init`.

- [ ] 🔴 **Dokončit SD: `sd force on` → `sd diag` → naformátovat na FAT32 → `sd test`.**
      Poslední známý stav: HW jede, ale **`f_mount` selhává**. Silné podezření **`FR_NO_FILESYSTEM`** —
      `ffconf.h` má `_FS_EXFAT = 0` a karta 14,5 GB může být exFAT.
      `sd test` (zápis 8 kB + zpětné porovnání obsahu) je pak finální důkaz celé cesty.
      ⚠️ Firmware s auditními opravami je **zbuilděný, ale NENAFLASHOVANÝ** (ST-LINK odpojen).

- [ ] **Při prvním flashi si přečíst boot výpis `SELFTEST:`** — nově vypíše i indexy failujících
      testů a u SCPI přímo `scpi.c:<řádek>` prvního neúspěšného assertu. Tím se zavře otevřená
      otázka „SCPI parser hlásí chybu při startu" bez dalšího hádání.

- [ ] **Card-detect PE3 nereaguje** — HIGH i se zasunutou kartou. Nikdy se nezměřilo s kartou
      VENKU, takže může jít i o obrácenou polaritu (`sd det invert on`). Dohledat ve schématu,
      kam `SDMMC1_DET` doopravdy vede. **Není blokátor** — `sd force on` to obejde.

---

## P1 — dokončit SD/FatFs (rozpracované)

- [ ] **`SDMMC1.ClockDiv` 2 → 4** (16 → 8 MHz) pro bring-up. Deska má na SD VDD jen C75 100n
      (chybí bulk 4,7–10 µF) a na CK není sériový tlumicí odpor → při 16 MHz hrozí překmity.
      Zvýšit zpět, až přenos poběží. ⚠️ Změnit **i v `sd_probe()`**, které `Init` zrcadlí.
- [ ] **PE3 → `GPIO_PuPd = Pull-up`** v CubeMX. Dnes generuje `GPIO_NOPULL`; funguje to (externí
      47k na desce) a `sd_det_init()` si stejně nastaví PULLUP — čistě konzistence.
- [ ] **Rozhodnout formát exportu.** Dnes `sd export` → `GPSDO.CSV` s `FA_CREATE_ALWAYS`, tedy
      **přepisuje**. Varianty: (a) nechat, (b) přírůstkově (`FA_OPEN_APPEND`), (c) časové razítko
      v názvu. ⚠️ Pro (c) je nutné zapnout **`_USE_LFN`** — teď je 0, tedy jen 8.3 jména.
- [ ] **Auto-mount při vložení karty.** Dnes se mountuje líně až při prvním `sd export`
      (mount blokuje → nesmí do defaultTasku). Chce to buď request-flag zpracovaný v UartTasku,
      nebo to nechat tak, jak je (insert → EXPORT je použitelné UX).
- [ ] **Tlačítko EXPORT v okně Datalog.** Neudělané schválně: běželo by v UiTasku, který je hlídaný
      watchdogem, a export blokuje sekundy → potřebuje worker. Stejné omezení jako `calib_save()`.

---

## P2 — ověřovací dluh (nic z toho nikdy neběželo na HW)

- [ ] **Projet `HW_OVERENI_PRUCHOD.md`** — jeden průchod, **11 funkcí** implementovaných naslepo
      (#31, #32, #33, #43, #44, #47, #52, #53, #54, #67, #68). Po ověření překlopit v `STATUS.md`
      🔶 → ✅.
- [ ] **`stacktest yes`** → po restartu musí `status` ukázat `stack:UartTask` (#10). Audit zjistil,
      **proč hook při boot-loopu mlčel**: `configASSERT` shodil systém s vypnutými přerušeními dřív,
      než nastalo další přepnutí kontextu, a metoda 2 kontroluje vzorek právě tam. Detekce není
      rozbitá, jen ji šlo předběhnout — tenhle test (pomalé přetékání) ji spustit **má**.
- [ ] **Ověřit opravu per-core RCC na CM4** (#23) — `__HAL_RCC_C2_*` v `USER CODE BEGIN Init`.
      Projev by byl jen při low-power/ETH, takže stačí, že CM4 dál běží a `4:OK` svítí.

---

## P3 — nálezy z revize kódu (malé, nezávislé na HW)

- [x] ✅ **`s_busy` v `sd_export.c` — OPRAVENO 2026-08-13.** Audit našel dvě díry: `sd_export_selftest()`
      příznak **vůbec nenastavovala** (moje skriptovaná úprava tiše neproběhla) a `sd_export_run()`
      ho **nevyčistila na žádné chybové cestě** → jeden neúspěšný export natrvalo vypnul auto-unmount.
      Místo záplatování ~10 návratových cest je tělo vyčleněné (`selftest_body`/`export_body`) a
      příznak se nastavuje jen ve wrapperu → ta chyba nemůže vzniknout znovu.
- [x] ✅ **Zámek `run_selftests()` — DOKONČEN 2026-08-13.** Byl přidán jen z poloviny (acquire bez
      release), takže by se testy spustily **jednou** a pak už jen vracely starý výsledek. Doplněno
      `s_running = 0` + varování, že funkce smí běžet **až za schedulerem** (před `vTaskStartScheduler`
      má port `uxCriticalNesting = 0xaaaaaaaa` → `taskEXIT_CRITICAL()` by přerušení už nepovolil).
- [x] ✅ **Třetí kopie `hsd1.Init` v `sd_probe()` smazána 2026-08-13** — byla dvojnásobně mrtvá
      (`DATALOG_SD_RAW_OK=0` ji nepustí dál a `main.c:263` handle stejně vyplní dřív), a přitom
      třetí místo k ručnímu srovnávání s `.ioc`.
- [x] ✅ **`get_fattime()` torn-read ošetřen 2026-08-13** — `g_rtc_text_local` přepisuje defaultTask
      bez zámku, takže čtení z UartTasku mohlo zastihnout půlku staré a půlku nové hodnoty
      (přes půlnoc razítko o den vedle). Zámek sem nejde (volá to FatFs zevnitř `f_write`), takže
      se čte dvakrát a musí se to shodnout.
- [x] ✅ **`scpi_selftest` teď řekne, KTERÝ assert spadl (2026-08-13).** Bylo ~90 kontrol slitých do
      jedné návratové hodnoty a bez nativního kompilátoru na PC se test nedá spustit jinde než na
      cíli → „FAIL" byl nedohledatelný. `selftest` nově vypíše i seznam failujících indexů a u SCPI
      `scpi.c:<řádek>` prvního neúspěšného assertu. **Odpovídá na dotaz „co SCPI parser, hlásí chybu
      při startu" — konkrétní příčinu ukáže až první flash.**

- [x] ✅ **SCPI selftest OPRAVEN 2026-08-13 — příčina nalezena čtením, ne hádáním.** Padal na tom,
      že po `CALC:NULL:ACQuire` čekal pořád absolutní hodnotu (`2*1e7+100`), jenže
      `meas_math_capture_null()` referenci **nejen zachytí, ale rovnou zapne relativní režim**
      → Y klesne na 0 a padly hned dva asserty (druhý čekal `MEAS_HI`, dostal `MEAS_LO`).
      **Chyba byla v očekávání testu, ne v parseru** — `meas_math_selftest` tutéž sémantiku
      očekává správně. Test teď ověřuje OBĚ větve (absolutní i relativní).
- [x] ✅ **Rozšíření SCPI 2026-08-13** — `SYST:CAP?`, `SYST:ERR:ALL?`, `STAT:PRES`, `SENS:FUNC?`,
      `SENS:ROSC:SOUR?/LOCK?`, `MEAS:PER?`, `MEAS:FREQ:STAL?`, `SYST:TEMP:ALL?`, `MEAS:VOLT:ALL?`.
      Vše **jen nad poli, která `scpi_src_t` už má** → žádný bump `IPC_VERSION` a CM4 se bude
      chovat identicky. Selftest rozšířen na 101 assertů.

- [x] ✅ **`_Static_assert` mezi `SCPI_CFG_*` a `IPC_CFG_*` — HOTOVO 2026-08-13.** Riziko potvrzeno:
      `ipc_cfg_apply()` v `ipc.c` je byte-za-byte duplikát `scpi_cfg_apply()` a klíč se přes cmd
      ring přenáší jako **holé číslo, bez převodu**. Ty dvě hlavičky se navíc nikdy nepotkaly
      v jedné translation unit, takže to nemohl odhalit žádný kompilátor. 8 assertů v `ipc.c`
      (`scpi.h` se tam includuje výhradně kvůli nim). **Ověřeno injektáží chyby** — po vložení
      položky do jednoho výčtu build spadne, po obnovení projde. Tím padá blokátor ETH etapy F6.
- [ ] **`ipc_cm4_cm7_alive()` hlásí „alive" při bootu**, než CM7 poprvé publikuje snapshot
      (`s_last_seq`/`snap.seq` obojí 0 → `(now-0) < 2000`). CM4 pak ~2 s servíruje nuly jako platná data.
- [ ] **Snapshot posílá napětí jako 0 místo „neplatné".** CM7 SCPI hlásí `9.91E37` (NaN), CM4 by
      hlásil `0.00 V` → rozdílná sémantika `MEAS:VOLT?` mezi USB a TCP. Chce validity bity → `IPC_VERSION` bump.
- [ ] **`scpi_process` dělá plný sweep senzorů i pro `*IDN?`** (10 senzorů, `gps_get` v kritické
      sekci, `fpga_freq_get_last` IRQ-off). Načítat líně nebo gatovat dle subsystému.

---

## P4 — připravené, nezačaté

- [ ] **ETH etapa F0** (`ETH_BRINGUP_CHECKLIST.md`): vytáhnout ze schématu piny `MDC`/`MDIO`/
      `ETH_RES` (v repu **nejsou**) + UART příkaz `eth` = bit-bang MDIO, scan adres 0–31, čtení
      PHY ID (`0x0007C130/1`) + změřit REF_CLK 50 MHz. **Bez regenerace `.ioc`, nulové riziko.**
- [ ] **#29 encoder** (`ENCODER_J7_NAVRH.md`): piny **potvrzené ze schématu** — `PA8`/`PA9` =
      TIM1 CH1/CH2, `PC13` = tlačítko, konektor J2. **Riziko „PA9 = USB VBUS" je vyvrácené**
      (USB je na PA11/PA12). Blokátor padl, jde to dělat kdykoli.
- [ ] 🔴 **Ověřit reset scope IWDG2** (`DUALCORE_BRINGUP_CHECKLIST.md` §8). Za celé ladění
      boot-loopu se `IWDG2RSTF` (bit 27) ani jednou neasertoval → pořád nevíme, jestli je
      per-core, nebo shodí celý systém.

---

## P5 — nové zadání

- [x] ✅ **Horizontální bargrafy: segmenty užší o 30 % — HOTOVO 2026-08-12** (okno PŘEHLED KANÁLŮ,
      `s_view=30`, `app_gpsdo.c`). Potvrzena varianta „užší segmenty".

  `HB_SEGS` **45 → 56** (jediná změněná konstanta — geometrie je parametrická, `hb_seg()` si
  `sw` dopočítá a `hb_marker_idx()` škáluje procenta stejně):

  | | segment | zabere | krok |
  |---|---|---|---|
  | dřív (45) | 8 px | 448/452 px | 2,22 % |
  | teď (56) | **6 px** | 446/452 px | **1,79 %** |

- [ ] **Ověřit bargrafy vizuálně na displeji.** `hb_marker_idx()` teď dělí jemněji → markery
      MIN/MAX/REF budou přesnější, ale mohou se víc překrývat s výplní. Zkontrolovat čitelnost
      (6 px segment + 2 px mezera je blízko hranici rozlišitelnosti — 8,54 px/mm, tj. segment
      ≈ 0,7 mm). Součást průchodu `HW_OVERENI_PRUCHOD.md` §2.
