# CRC v HW a SPI přes DMA (STM32H757), mrtvé moduly — zadání pro Claude CLI — 2026-10-20

Fáze: **kód napsaný a otestovaný na hostu; na desce NEověřený.** Výchozí nastavení nemění chování firmwaru
(`FPGA_CRC_USE_HW = 0`, `FPGA_SPI_USE_DMA = 0`). Tento dokument popisuje, co je hotové a jisté, co musí dokončit CLI
(závisí na projektu CubeMX, linker skriptu a druhém jádru) a jak to ověřit.

Souvisí s: `2026-10-20_E0_implementace_zadani_CLI.md` (E0, FW 0x0425), `2026-10-20_v3_navrh_a_analyzy.md` (rámec 1024 B).

---

## 1. Přehled

| část | stav | kdo |
|---|---|---|
| `fpga_crc.c/.h` — SW tabulka + HW jednotka CRC s pojistkami | hotovo, host testy prošly | CLI: ověřit, že CRC jednotku nepoužívá nic jiného (ani CM4), zapnout, ověřit na desce |
| `fpga_spi_dma.c/.h` — přenos datové fáze přes DMA | logika hotová, host testy prošly | CLI: CubeMX (DMA, NVIC), sekce linkeru, callbacky, zapnout, ověřit na desce |
| `fpga_freq.c` — napojení obou modulů | hotovo (za přepínači) | — |
| mrtvé moduly ve `spi_app.v` a mimo `.gprj` | jen návod | CLI (kap. 5) |

**Přínos:**
- **v2 (rámec 128 B):** malý — DMA uvolní CPU na ~65 µs z každého čtení (~0,13 % CM7 při 20 Hz), HW CRC ~1 µs.
- **v3 (rámec 1024 B):** podstatný — přenos 655 µs (12,5 MHz) neblokuje CPU, CRC 1024 B bez CPU.
- Proto teď jde o **přípravu na v3**, ověřenou už na dnešním protokolu.

---

## 2. Soubory

| soubor | co | poznámka |
|---|---|---|
| `stm/fpga_crc.h`, `stm/fpga_crc.c` | CRC-16/CCITT-FALSE: SW (půlbajtová tabulka, 32 B) a volitelně HW jednotka | **nový soubor → přidat do projektu STM** |
| `stm/fpga_spi_dma.h`, `stm/fpga_spi_dma.c` | datová fáze SPI2 přes DMA s pojistkami | **přidat do projektu až při zapnutí DMA** (kap. 4.5); bez definice `FPGA_DMA_SECTION` se záměrně nepřeloží |
| `stm/fpga_freq.c` | `fpga_crc_init()` v `fpga_freq_init`; kontrola CRC přijatého rámce (3 místa) přes `fpga_crc16_check`; v `xfer_raw` větev DMA za `FPGA_SPI_USE_DMA` | sestavování TX rámce (`build_frame`) zůstává na SW CRC |
| `test/t_crc_hw.c`, `test/t_dma.c` (+ `.out`) | host testy obou modulů | kap. 6 |

---

## 3. CRC v hardwaru (`fpga_crc`)

### 3.1 Co je hotové a proč je to jisté

- **SW cesta** (vždy): půlbajtová tabulka, bit po bitu shodná s dosavadní bitovou smyčkou (selftest driveru ji porovnává
  na 64 pseudonáhodných rámcích 1–1024 B; vektor „123456789“ = 0x29B1).
- **HW cesta** (`FPGA_CRC_USE_HW = 1`) přímo přes registry (RM0399, kapitola CRC), bez modulu HAL CRC:
  - `CRC->POL = 0x1021`, `CRC->INIT = 0xFFFF`, `CRC->CR = CRC_CR_POLYSIZE_0 | CRC_CR_RESET` (16 b, bez reflexe,
    RESET nahraje INIT);
  - data se zapisují po bajtech do `CRC->DR` (8b přístup; stejně to dělá HAL `CRC_Handle_8`), výsledek `CRC->DR[15:0]`;
  - hodiny `__HAL_RCC_CRC_CLK_ENABLE()`.
