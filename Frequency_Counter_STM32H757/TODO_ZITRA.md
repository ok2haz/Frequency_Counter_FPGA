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
- [x] ✅ **SCPI USB strana KOMPLETNÍ 2026-08-15** — doplněno i `SYST:DATE`/`SYST:TIME` (SET přes
      request-most do defaultTasku, aplikuje se před GPS syncem, ručně zadaný čas se netváří
      jako disciplinovaný).
      ⏸ **Až s HW:** `TRIG:SOUR` + `*TRG`, `INP:COUP/IMP/ATT` (nemáme přepínatelný vstup —
      implementace by lhala), `CAL:*`, `FORM:DATA`. TCP varianta ⬅ CM4/ETH.
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

## 🔜 NA ZÍTRA — zrychlit SD (HW úprava + jedna konstanta)

**Zjištění 2026-08-15:** Frantův projekt `H757_SDcard_01` jede na **48 MHz** (`ClockDiv = 0`
= bypass děličky, kernel 48 MHz) — a to na **TÉŽE desce**. Tím padá dosavadní obava
ze STATUS #69, že deska vyšší takt nesnese; empiricky ho snese.
⚠️ Můj dřívější závěr „Franta jede 12 MHz, brát si od něj nemáme co" byl **chybný** —
četl jsem starší lokální kopii, ne aktuální GitHub.

### 1) HW úpravy — ✅ HOTOVO 2026-08-16
- [x] ✅ **R60 odstraněn** (pull-up na `SDMMC1_CK`, patřil na CMD dle erratum desky).
- [x] ✅ **Bulk kondenzátor na SD VDD navýšen na 10 µF** (byl jen C75 100n).
- [x] ✅ **R55 odstraněn** (1,5 kΩ pull-up z +3V3 na D+) — STM32H7 má v OTG_FS PHY vlastní
      interní pull-up řízený `USBD_Start()`. Externí byl navíc a hlásil hostu „připojeno"
      hned po zapnutí, tedy ~2,5 s předtím, než firmware USB inicializuje → Windows to
      odbylo jako „Unknown USB Device" a **nepomohl ani reset desky** (pull-up byl natvrdo).
      Teď se USB připojí, až je firmware připravený.

### 2) SW — ✅ NASTAVENO 2026-08-16 (čeká na ověření na HW)
- [x] ✅ **`ClockDiv` 2 → 1** = `SDMMC_CK` **16 → 32 MHz**. Srovnáno na všech třech místech:
      `.ioc`, USER CODE v `sdmmc.c` a `sd_apply_init_config()` v `sd_export.c`.
      ⚠️ Při té příležitosti opraveno, že `sdmmc.c` měl `HWFC = DISABLE` (konfigurace, se
      kterou datová cesta vůbec nejede) — zachraňoval nás jen přepis v `BSP_SD_Init()`.
      ⚠️ **`ClockDiv = 0` u nás NEPŘEVZÍT:** máme kernel **64 MHz** (Franta 48), takže bypass
      by dal **64 MHz** = nad SD High-Speed limitem 50 MHz. `ClockDiv=1` → 32 MHz je bezpečně
      pod ním a pod hodnotou, kterou Franta na stejném HW prokázal.
- [ ] **Ověřit:** `sd diag` (musí hlásit `4-bit, SDMMC_CK 32.000 MHz`, ne fallback) →
      `sd test` (rychlost zápisu/čtení — dnes 0,43 / 1,32 MB/s) → `sd export`.
      Při chybách CRC/timeoutu vrátit na 2 a řešit HW.

---

## P2 — čeká na hardware

- [ ] 🔴 **ETH → a s ním SCPI/TCP + web na CM4.** Blokované: PHY LAN8742A dostává
      **10 MHz místo 25** (X1 sdílený s HSE procesoru). Řešení = **výměna X1 za 25 MHz TCXO**,
      pak `CLOCK_25MHZ_MIGRACE.md` (VCO všech PLL zůstává, nic odvozeného se nemění),
      pak `eth` musí najít PHY na adrese 0. Do té doby ETH nezačínat.
      ⚠️ CM4 strana IPC je hotová a běží — chybí **jen** ten síťový stack.
- [x] ➡️ **SD rychlost — přesunuto nahoru na „NA ZÍTRA"** (máme důkaz z Frantova projektu,
      že deska vyšší takt zvládne; čeká jen na odstranění R60 + bulk kondík).
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
