# TODO — aktuální stav (2026-08-15)

> Přepsáno po dokončení **ověřovacího průchodu na HW** (`HW_OVERENI_PRUCHOD.md`).
> Předchozí verze byla z 2026-08-12 a z poloviny už neplatila.
> Čísla `#nn` odkazují na `STATUS.md`.

---

## ✅ Hotovo v relaci 2026-08-14/15

- **SD karta plně funkční.** Root cause „přenos nejede" = vypnutý `HardwareFlowControl`
  (+ CLKCR se nikdy nepřenastavil na transfer konfiguraci, protože obcházíme zaseklý
  `HAL_SD_ConfigWideBusOperation`). Odhalil to funkční referenční projekt `H757_SDcard_01`.
  Dále: 4-bit sběrnice (ACMD6 bez SCR čtení), 32 KB bloky, `f_expand` předalokace,
  okno SD KARTA (PŘIPOJIT/ODPOJIT, TEST s měřením rychlosti, EXPORT CSV, FORMAT s dvojím
  potvrzením), `sd init` kroková diagnostika `[a]..[e]`.
- **CM4 běží + IPC round-trip HW-ověřený.** „CM4 nebootuje / bank2 neflashnutá" byla
  **mylná diagnóza** — maskoval ho připojený debugger. Header ukazuje `CM4:xx%`
  (CM4 měří vlastní zátěž přes DWT).
- **HardFault vyřešen** — byl to leftover breakpoint sondy (`HFSR` bit31 DEBUGEVT),
  ne chyba kódu. Crash black-box nově ukládá i `LR` (odkud se skočilo) a `HFSR`.
- **Datalog omezen na 1/3 W25Q DATA regionu** (~80 dní místo ~242).
- **Ověřovací průchod 11 funkcí** (#31/#32/#33/#43/#44/#47/#52/#53/#54/#67/#68) → ✅.
- **#10 detekce přetečení zásobníku ověřena** (`stacktest yes` → `stack:UartTask`).
- **Audit:** `-Wall -Wextra -Wshadow` sweep 73 souborů = 0 varování, `-fanalyzer` čistý,
  žádný mrtvý kód. Opraveny 2 clear boxy pod baseline a **`FIL` ze stacku do .bss**
  (UartTask ušetřil ~1,1 kB).

---

## P0 — největší funkční dluh

- [ ] 🔴 **#2 FPGA SPI link** (`FPGA: link NOLINK`, symptom `RX0:FF` = FPGA nebudí MISO).
      **Blokuje všechno podstatné:** velké číslo na hlavní obrazovce je pořád SIMULACE,
      stejně tak trend/Allan/histogram/spektrogram/Math. Datalog zapisuje kmitočet 0.
      Diagnostika připravená: UART `fpgaraw`, `fpgaloop`, okno Čítač.

---

## P1 — odblokované, dá se dělat hned

- [x] ✅ **SCPI — USB strana kompletní 2026-08-15.** Doplněno: SET (`SENS:FREQ:GATE/CHAN`,
      `INIT`/`ABOR`/`READ?`), **STATus OPER/QUES** event+enable registry s latchováním hran
      + summary bity v `*STB?`, `DISP:BRIG`, `*OPT?`, `CONF:FREQ`.
- [ ] **SCPI — zbytek** (malé): `SYST:DATE`/`SYST:TIME` **set** — ⚠️ RTC registry vlastní
      VÝHRADNĚ defaultTask, takže to chce request-most jako `g_ui_cfg_req`; hodnotu to má jen
      v laboratoři bez GPS antény (jinak se RTC disciplinuje z GPS).
      ⏸ **Až s HW:** `TRIG:SOUR` + `*TRG`, `INP:COUP/IMP/ATT` (nemáme přepínatelný vstup —
      implementace by lhala), `CAL:*`, `FORM:DATA`.
- [x] ✅ **#55 screenshot** — `screenshot sd` ukládá `SHOTnnn.BMP` na kartu (anti-tearing přes
      SDRAM scratch, FatFs zapíše celý soubor). USB varianta zůstává pro běh bez karty.
- [x] ✅ **#67 okno MĚŘENÍ** — bylo hotové, chyběl jen vstupní bod; přidána dlaždice v Menu.
- [x] ✅ **#35 audit** — sweep 0 varování, `-fanalyzer` čistý, nálezy opraveny (viz STATUS).