- **Pojistky:**
  1. při startu selftest HW proti SW (vektor + 32 bloků 1–256 B); nesoulad → HW se nepoužije;
  2. jednotka se **nastavuje před každým výpočtem** (jiný modul ji mohl překonfigurovat);
  3. zámek s nulovým čekáním — při souběhu nebo v přerušení se počítá SW;
  4. **kontrola přijatého rámce**: když HW řekne „nesedí“, přepočítá se SW. Nesoulad HW/SW = HW se trvale vypne a
     započte do `mismatch`. Vadná HW tedy nikdy nezpůsobí odmítnutí platného rámce.
- TX rámce (`build_frame`) zůstávají na SW (u v2 je to 126 B; u v3 lze přepnout na `fpga_crc16`, až bude HW ověřená).

### 3.2 Co musí udělat CLI

1. **Ověřit, že jednotku CRC nikdo jiný nepoužívá** — v obou jádrech (CM7 i CM4) a v knihovnách:
   `grep -rn "CRC->\|hcrc\|HAL_CRC_\|CRC_HandleTypeDef\|__HAL_RCC_CRC" Core* CM7 CM4 Drivers Middlewares`.
   - nic → pokračovat;
   - používá ji CM7 jinde → stačí stávající pojistky (bod 2 a 4), ale doplnit si to do poznámek;
   - **používá ji CM4** → zámek z CM7 nestačí (dvě jádra). Buď HW CRC nezapínat, nebo zámek přes HSEM (hardwarový semafor).
2. Ověřit v `stm32h757xx.h`, že existují `CRC_CR_POLYSIZE_0` a `CRC_CR_RESET` (měly by; jinak doplnit podle RM0399).
3. Přeložit s `-DFPGA_CRC_USE_HW=1` (definice v projektu, ne v kódu).
4. Doplnit do UART `status`: `fpga_crc_stats()` → `selftest_ok`, `hw_on`, `hw_calls`, `sw_calls`, `hw_busy`, `mismatch`.
5. Ověření na desce (kap. 6.2).

---

## 4. SPI přes DMA (`fpga_spi_dma`)

### 4.1 Co je hotové a proč je to jisté

- `fpga_spi_dma_txrx()` provede **jen datovou fázi** transakce; CS a mezery řídí dál `xfer_raw` (beze změny).
- Data se kopírují do **vlastních bufferů modulu** (zarovnané na 32 B, délka zaokrouhlená na celé řádky cache):
  `memcpy` → `SCB_CleanDCache_by_Addr(TX)` → `SCB_InvalidateDCache_by_Addr(RX)` → `HAL_SPI_TransmitReceive_DMA` →
  čekání na semafor s časovým limitem → `SCB_InvalidateDCache_by_Addr(RX)` → `memcpy` k volajícímu.
- **Chyby**: start selže / vyprší limit / chybový callback → `HAL_SPI_Abort`, návrat `false`. `xfer_raw` transakci ukončí
  (CS high) a vrátí chybu — **nikdy nepouští druhou datovou fázi pod stejným CS** (FPGA by viděla delší rámec).
  Po **3 chybách za sebou** se DMA vypne a dál běží blokující `HAL_SPI_TransmitReceive`.
- **Kontroly v `fpga_spi_dma_init`**: propojené handly `hdmatx/hdmarx`, buffery mimo DTCM (0x2000_0000–0x2001_FFFF) a
  ITCM, běžící scheduler (semafor). Cokoli nesedí → `false`, firmware jede blokující cestou.
- `fpga_spi_dma_ready()` vrací `false` mimo scheduler a v přerušení → tam vždy blokující cesta.
- Statistiky: `ok`, `start_err`, `timeout`, `dma_err`, `rejected`, doba posledního/nejdelšího přenosu v taktech DWT.

### 4.2 CubeMX (projekt `.ioc`) — CLI

1. **SPI2 → DMA Settings → Add**:
   - `SPI2_RX`: DMA1 (nebo DMA2) Stream volný, Direction *Peripheral To Memory*, Priority **High**;
   - `SPI2_TX`: další Stream, *Memory To Peripheral*, Priority **Medium** (RX ≥ TX, aby nevznikl overrun);
   - obě: Mode **Normal**, Increment Address *Memory* ano / *Peripheral* ne, Data Width **Byte / Byte**,
     FIFO **vypnuto** (direct mode) pro první verzi.
