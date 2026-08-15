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

- [ ] **#29 encoder** (`ENCODER_J7_NAVRH.md`): HW vrstva hotová (UART `enc`, TIM1 PA8/PA9 +
      tlačítko PC13). Chybí **model fokusu v UI** — návrhové rozhodnutí, ne kód.
- [ ] **SD drobnosti:** formát exportu (dnes `GPSDOnnn.CSV` přírůstkově — ověřit chování),
      auto-mount při vložení karty (dnes jen request-flag).
- [ ] **#55 screenshot** (front FB → BMP přes USB CDC), **#67 okno prezentace měření**,
      **#68 autocal** — rozpracované.

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

## P3 — konfigurace / hygiena

- [ ] **CubeMX:** doplnit `USE_MKFS`/`USE_EXPAND` do FATFS Advanced (USE_EXPAND už v `.ioc`
      je, MKFS je default) — viz `CUBEMX_CHECKLIST.md`.
- [ ] ⚠️ **Po každém Generate Code vrátit naked `HardFault_Handler`** v `stm32h7xx_it.c`
      (regen ho přepíše na prázdný `while(1)`; helper v USER CODE 0 přežije).
- [ ] **Vizuální doladění** zbytku UI podle `UI_SIZES.md` (prvky pod 7 mm dotykovým cílem).
