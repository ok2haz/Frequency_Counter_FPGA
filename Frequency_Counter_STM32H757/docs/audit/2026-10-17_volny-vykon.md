# Na co využít volný výkon CM7 a CM4 (analýza 2026-10-17)

> **Fáze analýzy — kód se neměnil.** Nic z toho není rozhodnuto ani implementováno. Navazuje na STATUS #308 (nápady pro CM4),
> #307 (sloty spojení), #27 (MathTask), #303/#311 (holdover, drift) a na dnešní měření hrbolu ADEV
> (`docs/MERENI_A_KALIBRACE.md`, oddíl „Příčina hrbolu ADEV na 20–50 s").

## 1. Výchozí stav

**CPU (odečet uživatele 2026-10-17, během běhu loggeru 10 dotazů/s):** CM7 **60 % volno**, CM4 **80 % volno**.
⚠️ Volno na CM7 závisí na zobrazeném okně — dříve naměřeno UiTask 24 % (IDLE 67 %, STATUS #220) i UiTask 59 % (IDLE 33 %).
Pro plánování proto počítat s **~30 % v nejhorším případě**, ne s 60 %.

**Paměť** (`arm-none-eabi-size -A`, Release v0.19.0):

| Paměť | Obsazeno | Kapacita | Volno |
|---|---|---|---|
| CM7 flash bank 1 | 672 kB (`.text` 341 + `.rodata` 330 + ostatní) | 1024 kB | **~350 kB** |
| CM7 AXI SRAM (RAM_D1) | 283 kB (`.bss` 280 kB) | 512 kB | **~230 kB** |
| CM7 DTCM / ITCM | nic | 128 / 64 kB | **celé** |
| CM7 SRAM1 (RAM_D2) | nic (jen diagnostika `ram write`) | 128 kB | celé |
| SDRAM | FB 3 MB, scratch 4 MB, `.sdram` 1,1 MB z 4 MB, `.measlog` 8 MB | 32 MB | **8 MB nad logem** + ~2,9 MB v `.sdram` |
| CM4 flash bank 2 | 282 kB | 1024 kB | **~740 kB** |
| CM4 SRAM2 | 90 kB (`.bss` 88 kB) | 128 kB | **~38 kB** |
| FreeRTOS heap CM7 | ? (stacky tasků + objekty) | 32 kB (`FreeRTOSConfig.h:73`) | ⬜ změřit `status` → heap free/min |

**Skutečné stropy nejsou CPU:**
- **CM4 = RAM** (38 kB; jeden HTTP slot stojí 8,4 kB) a **chybí double FPU** (statistika v `double` by běžela softwarově).
- **I2C1 50 kHz** — dither DAC 200 Hz by zabral ~16 % sběrnice (#277) a bez RC před bufferem nemá smysl (HW).
- **FPGA CLS 85 %** — nová logika tam prakticky není.
- **SPI pull/ACK + hradlo** — FPGA nepotvrzené měření nedrží, takže zpoždění FpgaTasku > 250 ms = ztracené měření.
- **Pravidlo spinu ~10 ms** v hlídaných taskech (defaultTask, UiTask, FpgaTask).

## 2. Jak nezasáhnout do měření (podmínka pro každý návrh)

1. **Nesahat na měřicí cestu:** FpgaTask a SPI2, `gpsdo_tick()` (`freertos_task_fpga.c:544`, defaultTask 100 Hz, spolu s
   `watchdog_supervise`), `ad5693_servo()` (`freertos_task_sensors.c:361`, I2C1). Nová práce do nich **nepatří**.
2. **Nová práce na CM7 = vlastní task s prioritou NIŽŠÍ než UiTask** (BelowNormal) — tedy Low/idle+1, s `osDelay` mezi
   dávkami. Nemůže pak vyhladovět nic hlídaného watchdogem ani měření. Stack ze sdíleného heapu 32 kB → nejdřív změřit volný heap.
   Implementace do souboru, který už je v buildu (nový `.c` potřebuje Close → Open Project).
3. **Sdílené zdroje jen krátce:** W25Q přes `qspiMutexHandle` — datalog čeká jen 10 ms a jinak vzorek zahodí
   (`write_errors`), takže čtenář musí číst po malých dávkách a mutex pouštět. SDRAM log (`sdram_log`) je lock-free SPSC,
   čtení producenta (FpgaTask) neblokuje.
4. **Ověření A/B (stejné jako dnešní pokus s hradlem):** před a po změně stejný běh loggeru; kritéria `status` → `SEQ FPGA:
   diry 0`, zmeškaná měření v CSV (skoky `seq_meas`) beze změny, ADEV 1–500 s v rámci nejistoty, `stats` volno ≥ 20 %.
   ⬜ HYPOTÉZA k vyloučení: vyšší aktivita CPU/SDRAM ruší přes napájení desku FPGA/OCXO — A/B běh to ukáže.

## 3. Návrhy seřazené podle přínosu pro měření

### A. Analýza surové řady v přístroji (CM7, nízká priorita) — **doporučeno první**
`sdram_log` drží každé měření (8 MB / 32 B = 262 144 vzorků ≈ **18 h při 4 měřeních/s**), ale **konzument není napojený**
(CLAUDE.md, „Datová cache měření"). Volný CPU stačí na:
- **přesné overlapping ADEV/MDEV/TOTDEV přes všechna τ** z plné řady (dnes jen pyramida s ringem 60, `screen_main.c:1455`,
  která saturuje n = 60 a ve free-runu míchala data dvou režimů);
- **spektrum Sy(f) (Welch, FFT 4k–16k bodů)** — hrbol 30–300 s by byl vidět přímo na displeji/webu, dnes jsem ho počítal na PC.
Měření se nedotkne (jen čte ring). Cena: RAM pro FFT ~128–256 kB (DTCM/SRAM1 jsou volné), CPU desítky ms na výpočet 1×/min.

### B. „Tři rohy" (three-cornered hat) přímo v přístroji (CM7)
Přístroj už měří **současně dvě nezávislé dvojice**: OCXO↔externí reference (čítač) a OCXO↔GPS 1PPS (smyčka, `e_ns`).
Třetí rozdíl (externí↔GPS) se dopočte. Ze tří ADEV jde odhadnout šum každého zdroje zvlášť:
σ²_OCXO = ½(σ²_OCXO–ext + σ²_OCXO–GPS − σ²_ext–GPS). Odpověděl by trvale na dnešní otázku „čí je hrbol" bez PC.
Cena: 2 další pyramidy ~6 kB každá (RAM_D1). ⚠️ Platí jen tam, kde jsou šumy nezávislé: externí GPSDO i náš GPS jsou vázané
na GPS, takže na dlouhých τ (≳ 1000 s) korelují a odhad může vyjít záporný — zobrazovat jen τ, kde vyjde kladný.

### C. Doporučení časové konstanty smyčky z měření (CM7)
Z B vyjde σ_OCXO(τ) a σ_1PPS(τ); optimální τ smyčky je jejich průsečík. Dnes je τ pevně 1000 s. **Jen zobrazit doporučení**
(„doporučené τ = X s"), automaticky smyčku neměnit — to je zásah do disciplinace a patří k rozhodnutí uživatele.

### D. Model driftu a teploty pro holdover (#303, #311) (CM7, 1×/h)
Datalog od v0.19.0 nese kód DAC. Regrese kód ~ čas + T_ocxo + T_deska nad posledními dny (8 640 záznamů/den) v tasku s
nízkou prioritou, W25Q po dávkách (bod 2.3). Výstup: stárnutí D a tempco s nejistotou → podklad pro tabulku holdoveru.
⚠️ Podle rozhodnutí uživatele (#303 b) se **odhad holdoveru nezobrazí, dokud D není změřené** — tady by se jen počítal.

### E. CM4: export dat po síti místo pollování loggerem
Dnešní logger musí deskou cvičit 10×/s přes HTTP, aby dostal každé měření (a sám tím přidává provoz). Lepší:
- **`GET /api/raw?from=seq`** — dávka surových měření ze `sdram_log` přes IPC kanál (vzor `ipc_datalog_xfer_t`);
  PC si stáhne vše za poslední hodiny jedním dotazem;
- **`/metrics` (Prometheus)** a **`/api/gpsdo/history`** (2h historie smyčky) — #308 (2), (3);
- **lehké SSE sloty** (#307 a) — 1 kB místo 8,4 kB.
RAM CM4 (38 kB) stačí na dávkový přenos po kouscích, ne na buffer celé řady.

### F. CM4: NTP server (UDP 123) — #308 (1)
Užitečné pro síť, ale přesnost omezí NMEA (ms), ne 1PPS. Pro měření přínos nulový; na RAM levné.

### G. Uvolnění FPGA (CLS 85 %) přesunem výpočtu na STM — ⬜ HYPOTÉZA
Kdyby regresní blok FPGA posílal jen surové součty a dělení/normalizaci dělal CM7, mohlo by se uvolnit místo pro TDC kanál
1PPS (#262: 10 ns → ~50 ps). Přínos je ale malý: šum 1PPS po korekci sawtooth je ~8 ns (rozptyl 63 ns²), kvantizace
10 ns přispívá σ 2,9 ns. Nejdřív zjistit `sim/res_breakdown.py`, co v regresním bloku stojí nejvíc.

### Nedoporučeno
- **Přesun ADEV/statistiky na CM4** — nemá double FPU, výhoda nulová (#308).
- **VXI-11/HiSLIP, PTP** — velká práce, malý přínos pro měření (#308 (5), (6)).
- **Rychlejší dither DAC (#277)** — CPU je dost, chybí sběrnice I2C1 a RC na desce.

## 4. Doporučené pořadí (k rozhodnutí uživatele)
1. **Změřit rozpočet:** `stats` a `status` (heap free/min) ve dvou oknech (hlavní obrazovka, ALLAN) — až po skončení běhu loggeru.
2. **A** (surová řada → ADEV + spektrum) — největší diagnostický přínos, jen čte.
3. **B** (tři rohy) — staví na A, řeší původ hrbolu trvale.
4. **E** (`/api/raw`) — zbaví měření závislosti na PC loggeru.
5. **D**, pak **C**.
