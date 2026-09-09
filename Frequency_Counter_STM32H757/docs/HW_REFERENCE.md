# HW_REFERENCE — stabilní referenční hodnoty

> Vyčleněno z `CLAUDE.md` 2026-08-29, aby se hlavní poznámka odlehčila. **Tyto tabulky
> se skoro nemění** — jsou to konkrétní hodnoty registrů / offsetů / pinů. `CLAUDE.md`
> na tento soubor odkazuje; závazné „proč" a pravidla zůstávají tam.

---

## Hodiny (main.c)

| Veličina | Hodnota | Zdroj |
|---|---|---|
| HSE | **25 MHz (bypass)** — od 2026-08-22 (v0.6.0), dřív 10 MHz; X1/TCXO sdílený s ETH PHY | |
| SYSCLK | 480 MHz | PLL1 **M=5, N=192**, P=2 (VCO 960 = stejné; vstup 25/5=5 MHz) |
| Pixel clock (LTDC) | 25 MHz | PLL3 **N=35, FRACN=0**, R=7 (VCO 175 = stejné) |
| DSI bit clock | 700 Mbps/lane | DSI-PLL **NDIV=28**, IDF=1, ODF=1 (VCO 1400 = stejné) |
| DSI byte clock | 87.5 MHz | bit/8 |
| DSI escape clock | 17.5 MHz | TXEscapeCkdiv=5 |