2. **NVIC**: zapnout přerušení obou DMA streamů **a SPI2 global** (HAL SPI na H7 používá při DMA i přerušení SPI
   pro EOT a chyby). **Priorita všech tří číselně ≥ `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`** (např. 5–15), protože
   callback volá `osSemaphoreRelease` — jinak FreeRTOS spadne na `configASSERT`.
3. Zkontrolovat pořadí v `main.c`: **`MX_DMA_Init()` před `MX_SPI2_Init()`** (CubeMX ho občas vygeneruje obráceně;
   pak DMA nefunguje).
4. Po vygenerování ověřit v `stm32h7xx_hal_msp.c`, že `HAL_SPI_MspInit` obsahuje `__HAL_LINKDMA(hspi, hdmarx, …)` a
   `__HAL_LINKDMA(hspi, hdmatx, …)` (jinak `fpga_spi_dma_init` vrátí false — bezpečné, ale DMA nepoběží).
5. Vlastní kód v `USER CODE` blocích, ať ho další generování nepřepíše (L-0012).

### 4.3 Sekce linkeru — CLI

DMA1/DMA2 nevidí DTCM. Buffery (2 × 1024 B) musí ležet v **AXI SRAM (0x2400_0000)** nebo **SRAM1–3 (0x3000_0000)**.

1. V linker skriptu CM7 (`*.ld`) ověřit / doplnit sekci, např.
   ```
   .dma_buffer (NOLOAD) : { . = ALIGN(32); *(.dma_buffer) . = ALIGN(32); } >RAM_D2
   ```
   (název oblasti `RAM_D2`/`RAM_D1` podle skriptu projektu).
2. Do definic projektu: `FPGA_DMA_SECTION=__attribute__((section(".dma_buffer")))`.
   Bez této definice se `fpga_spi_dma.c` **záměrně nepřeloží** (`#error`).
3. Po překladu v `.map` ověřit adresy `s_tx`/`s_rx` (0x24… nebo 0x30…). Modul DTCM stejně odmítne za běhu.
4. MPU: pokud je oblast nastavená jako ne-cacheovaná, operace s cache jsou neškodné; pokud je D-cache zapnutá
   (`SCB_EnableDCache`), jsou nutné — modul je dělá.

### 4.4 Callbacky HAL — CLI

`HAL_SPI_TxRxCpltCallback` a `HAL_SPI_ErrorCallback` jsou v HAL slabé (`__weak`).

- Nejdřív `grep -rn "HAL_SPI_TxRxCpltCallback\|HAL_SPI_ErrorCallback"` v CM7.
- **Existují** (jiné SPI) → do nich přidat volání:
  ```c
  void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
  {
      fpga_spi_dma_cplt_isr(hspi);      /* modul sam overi, ze jde o SPI2 */
      /* ... puvodni kod ... */
  }
  void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
  {
      fpga_spi_dma_err_isr(hspi);
      /* ... puvodni kod ... */
  }
  ```
- **Neexistují** → vytvořit je (např. v `fpga_freq.c` za `#if FPGA_SPI_USE_DMA`, nebo v `stm32h7xx_it.c` v USER CODE).

### 4.5 Zapnutí

1. Kapitoly 4.2–4.4 hotové.
2. Přeložit s `-DFPGA_SPI_USE_DMA=1`; `fpga_spi_dma.c` přidat do projektu.
3. Do UART `status` doplnit `fpga_spi_dma_stats()` (ready, disabled, ok, timeout, dma_err, start_err, rejected,
   `cyc_last`/`cyc_max` → µs = takty / (SystemCoreClock/1e6)).

### 4.6 Co se NEMĚNÍ

- CS a mezery (`delay_us` 2 + 2 + 25 µs) zůstávají busy-wait. Pro v3 (mezera ≥ 150 µs) je nahradit `osDelay`/časovačem — TODO v3.
- Zámek `s_spi_mtx` (FpgaTask × UART `fpgaraw`/`fpgaloop`) zůstává; DMA běží uvnitř něj.
- `FPGA_SPI_DMA_MAX = 1024` (v3); dnešní rámec 128 B.

