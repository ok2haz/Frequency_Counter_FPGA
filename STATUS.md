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
| 6 | Datalogging stability do W25Q DATA regionu (~32 B/10 s → ~600 dní) | STM32 | ⬅ potřebuje reálná data (#1) |
| 7 | Reálná kalibrace RF (AD8307 slope/intercept) → CALIB store | STM32 | **infrastruktura HOTOVÁ** (`calib.c/h`, okno Kalibrace, persist do W25Q) — zbývá jen **změřit skutečné hodnoty** na HW a zadat je |
| 8 | **Si5356 „LOS_CLKIN" — VYŘEŠENO 2026-07-18: byla to FW záměna bitů, HW je V POŘÁDKU.** Kritická validace odhalila, že reg 218 má dle **AN565**: bit2 = **LOS_XTAL**, bit3 = **LOS_CLKIN** (FW měl bit2 mylně jako LOS_CLKIN). Pozorovaný status `0x04` = bit2 = LOS_XTAL — krystal XA/XB na desce **není osazen** (piny uzemněné dle datasheetu) → bit je **trvale 1 a benigní**. Skutečný LOS_CLKIN (bit3) = **0** → **TTL buzení CLKIN detektoru plně vyhovuje** (dřívější teorie „TTL 2,4 V < VIH 2,64 V" tímto empiricky vyvrácena — VIH je garanční mez, ne skutečný práh detektoru). Důkazy (4 nezávislé): AN565 tab. reg 218 + datasheet Fig. 7 (změřené sloupce) + schéma XA/XB→GND (#PWR086) + CBPro mapa `{6,0x04,0x1D}` maskuje přesně bit2 (LOS_XTAL) interrupt. **FW opraveno**: definy bit2/bit3, LOS_CLKIN(bit3)=ČERVENÁ (⚠️ AN565: **PLL_LOL se při fyzické ztrátě vstupu neasertuje** — bit3 je hlavní indikátor ztráty reference), bit2 ignorován. **Žádná HW změna není potřeba** — LMK1C1104/LVC1G17 bezpředmětné pro LOS (s bufferem by 0x04 svítil dál!). **Verifikace po flashi**: odpojit 10 MHz → status `0x08/0x0C`+červené „LOS CLKIN!", připojit → `0x04`+zelené „LOCK OK". Volitelná hygiena: scope-check overshoot ≤3,63 V na pinu 4. (Pozn. trvale platné: 74ACT/HCT jen 4,5–5,5 V.) | — vyřešeno (FW) | flash + 1min verifikace |

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
`PHASE_CAL_DESIGN.md` · `FPGA_module_schematic.pdf` · `src/` (RTL) · `sim/` (testbenche).