- [ ] **#29 encoder** (`ENCODER_J7_NAVRH.md`): HW vrstva hotová (UART `enc`, TIM1 PA8/PA9 +
      tlačítko PC13). Chybí **model fokusu v UI** — návrhové rozhodnutí, ne kód.
- [ ] **SD drobnosti:** formát exportu (dnes `GPSDOnnn.CSV` přírůstkově — ověřit chování),
      auto-mount při vložení karty (dnes jen request-flag).
- [ ] **#68 autocal** — rozpracované: dnes jen verifikace guard-bandů (VREF/12V/5V/VBAT →
      PASS/WARN/FAIL). Staged kroky (ADC3 self-cal, timebase vs GPS, RF slope/intercept)
      čekají na #2 resp. externí RF referenci.

---

## P2 — čeká na hardware

- [ ] 🔴 **ETH → a s ním SCPI/TCP + web na CM4.** Blokované: PHY LAN8742A dostává
      **10 MHz místo 25** (X1 sdílený s HSE procesoru). Řešení = **výměna X1 za 25 MHz TCXO**,
      pak `CLOCK_25MHZ_MIGRACE.md` (VCO všech PLL zůstává, nic odvozeného se nemění),
      pak `eth` musí najít PHY na adrese 0. Do té doby ETH nezačínat.
      ⚠️ CM4 strana IPC je hotová a běží — chybí **jen** ten síťový stack.
- [ ] **SD rychlost (volitelné):** vyšší SDMMC takt (16 → 25/50 MHz) až po
      (a) sériovém tlumicím odporu ~22–33 Ω na CK a (b) bulk kondenzátoru 4,7–10 µF
      na SD VDD. Viz STATUS #69. Dnešní rychlost na export bohatě stačí.
- [ ] **RF bargraf** — ověřit až bude připojená plná FPGA deska.

---

## P3 — konfigurace / hygiena ✅ HOTOVO 2026-08-15

- [x] ✅ **CubeMX FatFs volby ověřeny** — `_USE_MKFS=1` i `_USE_EXPAND=1` jsou v `ffconf.h`
      a **přežily regeneraci**. `_USE_EXPAND` je zapsaný i v `.ioc` (`FATFS_M7._USE_EXPAND=1`);
      `_USE_MKFS` v `.ioc` být nemusí — je to default pro režim SD Card, což potvrdil regen.
      Kontrola po každém regenu zůstává v `CUBEMX_CHECKLIST.md`.
- [x] ✅ **HardFault pravidlo zdokumentováno** — naked `HardFault_Handler` je v kódu
      (`stm32h7xx_it.c:109`), helper `hard_fault_capture` v `USER CODE 0` (přežije regen),
      varování v `CUBEMX_CHECKLIST.md`. Není to úkol, ale trvalé pravidlo po každém Generate Code.
- [x] ✅ **`UI_SIZES.md` aktualizován** (8. vlna) — tři nová okna (DISPLEJ/SÍŤ/SD KARTA),
      `DG_CARD_FULL_TALL`, header `CM7:/CM4:`, nová sekce **„Svislá geometrie textu"** s tabulkou
      `line_height` per font a třemi pravidly (rozteč ≠ ascent; clear box nad baseline; header
      karty zabírá prvních ~30 px) — to je destilát 10 chyb z HW průchodu.
- [x] ✅ **Opraven nález z revize:** `DIM_MINUS`/`DIM_PLUS` (auto-dim prodleva) byly **56 px
      široké = 6,6 mm**, jediný dotykový cíl pod 7mm minimem. Rozšířeny na **64×64** (7,5 mm),
      místo vzato ze 74 px prázdna mezi nimi. **V UI teď není žádný cíl pod minimem** kromě
      pilulek v headeru, které limituje výška headeru (dokumentováno).

---

## Doporučené pořadí dál

1. **#2 FPGA SPI link** (P0) — jediná věc, po které přístroj začne skutečně měřit.
2. **#29 encoder** nebo rozpracované #55/#67/#68 (P1) — dá se dělat hned.
3. **ETH** (P2) — až bude osazený 25 MHz TCXO.