---

## 5. Mrtvé moduly — úklid (CLI)

| modul / soubor | kde | stav |
|---|---|---|
| `phase_oversampler` | `spi_app.v` (~ř. 670–720) | nikde se neinstancuje; syntéza ho zahodí |
| `phase_check` | `spi_app.v` (~ř. 1190–1220) | totéž |
| `tdc_vernier_4phase.v` | repo FPGA | není v `Counter_FPGA.gprj` |
| `coarse_counter.v` | repo FPGA | není v `.gprj` |

Postup:
1. `grep -rn "phase_oversampler\|phase_check\|tdc_vernier_4phase\|coarse_counter" sim tools *.tcl *.gprj` — testbenche,
   skripty a projekt je nesmí používat (jinak nejdřív upravit je).
2. Moduly přesunout do `legacy/` (soubor `legacy/spi_app_legacy.v` + oba soubory), ne mazat — historie zůstane čitelná.
3. Build: **využití zdrojů v reportu P&R musí být identické** (moduly nebyly instancované) — to je kritérium, že úklid
   nic nezměnil. FW_VERSION se kvůli úklidu nemění, pokud je report identický; jinak nový build a bump.
4. `sim/` celá sada PASS.

---

## 6. Testy

### 6.1 Host (hotové, gcc; výstupy v `test/*.out`)

| test | ověřuje | výsledek |
|---|---|---|
| `t_crc_hw` (HW jednotka nahrazená **emulátorem** podle RM0399) | selftest HW = SW, výpočet, kontrola platného/vadného rámce, obsazený zámek → SW, vadná HW → platný rámec přijat a HW vypnuta, vadná HW při startu, běh před schedulerem | **10/10** |
| `t_dma` (HAL SPI DMA, semafor a cache nahrazené modely) | odmítnutí bez DMA handlů a před schedulerem, kontrola adres DTCM/ITCM/AXI/SRAM, přenos 128 B a 1024 B, pořadí operací cache, zarovnání bufferů, meze délky, zahození starého semaforu, timeout → Abort, chybový callback, selhání startu, vypnutí po 3 chybách, statistiky | **17/17** |
| `t_fpga` (celý driver s novým napojením) | původní selftesty, adaptivní regrese, meze, koherence | **prošlo** |
| syntaxe `fpga_freq.c` s `FPGA_SPI_USE_DMA = 0` i `1`, `fpga_crc.c` s HW, `fpga_spi_dma.c` | překlad bez chyb a varování; bez `FPGA_DMA_SECTION` záměrná chyba | **OK** |

**Co host testy NEpokrývají:** skutečné chování jednotky CRC a DMA na křemíku, priority přerušení FreeRTOS, umístění
v paměti, koherenci cache na reálném CM7, souběh s CM4.

### 6.2 Deska (CLI)

| # | test | kritérium |
|---|---|---|
| D1 | start s `FPGA_CRC_USE_HW = 1` | `selftest_ok = 1`, `hw_on = 1` |
| D2 | 24 h provozu | `mismatch = 0`, `g_rx_crc` stejné jako s SW (0) |
| D3 | start s `FPGA_SPI_USE_DMA = 1` | `ready = 1`, `disabled = 0`; adresy bufferů v `.map` mimo DTCM |
| D4 | `fpgaloop` 100 000 transakcí, DMA × blokující | 0 chyb CRC v obou; shodné rámce |
| D5 | souběh: `fpgaraw` z UART během pollingu FpgaTasku | žádná chyba, žádný deadlock |
| D6 | vyvolaná chyba: dočasně vypnout přerušení DMA streamu | 3 timeouty → `disabled = 1`, linka dál funguje blokující cestou |
| D7 | zátěž CPU (DWT/`status` CPU %) | DMA: čas CPU na čtení klesne o ~65 µs (v2) |
| D8 | 24 h s DMA | `timeout = dma_err = start_err = 0`, `cyc_max` stabilní |

### 6.3 Přijetí a návrat

- Přijato: D1–D8 splněno.
- Návrat: přeložit s `FPGA_CRC_USE_HW = 0` / `FPGA_SPI_USE_DMA = 0` — chování je pak shodné s E0.