> **⚠️ Přechod 10 → 25 MHz HSE (2026-08-22, v0.6.0):** X1/TCXO byl vyměněn za 25 MHz,
> aby dostal ETH PHY LAN8742A svých 25 MHz (sdílený oscilátor, dřív 10 MHz → PHY nenaběhl,
> STATUS #24). **Princip přepočtu: všechna VCO zůstala identická, změnil se jen vstupní
> dělič** (`M` 1→5, vstup PLL 10→5 MHz) **+ DSI NDIV** (70→28) — takže **všechny výstupní
> frekvence jsou beze změny** (SYSCLK 480, LTDC 25, DSI 700, SPI123/FMC z PLL2 **M=5 N=40**,
> ADC3 25). `HSE_VALUE`=25000000 (oba `hal_conf.h`). ⚠️ **`HSE_VALUE` je KRITICKÉ pro
> `fpga_freq_init`** (počítá SPI prescaler přes `HAL_RCCEx_GetPeriphCLKFreq`). ⚠️ OCXO 10 MHz
> → Si5356 → FPGA čítač je **JINÝ, oddělený oscilátor** (na FPGA desce) → přesnost měření beze
> změny. ⚠️ **Tento FW nenaběhne na desce s 10 MHz HSE** (PLL by chtěl VCO 2400 MHz → nezamkne
> se → `Error_Handler`). Viz i `CLOCK_25MHZ_MIGRACE.md`.

---

## DSI VidCfg (dsihost.c)
- Mode = **DSI_VID_MODE_BURST**, ColorCoding = **DSI_RGB565**, NumberOfLanes = 1, PacketSize = 800
- H (byteclk): HSA=7, HBP=161, HLINE=2975
- V: VSA=2, VBP=21, VFP=7, VACT=480
- HS/VS polarity = ACTIVE_LOW
- PhyTimings (clk HS2LP/LP2HS, data HS2LP/LP2HS, StopWait) = 35/35/35/35/10

## LTDC (ltdc.c)
- PixelFormat = **RGB565**
- H: HSA=2, HBP=46, HACT=800, HFP=2 → total 850
- V: VSA=2, VBP=21, VACT=480, VFP=7 → total 510
- FBStartAdress = 0xC0000000, Image 800×480

## TC358762 bridge (tc358762.c)
- LCDCTRL = **0x00100050** (RGB565, VTGEN on), SYSCTRL = 0x040F, LPTXTIMECNT = 3
- LCD H: HSW=2, HBP=47, HDISP=800, HFP=1 (rpi modeline)
- LCD V: VSW=2, VBP=21, VDISP=480, VFP=7

## Framebuffer + MPU (main.c)
- 0xC0000000 (SDRAM/FMC), RGB565, 800×480×2 = 750 KB / buffer
- **⚠️ Boot vyčistí FB na černo** (`memset(0xC0000000, 0, 3 MB)` v USER CODE 2, za MX_FMC_Init): SDRAM přežije **soft reset** → jinak LTDC při bootu krátce zobrazí poslední snímek před restartem.
- **MPU region 0: 4 MB, Write-Through** (dříve 2 MB — rozšířeno kvůli triple bufferingu)
- **SDRAM = 32 MB** (FMC: 9 col + 13 row bits × 4 banky × 16 bit).

### SDRAM mapa
- **Region 0 (4 MB WT, `0xC0000000`):** triple buffer **FB0 `0xC0000000`, FB1 `0xC0100000`, FB2 `0xC0200000`** + **off-screen canvas pool `0xC0300000`** (1 MB), vše RGB565, 1 MB stride.
- **Region 1 (4 MB WBWA cached, `0xC0400000`):** `sdram` test buffer (`sdram write/read`) + scratch; dřív bignum workspace.
- **`.sdram` linker sekce `0xC0800000`** (**8 MB**, default/Device map): libprim glow scratch + `bg_cache` (předrenderované pozadí). Reálně obsazeno 1,79 MB → 4,5× rezerva. *(Zmenšeno 16→8 MB 2026-08-30, aby se vešla datová cache níže.)*
- **Region 3 (16 MB WBWA cached, `0xC1000000`) = datová cache měření** (`.measlog` / `sdram_log.c`): 1 048 576 záznamů po 16 B, dlouhá **přesná** historie pro analýzu. Viz CLAUDE.md „Datová cache měření".
  - ⚠️ Sekce se jmenuje **`.measlog`, ne `.sdram_log`** — sekce `.sdram` výš má hladový wildcard `*(.sdram*)`, který by ji spolkl dřív (první shoda vyhrává) a 16MB pole by skončilo v 8MB regionu. Chyba se při psaní stala; link ji odhalil (`region SDRAM overflowed`).
- SDRAM celkem 32 MB — po přidání cache je **namapovaná celá** (4+4+8+16). Podrobný audit → `SDRAM_MAP_AUDIT.md`.
- 🔴 **NEUZAVŘENÝ NÁLEZ (viz i CLAUDE.md „Benchmark pamětí"): adresy SDRAM se můžou opakovat po 2 MB** (podezření HW pájka `FMC_A9`=`PF15` ↔ `HADDR[21]`). `membench` to přímo testuje (`fb_alias`, `sdram_safety_check`). Než saháš na cokoli v zobrazovacím řetězci, přečti si `fb_alias` řádek.
  - ✅ **Nová informace 2026-08-30:** `sdram_log_init` testuje aliasing v pásmu `0xC1000000`–`0xC2000000` (mocniny 2 od 32 B po 8 MB) **plus** reverzibilní sondu proti FB0/FB1/FB2 — na HW **prošlo** (`sdramlog: OK`). V horních 16 MB se tedy adresy neopakují. Podezření na 2 MB tím **nepadá** (týká se jiného pásma), ale zužuje se.

---

## FreeRTOS tasky (freertos.c)

| Task | Priorita | Stack |
|---|---|---|
| defaultTask | Normal | **3584 B** (GPS drain + rtc_app_tick snprintf + syscfg persist + alarm_tick + watchdog_supervise + ipc_publish/ipc_service + CM4 stall detekce + USB pump) |
| UartTask | Normal | 4096 B |
| I2C4Task | Low | 1536 B |
| UiTask | BelowNormal | 8192 B |
| FpgaTask | Normal | 2048 B |
| UartRxQueue | — | 64 × 1 B |
| GpsRxQueue | — | 256 × 1 B |

⚠️ **Tabulku drž synchronní se skutečnými `.stack_size` v `freertos.c`** (audit 2026-08-17
odhalil rozjetí: dřív tu bylo 1536/2048 B, realita 2560/4096 B — defaultTask rostl kvůli
`rtc_app_tick` snprintf, UartTask kvůli TODO #9 bumpu 512→1024 words). Součet stacků
18 432 B ze 32 768 B heapu = 14 336 B rezerva na malloc/haldu.

PRIO_BITS=4, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5.
**Heap `configTOTAL_HEAP_SIZE` = 32768 B** (drženo i v .ioc klíčem
`FREERTOS_M7.configTOTAL_HEAP_SIZE`; dřívějších 15360 B = přesně součet stacků →
nešel přidat žádný task, proto byl zrušen GpsTask).

**Rozdělení souborů (CubeMX-regen-safe).** Tasky jsou vyčleněné z `freertos.c` do
`freertos_task_*.c`, sdílený stav v `freertos_shared.h`:
- `freertos.c` — jen `MX_FREERTOS_Init`, `StartDefaultTask`, definice globálů.
- `freertos_task_uart.c` (`UartTask_run`), `freertos_task_sensors.c` (`SensorsTask_run`),
  `freertos_task_ui.c` (`StartUiTask`), `freertos_task_fpga.c` (`StartFpgaTask`),
  `freertos_hooks.c` (RunTimeStats + stack/malloc hooky).
- ⚠️ **Regen-safe pattern:** `StartUartTask`/`StartI2C4` (= tasky v `.ioc`) zůstávají
  ve `freertos.c` jako **tenké stuby**, jejichž USER CODE tělo jen volá
  `UartTask_run`/`SensorsTask_run` ve split souborech → CubeMX regen NEzpůsobí
  duplicitní symbol. `UiTask`/`FpgaTask` (NEjsou v `.ioc`) mají handle + attributes
  v **USER CODE Variables** a `osThreadNew` v **USER CODE RTOS_THREADS** → regen je nesmaže.
  **Při přidání tasku v CubeMX:** drž se tohoto patternu (stub → `*_run`).

---

## Konektor J3 — propojka STM32 ↔ deska FPGA (2026-08-30)

Deska STM32 má **čtyři identické 20pinové rozšiřující sloty** `J3`–`J6` („Board 1–4",
`STM32H747BIT/Connectors.kicad_sch`). **FPGA deska visí na `J3`** — jako jediný nese
zároveň SPI2 i I2C1. Mapování návěstí → pin vytaženo geometricky z KiCadu a ověřeno
proti třem známým faktům (I2C1_SCL→PB8, I2C1_SDA→PB9, SPI2_MOSI→PB15 sedí s CLAUDE.md).

| Návěstí na J3 | STM32 pin | EXTI | Stav |
|---|---|---|---|
| `SPI2_SCK` / `SPI2_MISO` / `SPI2_MOSI` | PI1 / PI2 / PB15 | — | obsazeno |
| **`SPI2_NSS`** | **PB12** | 12 | dnes CS jako **ruční GPIO** (v `.ioc` pod starým jménem `SPI2_RCK`) |
| `I2C1_SCL` / `I2C1_SDA` | PB8 / PB9 | 8 / 9 | obsazeno |
| `USART1_TX` / `USART1_RX` | PB14 / PA10 | 14 / 10 | obsazeno |
| `PI4` | PI4 | 4 | **obsazeno** (GPIO out, `Locked` v `.ioc`) |
| **`TIM2_CH1`** | **PA15** | 15 | **VOLNÝ** ← kdyby se `Data_RDY` někdy přidával, patří sem (viz níže) |
| `TIM4_CH2` | PB7 | 7 | **VOLNÝ** |
| `PI3` | PI3 | 3 | **VOLNÝ** |
| `ADC2_INP0` / `ADC2_INP1` | PC2 / PC3 | 2 / 3 | volné, ale vedené jako analog |
| `VBUS`, `+5V`, `GND` | — | — | napájení |

### ✅ ROZHODNUTO 2026-08-30: `Data_RDY` se NEPOUŽIJE — stačí 4 vodiče

**Zůstává `MISO`/`MOSI`/`SCK`/`CS`.** Pátý vodič se záměrně nepřidává; zbytek téhle sekce
je zdůvodnění a záložní varianta, kdyby se to někdy ukázalo jako nutné.

- **Pro objem dat je `Data_RDY` bezpředmětný.** Trik „externí signál → DMA" má smysl na
  mnoho malých přenosů s nízkou latencí. Při vyčítání PSRAM se spustí **jeden dlouhý DMA**
  a CPU je volné celou dobu tak jako tak.
- **Handshake je in-band** (STATUS/FLAGS + `SEQUENCE`) — protokol byl takhle navržen právě
  proto, aby extra GPIO nepotřeboval.
- **Argument zadání §6 je o RUŠENÍ, ne o propustnosti** (*„každý burst vstřikuje rušení do
  časovacích vstupů asynchronně k měření"*) — a řeší se bez drátu:
  🔑 **čti úroveň `MISO` jako GPIO mezi přenosy.** `IDR` ukazuje skutečnou úroveň pinu
  i když je v režimu alternativní funkce, takže `HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_2)`
  funguje bez zásahu do SPI2. FPGA drží při deasertovaném CS na MISO příznak „mám data"
  a při CS↓ pin přepne na posuvný registr. **Dotaz je elektricky tichý** — žádné hodiny,
  žádná změna CS → rušení vzniká jen při skutečném čtení dat, což je nevyhnutelné.
  ⚠️ Vyžaduje, aby FPGA budila MISO i při CS vysoko (proti běžnému zvyku SPI slave = Hi-Z).
  Tady to nevadí — sběrnice je **dvoubodová** (zadání §6: „Jen Tang Nano na SPI1,
  žádná další zařízení").
- ⚠️ Argument o rušení **není v zadání změřený** (úvaha, ne `[M]`), a hlavní obrana proti
  němu je stejně už v návrhu pinů: `PIN53` je volný jako odstup před SPI na `PIN54–57`,
  stejně jako je volná pozice vedle `CH_A`/`CH_B`.
- **Důsledek: CS zůstává ruční GPIO na PB12** (hardwarový NSS níže není potřeba) → zlaté
  pravidlo „PB12 HIGH během config loadu GW1NR-9" platí triviálně dál, beze změny.
- **Co tím ztrácíme:** 32bitové razítko každého bloku z `TIM2->CCR1` (jen diagnostika:
  kadence FPGA, vlastní latence, detekce zameškaného bloku) a start přenosu bez CPU
  (u bulk čtení bezpředmětný).

---

### 📦 ZÁLOŽNÍ VARIANTA (kdyby se `Data_RDY` přece jen ukázal jako nutný)

Kdyby se rušení od SPI na časovacích vstupech ukázalo jako **měřitelný** problém, tady je
hotová analýza — hlavně proto, že volba pinu není libovolná.

#### `Data_RDY` by patřil na `TIM2_CH1` = **PA15**, ne na EXTI

Zadání nové desky (`../citac_zadani_predavaci.md` §4, Tang Nano PIN49) zavádí `Data_RDY`
s pravidlem „**STM32 nesmí SPI pollovat** — každý burst vstřikuje rušení do časovacích
vstupů asynchronně k měření". Aby to mělo smysl, musí být odezva **deterministická**;
probuzení tasku (desítky µs, jittruje — UiTask bere 27–41 % CPU) ten smysl ruší.

⚠️ **EXTI to neumí.** DMAMUX1 (mux pro DMA1/DMA2, tedy i SPI2) přijímá jako externí
trigger **jedinou** EXTI linku — `HAL_DMAMUX1_REQ_GEN_EXTI0` (=6). Ostatní jsou LPTIM1/2/3_OUT,
TIM12_TRGO a interní kanálové eventy. DMAMUX2 umí EXTI0 i EXTI2, ale obsluhuje jen BDMA
v doméně D3 (SPI6/I2C4/LPUART1/ADC3) — na SPI2 nedosáhne. **A na J3 není ŽÁDNÝ pin
s indexem 0**, takže cesta přes request generator je bez přepájení nedostupná.

✅ **Timer input capture ale generuje BĚŽNÝ DMAMUX1 požadavek**, takže žádná EXTI není
potřeba: `DMA_REQUEST_TIM2_CH1` = 18, `DMA_REQUEST_TIM4_CH2` = 30.
`Data_RDY` → capture na TIM2_CH1 → DMA request → přenos **bez účasti CPU**, na pinu,
který je na J3 už vyvedený.

Proč PA15/TIM2 před PB7/TIM4: **TIM2 je 32bitový** (`IS_TIM_32B_COUNTER_INSTANCE` = TIM2, TIM5),
takže `CCR1` dá **32bitové razítko každého `Data_RDY`** → zadarmo změříš kadenci FPGA,
vlastní latenci odezvy a poznáš zameškaný blok. TIM4 je 16bitový a přetáčel by se.
`PB7` = záloha, `PI3` = obyčejná EXTI pro druhý signál (chyba/overflow z FPGA).

⚠️ **PA15 je `JTDI`** — po resetu má pull-up, což určuje klidovou úroveň `Data_RDY`
a FPGA proti němu nesmí bojovat během bootu. Uvolnit ho lze (ladíme přes SWD na PA13/PA14),
ale je to vědomý krok.

### CS zůstává ruční GPIO (hardwarový NSS jen pro záložní variantu)

⚠️ **Platí JEN pro záložní variantu výše.** Při rozhodnutých 4 vodičích iniciuje přenos
CPU, takže ruční GPIO CS na PB12 je v pořádku a nic se nemění.

Kdyby DMA startovalo bez CPU, nemohl by CS asertovat CPU. Vyšlo dobře, že **PB12 je na J3
vedený jako `SPI2_NSS`**, tedy hardwarový NSS pin — přechod na `SSOE` nechce zásah do desky.
Časování kontraktu umí H7 v hardwaru: **`SPI_CFG2_MSSI`** (CS setup/hold ≥1 µs) a
**`SPI_CFG2_MIDI`** (mezera mezi rámci ≥20 µs).

🔴 **Zlaté pravidlo platí dál:** PB12 musí být HIGH během načítání konfigurace GW1NR-9
z flash, jinak FPGA nenaběhne (`RX0:FF`). Pin proto nechat po bootu jako GPIO output HIGH
a na AF přepnout až **po** `osDelay(250)` ve `FpgaTask`.

### Co doopravdy limituje počet vzorků

Ne signalizace, ale **délka jednoho CS okna**. Při 25 MHz trvá 64B rámec 20,5 µs, jenže
kontrakt žádá mezi rámci ≥20 µs → **mezera sebere polovinu propustnosti** (1,58 z 3,13 MB/s).
Pro vyčítání PSRAM proto nechceš 64B rámce, ale jedno dlouhé CS přes tisíce bajtů —
patří to do zadání FPGA firmwaru.

⚠️ **DMA1/DMA2 nedosáhnou na DTCM** — cílový buffer musí být v AXI SRAM (`RAM_D1`) nebo
rovnou v SDRAM. Při zápisu do datové cache `.measlog` je pak nutné `sdram_log_invalidate()`
(DMA obchází D-cache stejně jako DMA2D u framebufferu).

---

## I²C adresní mapa — SMLUVNÍ (nová deska, `../citac_zadani_predavaci.md` §3)

**Jedna sběrnice:** STM32 `I2C1` (SCL=PB8, SDA=PB9) → J3 → deska FPGA → konektor J4
na desce FPGA → vstupní modul. Rychlost **100 kHz** (sběrnice je vytížená pod 10 %,
limitem jsou relé 5 ms a převody ADC 1,2 ms, ne sběrnice).

| Adresa | Zařízení | Deska | Strap |
|---|---|---|---|
| 0x20 | `MCP23017` #1 (relé) | modul | A0–A2 → GND |
| 0x21 | `MCP23017` #2 (relé) | modul | A0 → V+ |
| 0x48 | `ADS1115` | FPGA | ADDR → GND |
| 0x49 | `TMP117` (OCXO) | FPGA | ADD0 → V+ |
| **0x4A** | `TMP117` | **modul** | ADD0 → SDA |
| **0x4B** | `ADS1115` (RF_Level A/B, ±5 V monitor) | **modul** | ADDR → SCL |
| **0x4C** | `AD5693R` | **FPGA** | A0 → GND |
| 0x60 | `MCP4728` (práh + hystereze) | modul | tovární |
| 0x70 | `Si5356A` | FPGA | I2C_LSB → GND |

- 🔴 **`General Call 0x06` NIKDY neposílat** — resetoval by naráz `MCP4728`, `TMP117` i `ADS1115`.
- **Pull-upy jen na desce FPGA** (`R64`, `R65` po 2k2). Do modulu žádné.
  Kapacita odhadem 210 pF z limitu 400 pF — změř náběžnou hranu na `SDA`; nad 1 µs sniž
  pull-upy na 1k5 (minimum 967 Ω).
- ⚠️ **0x4A přestává být „neosazená".** Dnešní kód hlásí na 0x4A NACK jako očekávaný stav
  (`sensor_fail`, červený `!` v diagnostice) — podle nové mapy je to **TMP117 ve vstupním
  modulu** a objeví se, jakmile bude modul připojen. Poznámku v CLAUDE.md („NEodstraňovat,
  ať se připojí, až bude") tím pádem lze uzavřít.
- ⚠️ **Nová zařízení, která dnešní firmware neobsluhuje:** `MCP23017` ×2, `MCP4728`,
  `ADS1115` @0x4B, `AD5693R` @0x4C.

## Tang Nano 9K — přiřazení pinů, SMLUVNÍ (`../citac_zadani_predavaci.md` §4)

| Hdr | FPGA pin | Signál | Poznámka |
|---|---|---|---|
| 1–4 | PIN38/37/36/39 | **nezapojeno** | TF slot osazený, pahýly |
| **5** | **PIN25_IOB8A** | **`CH_A`** | carry chain A |
| 6 | PIN26_IOB8B | volný | **odstup** (potlačení přeslechu) |
| **7** | **PIN27_IOB11A** | **`CH_B`** | carry chain B |
| 8 | PIN28_IOB11B | volný | odstup |
| 9, 10 | PIN29, PIN30 | LED stavu | statické, ne PWM |
| 11 | PIN33_IOB23A | `GPS_1PPS` | carry chain C |
| **14** | **PIN35_IOB29A** | **`REF_100MHz`** | `GCLKT_4`, PRIMARY |
| 15 | PIN41_IOB41A | `Ref_Ctrl` | statický, oddělovač referencí, **pull-down 10k** |
| 16 | PIN42_IOB41B | `REF_10MHz` | záloha, surová z OCXO |
| 17 | PIN51_IOR17B | `Ext_Ref_Sens` | 10 MHz monitor, jiný bank |
| 19–22 | PIN54–57 | `MOSI`, `SCK`, `FPGA_CS2`, `MISO` | SPI k STM32 |
| **29** | **PIN49_IOR24A** | **`Data_RDY`** | → STM32 **PA15 / TIM2_CH1** (viz výše) |
| 40–47 | IOT_1V8 | **nepoužívat** | bank 1,8 V |

⚠️ **Obě reference drž vždy jen jednu aktivní** — `REF_100MHz` a `REF_10MHz` jsou koherentní
(100 MHz vzniká z 10 MHz), takže jejich přeslech by se neprůměroval.

⚠️ **`CH_A`/`CH_B` mají vedle sebe volnou pozici záměrně:** zemní posuv je oběma společný
a v rozdílu A−B se odečte, kdežto vzájemný přeslech se neodečte a mění se s měřeným
intervalem. Jedna volná pozice sníží vazbu ~4×.

---

## FPGA strana protokolu (specifikace, co musí FPGA implementovat)

> 🔴 **Tohle je protokol v1 = dnešní deska s děličkou `MC100EP016A` (÷4 na pin28, ÷16 na pin27).**
> Nový návrh (`../citac_zadani_predavaci.md`) tenhle vstupní řetězec **opouští**: místo dvou
> odboček jednoho čítače má **dva nezávislé kanály `CH_A`/`CH_B`** s vlastním carry-chain TDC
> a jedinou předděličkou ÷10 (`MC12080`) ve vstupním modulu. Tím padá i 4fázový vernier
> ze `Si5356` (0/90/180/270°), na kterém stojí pole `phase_status`.
> **Dokud nová deska neběží, platí v1 níže.** Co se změní → sekce „Nová revize desky"
> v `CLAUDE.md` a TODO ve `../STATUS.md`.

STM32 = SPI master (generuje SCK+CS), FPGA = slave. Pevný **64B full-duplex** rámec: STM32
vždy vyclockuje 64 B a FPGA ve stejném přenosu vrátí svůj 64B response. FPGA drží poslední
hotové měření v TX bufferu. Handshake je **in-band** (STATUS/FLAGS), žádné extra GPIO.

**SPI (STM strana, `fpga_freq.c`):** master, mode 0 (CPOL=0, CPHA=0), MSB first, 8-bit.
MOSI=PB15, SCK=PI1, MISO=PI2, **CS=PB12 (manuál GPIO, active-low, STM idle HIGH)**.
Bring-up ~0,78–1 MHz (`fpga_freq_init` volí prescaler dle `HAL_RCCEx_GetPeriphCLKFreq(SPI123)`).
**Strop dle kontraktu FPGA (GW1NR-9 oversampling): cíl ≤6 MHz, absolutní max ~10 MHz** (NE 20 MHz)
— `FPGA_SCK_TARGET_HZ` / `#error` na `FPGA_SCK_MAX_HZ`. Prodlevy STM (DWT): CS↓→1.SCK 2 µs,
posl.SCK→CS↑ 2 µs, **mezi rámci 25 µs** (≥20 µs — FPGA potřebuje ~124 cyklů @10 MHz na složení rámce).
FPGA piny: PIN54=MOSI, PIN57=MISO, PIN55=SCK, PIN56=CS. Detaily viz `FPGA_INSTANCE_BRIEF.md`.

### 64B rámec
| Off | Size | Pole |
|---|---|---|
| 0 | 1 | MAGIC = 0xA5 |
| 1 | 1 | VERSION = 0x01 |
| 2 | 1 | TYPE |
| 3 | 1 | FLAGS/STATUS |
| 4 | 4 | SEQUENCE (uint32 LE) |
| 8 | 2 | PAYLOAD_LEN (uint16 LE, max 50) |
| 10 | 2 | RESERVED = 0 |
| 12 | 50 | PAYLOAD |
| 62 | 2 | CRC16 (LE: 62=low, 63=high) |

**CRC16 = CRC-16/CCITT-FALSE** (poly 0x1021, init 0xFFFF, no reflect, xorout 0), pokrývá byte 0–61.

**TYPE:** 0x01–0x07 gate/režim, 0x06 ACK, 0x08 start continuous, 0x09 stop, 0x80 DATA response, 0xA0 autokalibrace, 0xB0 FFT.

**STATUS/FLAGS bity:** 0=DATA_VALID, 1=DATA_FRESH, 2=FIFO_EMPTY, 3=FIFO_OVERFLOW, 4=BUSY, 5=RX_CRC_ERROR, 6=ACK_OK, 7=ERROR.

### DATA payload (TYPE 0x80), offsety v payloadu / absolutní
4-fázové reciproké měření, 2 předděliče:

| P-off | Abs | Size | Pole |
|---|---|---|---|
| 0 | 12 | 8 | frequency_x100000 (uint64 LE) — **pin28 /4** (primár) |
| 8 | 20 | 8 | edge_count (počet period v okně) |
| 16 | 28 | 8 | gate_time_ns (≈250e6, kolísá) |
| 24 | 36 | 8 | timestamp_10MHz_ticks |
| 32 | 44 | 1 | channel_id |
| 33 | 45 | 1 | measurement_status (bit0 VALID, bit1 FRESH) |
| 34 | 46 | 4 | error_flags (u32 LE): bit0 meas err(/4), bit1 SIGNAL_LOST, bit2 overflow (okno >~21,5 s) |
| 38 | 50 | 1 | **phase_status**: bity3:0 present[3:0], bity7:4 fine_seen[3:0] (zdravé=0xFF = `PH:F/F`) |
| 39 | 51 | 1 | **status2**: bit0 = chyba dělení pin27 (/16) |
| 40 | 52 | 8 | **freq16_x100000** (u64 LE) — **pin27 /16** |
| 48 | 60 | 2 | spare = 0 |

**Škálování:** `freq_x100000` i `freq16_x100000` = reálný kmitočet × 1e5, **dělička (/4, /16)
už zahrnutá ve FPGA → STM NEnásobí**. `edge_count` = počet period (diag), ne Hz.

**Model:** STM32 polluje ~20 Hz, posílá ACK (TYPE 0x06, SEQUENCE = poslední přijatá) → FPGA
full-duplex vrací DATA. Po validním ACK smí FPGA shodit DATA_FRESH. **Měření je platné když:**
CRC OK ∧ DATA_VALID ∧ DATA_FRESH ∧ SEQUENCE ≠ poslední přečtená. SEQUENCE roste pomaleji
u nízkých f (okno čeká na hrany) — ztrátu signálu hlásí error_flags bit1, ne zamrzlá SEQ.

**Formát kmitočtu (BEZ float):** `frequency_x100000` = kmitočet × 100000 (5 desetinných míst
v Hz). `integer_hz = v / 100000`, `frac = v % 100000`. Zobrazení: české oddělení tisíců **tečkou**,
desetinná **čárka**, přesně 5 míst, bez mezery před Hz → např. **`123.456.789,01234Hz`**.

> **Pravidlo:** jakákoli změna rámce/offsetů/škálování se promítá do OBOU stran. Nejdřív uprav
> tuto specifikaci, pak obě implementace, a zapiš do `STATUS.md` TODO.

Handoff / bring-up: `FPGA_SPI_HANDOFF.md`, `FPGA_INSTANCE_BRIEF.md`. Protokol v2 návrh: `FPGA_PROTOCOL_V2_NAVRH.md`.

---

## W25Q512JV — region mapa (w25q_map.h)

Deska je **generická** → regiony obecné, ne GPSDO-specifické. Zarovnané na 64 KB:

| Offset | Velikost | Region | Účel |
|---|---|---|---|
| `0x000000` | 64 KB | **CONFIG** | runtime nastavení (časté změny), wear-leveled store |
| `0x010000` | 64 KB | **CALIB** | kalibrace + zařízení param (zřídka), wear-leveled store |
| `0x020000` | 64 KB | **SETUP** | uložené sestavy (setup profily, `setup.c`, s_view=33), wear-leveled store |
| `0x030000` | ~63,8 MB | **DATA** | generický bulk / datalog (⚠️ base posunut z `0x020000` kvůli SETUP → datalog se jednou založí znovu) |

**Piny QUADSPI (v .ioc, CubeMX-managed → regen-safe):** CLK=**PF10**(AF9), NCS=**PG6**(AF10),
IO0=**PD11**(AF9), IO1=**PD12**(AF9), IO2=**PF7**(AF9), IO3=**PD13**(AF9), /RESET=**PH1**(GPIO out high).
**CubeMX QUADSPI:** `FlashSize=25` (2²⁶ = 64 MB — KRITICKÉ), `SampleShifting=HALFCYCLE`, `ClockMode=0`, single flash.
Driver `w25q_init` přebíjí prescaler na `W25Q_SCK_PRESCALER=3` → SCLK 60 MHz. Read = Quad Fast Read 0x6C (4-line, 8 dummy).
4-byte adresování: `EN4B` (0xB7) v initu + nativní 4B příkazy (READ `0x13` / PP `0x12` / SE `0x21`).

---

## Okna UI (`s_view`)

`0`=main, `1`=diag, `2`=gps, `3`=health, `4`=senzory, `5`=pamět, `6`=histogram, `7`=nastavení,
`8`=screensaver, `9`=trend-fullscreen, `10`=o-přístroji, `11`=boot-splash, `12`=menu-rozcestník,
`13`=confirm-restart, `14`=reference, `15`=kalibrace, `16`=holdover, `17`=datalog, `18`=alarmy,
`19`=čítač, `20`=selftest, `21`=komunikace (blokové schéma), `22`=čas (zóna), `23`=allan-fullscreen,
`24`=animace/demo, `25`=příklady animací (smyčka), `26`=spektrogram Δf, `27`=EFEKTY (přepínače),
`28`=status ribbon demo, `29`=grafy (časový průběh senzorů), `30`=přehled kanálů (horizontální bargrafy),
`31`=math/limity, `32`=self-survey, `33`=sestavy (uložit/načíst), `34`=měření (prezentace: perioda/jednotky/statistika/TFOM),
`35`=síť/ETH, `36`=displej (jas+auto-dim+vzhled), `37`=SD karta, `38`=kvalita GPS (historie sats/HDOP z datalogu),
`39`=prahy (meze monitoru), `40`=průvodce kalibrací, `41`=analýza (nejistota/drift/tempco),
`42`=přístup (jméno/heslo pro web+SCPI), `43`=paměti (benchmark RAM/FLASH),
`44`=MĚŘENÍ rozcestník, `45`=TI 1PPS (placeholder — HW), `46`=Dvojkanál (/4 + /16 + RF bargraf),
`47`=Odchylka ×N (ADRET 4110 styl — df·N + ppb/ppm), `48`=NÁSTROJE (pod Diagnostikou —
blok.schéma/paměť/selftest/benchmark/SD/reference).
