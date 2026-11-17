# Audit: senzory / drivery periferií (modul 18)  (2026-09-18)

- **Commit:** `3912578` (HEAD větve `audit/2026-09-09-hodiny-pwr`)
- **Jádro / doména:** CM7. Sběrnice: `si5356`/`ads1115` na **I2C1** (D2), `ws_panel`/`ft5x06`
  na **I2C4** (D3). `sensor_hist` = jen RAM (AXI SRAM D1), žádná periferie.
- **Verze HAL:** STM32Cube FW_H7 V1.13.0 (dle CLAUDE.md / lwIP zdroje).
- **Soubory (1004 ř. vč. hlaviček):**
  `si5356.c` (148) + `.h` (90), `ads1115.c` (38) + `.h` (53),
  `ws_panel.c` (110) + `.h` (77), `ft5x06.c` (106) + `.h` (75),
  `sensor_hist.c` (115) + `.h` (44).
- **Projité sekce checklistu:** A (hodiny periferií — jen okrajově, časování I2C je
  modul 5), C (DMA/cache — **N/A, žádné DMA**, doloženo níže), D (souběh/RTOS),
  E (ošetření chyb, timeouty), G (I2C, ADC).
- **Neprojité (a proč):** B (dual-core — moduly běží jen na CM7), F (Flash —
  nepracují s ní), H (errata — bez revize silikonu, řešeno v modulu 1).

## Souhrn

Pět zralých ovladačů: clock generator Si5356A, ADC ADS1115, panelový ATTINY,
dotykový FT5x06 a RAM decimační pyramida senzorů. **Kód je čistý** — všechny
čekací smyčky mají timeout (50/100 ms), parsování FT5x06 je bezpečné a ohraničené,
ADS i sensor_hist matematika bez přetečení, buffery nejsou v DTCM a **modul
nepoužívá jediný DMA přenos** (vše je blokující `HAL_I2C_*`), takže celá sekce C
checklistu odpadá. Dva nálezy jsou robustnostní a leží v **chybových cestách**:
Si5356 init nekontroluje návratové hodnoty apply-sekvence (F-0113) a panelový
helper porušuje vlastní dokumentovaný slib „žádný printf z hlídaného UiTasku"
(F-0114). **Verdikt: funkční** — normální provozní cesta je správná, nálezy se
projeví jen při selhání I2C.

---

### F-0113 [S3] `si5356_init` nekontroluje návratové hodnoty apply-sekvence; selhání zápisu „výstupy ON" nechá hodiny vypnuté, ale init hlásí OK

- **Místo:** `CM7/Core/Src/si5356.c:130-147` (funkce `si5356_init`)
- **Popis:** Krok 1 (hromadný zápis register mapy, `:127-128`) sleduje úspěch do
  `ok`. Krok 2 — vlastní **apply procedura** SiLabs (OEB off → E2 pulse → SOFT_RESET
  → OEB on, `:131-137`) — volá `wr_masked` **bez jediné kontroly návratu**. `ok`
  vrácené funkcí tedy odráží jen krok 1. Poslední zápis `wr_masked(REG_OEB_ALL,
  0x00, 0x10)` (`:137`) je ten, který **zapíná 4× 100 MHz výstupy**.
- **Důkaz:** `:131-137` — sedm volání `wr_masked`, žádné v `if`/`ok &=`. Návratový
  typ `wr_masked` je `bool` (`:85`) a chyby propaguje (`:90` `return false`), takže
  informace je zahozena, ne nedostupná. Status read `:140` čte reg 218 (lock bity
  LOS_CLKIN/PLL_LOL) — ty se týkají **vstupu a PLL**, ne stavu výstupních bufferů
  (`OEB_ALL`), takže vypnuté výstupy z něj nepoznáš.
- **Dopad:** NACK na kterémkoli kroku apply (zvlášť na `:137`) → Si5356 negeneruje
  výstup → **FPGA čítač nemá časovou základnu** (4-fázový TDC ref), přitom
  `si5356_init` vrátí `true` a vytištěný status vypadá zdravě (`zapisy=OK`,
  `status218` bez LOS/LOL, protože 10 MHz vstup i PLL žijí). Přístroj neměří a
  hlásí „v pořádku". Dopad = celý přístroj (měření), ne jen modul. Deterministické
  při daném NACKu; NACK sám je občasný (I2C1 sdílí SensorsTask + Si5356 + TMP117).
- **Reprodukce:** HYPOTÉZA — vynutit NACK na zápisu OEB (kolize na sběrnici při
  bootu) a sledovat FPGA `RX0:FF`/mrtvý link, zatímco UART `si5356` hlásí OK.
  Staticky doložitelné z kódu.
- **Návrh opravy:** minimálně akumulovat návraty apply-kroků do `ok`
  (`ok &= wr_masked(...)` u všech pěti aktivních zápisů — E2 pulse i OEB), aby se
  selhání promítlo do návratové hodnoty i do řádku `zapisy=`. Volitelně tvrději:
  po OEB-enable přečíst zpět a ověřit (L-0055 „hodnotu, kterou HW nemusí přijmout,
  přečti zpět"). Init běží 1× před schedulerem, takže `HAL_Delay`/čtení navíc nevadí.
- **Riziko opravy:** nízké — přidání kontrol návratu nemění happy-path chování.
  ⚠️ Nesmí spadnout do `Error_Handler` (chybějící reference řeší SYS pilulka za
  běhu, ne boot) — jen se musí přestat lhát v návratové hodnotě.
- **Vztah k lekcím:** `L-0003` (ignorovaný návrat HAL — první konkrétní incident),
  `L-0028` (obrana/údaj, který kód neposkytuje).
- **Stav:** opraveno 2026-09-18 v `f128b59`. Přesně podle návrhu (akumulace
  návratu apply-kroků do `ok`); tvrdší varianta (readback OEB) nebyla nutná —
  volající návrat stejně jen loguje, takže stačí, aby přestal lhát. ⬜ neověřeno na HW.

---

### F-0114 [S3] Sdílený `ws_write_reg` tiskne `printf` i pro dva „runtime" volající, kteří v komentáři slibují opak (hlídaný UiTask)

- **Místo:** `CM7/Core/Src/ws_panel.c:12-23` (`ws_write_reg`), volané z
  `ws_panel_set_backlight` (`:104-110`) a `ws_panel_set_portc` (`:99-102`);
  místo volání backlightu `CM7/Core/Src/freertos_task_ui.c:492` je uvnitř
  `vTaskSuspendAll()`/`xTaskResumeAll()` (`:491-493`).
- **Popis:** `ws_write_reg` na chybové cestě volá `printf(...)` (`:18-20`). Oba
  runtime settery ho volají, přitom jejich komentáře výslovně tvrdí opak:
  `:98` „Bez printf … volá to hlídaný UiTask", `:106-108` „ŽÁDNÝ printf zde …
  chyba zápisu se projeví návratovou hodnotou". Slib je nepravdivý — chybová
  cesta helperu printf udělá.
- **Důkaz:** `ws_panel.c:17-21` (printf ve větvi `st != HAL_OK`). `_write`
  (`main.c:732`) sice bere mutex jen když `osKernelGetState() == osKernelRunning`
  — pod `vTaskSuspendAll()` vrací CMSIS `osKernelLocked`, takže se mutex
  **přeskočí** — ale pak běží blokující `HAL_UART_Transmit` s timeoutem 100 ms/řádek
  (`main.c:712-749`) **se zastaveným schedulerem** (backlight je mezi
  `vTaskSuspendAll`/`xTaskResumeAll`, `freertos_task_ui.c:491-493`).
- **Dopad:** Při selhání zápisu jasu (přesně dokumentovaný scénář umírající I2C4)
  se z UiTasku provede printf → na cestě USART1 systémový stall až ~100 ms se
  zastaveným schedulerem. Ohraničené (pod 2,5 s watchdogem), **nezhavaruje**, ale
  ruší deklarovaný záměr držet hlídaný UiTask bez printf (projekt tuhle třídu bere
  vážně — printf v malém tasku už jednou způsobil freeze). `ws_panel_set_portc` je
  dnes mrtvý kód (`I2C4_RECOVERY_TOUCHES_ATTINY == 0`), takže reálně jde o backlight.
- **Reprodukce:** HYPOTÉZA — spustit auto-dim (změna cíle jasu), zatímco 0x45
  NACKuje na I2C4 → jeden neúspěšný zápis → printf. Že helper printf dělá, je
  staticky zřejmé.
- **Návrh opravy:** dát dvěma runtime setterům printf-free cestu (např. interní
  `ws_write_reg_quiet`), nebo printf z `ws_write_reg` odstranit a nechat ho jen
  ve `ws_panel_probe`/`ws_panel_power_on` (které běží před schedulerem a log chtějí).
  Nízké riziko.
- **Riziko opravy:** nízké — dotýká se jen chybové větve.
- **Vztah k lekcím:** `L-0028` (komentář popisuje obranu/vlastnost, kterou kód
  nemá — opakovaný výskyt), `L-0018` (jeden zdroj pravdy místo dvou), okrajově
  `L-0013` (co smí běžet v hlídaném/kritickém kontextu).
- **Stav:** opraveno 2026-09-18 v `662e049`. Opraveno jinak, než návrh nabízel:
  místo samostatného `ws_write_reg_quiet` sjednoceno do `ws_write_reg_ex(…, log)`
  (jeden přenos, parametr `log`) — per L-0018 lepší než dvě kopie HAL volání.
  ⬜ neověřeno na HW.

---

## Co bylo zkontrolováno a je v pořádku

- **Žádné DMA v celém modulu** → sekce C checklistu N/A. Všechny přenosy jsou
  blokující `HAL_I2C_Master_Transmit/Receive`, `HAL_I2C_Mem_Read/Write`,
  `HAL_I2C_IsDeviceReady`. Žádná cache maintenance není potřeba, žádný buffer
  v DTCM (`L-0001`/`L-0002` N/A). Ověřeno v `CM7/Release/H757_LED_CM7.map`:
  `s_hist` @ `0x2401a104` (AXI SRAM D1), 0x3de0 = 15840 B; `s_pre`/`s_div`/
  `s_pre_n` tamtéž; `REGMAP` v `.rodata` (flash). Nic v `0x2000_0000` (DTCM).
- **Všechny čekací smyčky mají timeout** (checklist E): `si5356` 50 ms, `ads1115`
  50 ms, `ws_panel`/`ft5x06` 100 ms, `IsDeviceReady` 3 pokusy × 100 ms. Žádný
  `while(!(REG&FLAG))` bez meze.
- **Návratové hodnoty HAL** (`L-0003`): `ads1115_start/read_raw` — kontrolováno;
  `ft5x06_probe/read_touch` — kontrolováno; `ws_panel_probe/power_on` —
  kontrolováno; `si5356` krok 1 — kontrolováno. Výjimky = F-0113, F-0114.
- **`ads1115_start` config bity** (checklist G/ADC): OS(15)=1, MUX=`0x04+ch`
  (single-ended AINch vs GND), PGA<<9, MODE(8)=1 single-shot, DR=`4<<5` (128 SPS),
  COMP_QUE=0x03 (disable). Odpovídá datasheetu. Guard `ch>3 || pga>PGA_0V256`
  (`:13`) chrání proti neplatnému kanálu/PGA.
- **`ads1115_raw_to_mv` přetečení** (`ads1115.h:48-51`): `raw(±32767) × fs_mv(≤6144)`
  = ≤2,01e8, bezpečně v `int32`. Dělení `/32768` exaktní. Bez přetečení.
- **`ft5x06_parse` bezpečnost** (`ft5x06.c:69-91`): čte `frame[0..4]`,
  `FT5X06_FRAME_LEN = 31` (`ft5x06.h:29`) → žádné OOB. `num == 0 || num > 5`
  (`:78`) odmítá prázdný i sentinel `0x0F`. Offsety XH/XL/YH/YL sedí na registry
  0x02–0x06. Celý rámec se čte (`:99-100`) → nezasekne controller (dokumentovaná
  past 1B probe).
- **`sensor_hist` matematika:** `hist_res` max = `BASE_S(2) × DECIM(4)^3` = 128,
  `RING(96) × 128` = 12288 — `hist_pick` navíc počítá v `int64` (`:71`), bez
  přetečení. `hist_at` idx = `(head-1-age+2·RING) % RING` je vždy v `[0,RING)`
  bez ohledu na `head` → **žádný OOB** ani při souběžném posunu `head`.
  `s_pre_n` (uint16) roste max `BASE_S·2` = 4 na okno → bez přetečení.
- **`si5356` fázové registry:** hodnoty 704/1408/2112 LSB = 2,5/5,0/7,5 ns @100 MHz
  = 90/180/270° (`:24-27, :53-55`) — odpovídá požadavku „musí být 90°, ne 45°"
  z CLAUDE.md. `wr_masked` respektuje `mask==0x00` (přeskočit) / `0xFF` (přímý) /
  `<0xFF` (RMW), shodně s pravidlem CBPro exportu.
- **`si5356` sticky (reg 247):** `clear_sticky` maže zápisem nuly (`v & ~mask`),
  ostatní bity ponechá — odpovídá AN565. `read_sticky`/`read_status` bez přepínání
  stránky (chip zůstává na page 0 po init) — dokumentováno a ověřeno provozem
  (`status 0x04`).
- **Kontext vláken / mutex:** zápis na I2C1 (`si5356_init` před schedulerem,
  `clear_sticky` v SensorsTask) i I2C4 (backlight/portc/touch v UiTask, vše pod
  `i2c4MutexHandle`) je jednoznačný a odpovídá tabulce v CLAUDE.md. Drivery jsou
  bezstavové vůči sběrnici (mutex drží volající) — správně.
- **Časově-derivované konstanty (`L-0006`):** modul žádné nemá — I2C `TIMINGR`
  je v `i2c.c` (modul 5), ADS 128 SPS a Si5356 100 MHz jsou interní vlastnosti
  čipů, ne odvozené z MCU hodin.

## Nezkontrolováno / omezení tohoto běhu

- **Souběh `sensor_hist` čtenář (UiTask) × zapisovatel (SensorsTask):** header
  (`sensor_hist.h:10-13`) souběh **výslovně dokumentuje a toleruje** („bez zámku …
  roztržené čtení tolerováno", stejně jako `g_sensors[]`). Drobný přesah proti té
  toleranci: `hist_at` čte `head` živě v každé iteraci smyčky `sensor_hist_series`
  (`:78`), takže feed uprostřed renderu může posunout množinu vzorků o 1 (ne jen
  roztrhnout jednu hodnotu). Je to **jen kreslení** (okno GRAFY, refresh ~1×/s) a
  příští snímek se opraví → **není to nález**, jen poznámka pro úplnost; čisté
  řešení by bylo snapshot `head` před smyčkou. Bez HW se vizuální dopad neměří.
- **`ws_read_reg` (`:26-39`) obsahuje `HAL_Delay(1)`** mezi transmit a receive.
  Dnes ho volá **jen** `ws_panel_probe` (před schedulerem), kde je `HAL_Delay`
  v pořádku → **není to nález**. Latentní past jen kdyby ho někdo zavolal za běhu
  z hlídaného tasku; dnes se to neděje.
- **Skutečné časování I2C / integrita signálu / teplotní chování** nelze rozhodnout
  staticky (moduly běží proti reálným čipům). Chybovost podle taktu je změřená
  v modulu 5 (`docs/audit/2026-09-10_i2c.md`).
- **`si5356` REGMAP správnost** je přijata jako CBPro export (nelze staticky ověřit
  proti VCO/PLL matematice bez CBPro projektu); zkontrolována jen konzistence
  formátu, fázových offsetů a apply procedury.
