# CubeMX (IOC) kontrolní seznam — H757_LED CM7

Hodnoty odpovídají AKTUÁLNÍMU funkčnímu kódu. Před každou regenerací z IOC zkontroluj,
že IOC sedí na tyto hodnoty, jinak `Generate Code` přepíše funkční nastavení.

## ⚠️ NEJVYŠŠÍ RIZIKO: DSI
Display byl rozchozen ručním doladěním DSI v `dsihost.c`. CubeMX si DSI timingy dopočítává
a NEMUSÍ vygenerovat stejné → **regenerace může zhasnout displej.** Pokud nemusíš, DSI negeneruj.
Po případné regeneraci OVĚŘ tyto hodnoty v `MX_DSIHOST_DSI_Init`:

## RCC / Clock Configuration
- HSE: **BYPASS, 10 MHz**
- Power: **SMPS 1.8V** (PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO), **VOS Scale 0**, **Flash latency 4**
- PLL1: M=1, N=96, P=2, Q=2, R=2, VCO Wide, FRACN=0 → **SYSCLK 480 MHz**
- AHB /2 → HCLK **240 MHz**; APB1/2/3/4 = /2 → **120 MHz**
- PLL2: M=1, N=20, P=1, Q=1, R=2, VCO Wide, FRACN=0 → **200 MHz** → FMC + SPI123(SPI2)
- PLL3: M=1, N=17, P=2, Q=2, R=7, VCO Medium, **FRACN=4096** → **25 MHz** → LTDC pixel clock **+ ADC3** (PLL3R)
  - ⚠️ **Od přidání ADC3 (sdílí PLL3R) CubeMX přesunul PLL3 init z `ltdc.c` `HAL_LTDC_MspInit` do `PeriphCommonClock_Config` (main.c)** — `ltdc.c` MspInit teď PLL3 NEnastavuje, jen `__HAL_RCC_LTDC_CLK_ENABLE`. Při ladění pixel clocku koukej do `PeriphCommonClock_Config` (PLL3 + `RCC_PERIPHCLK_LTDC|ADC` + `AdcClockSelection=PLL3`). Volá se brzy (po `SystemClock_Config`, před display init) → OK.
- DSI clock source: **D-PHY**
- I2C4 clock: **D3PCLK1** (PCLK4 = 120 MHz)
- USART1 clock: **D2PCLK2** (PCLK2 = 120 MHz)
- I-Cache **ON**, D-Cache **ON**

## DSI Host  ⚠️
- Number of lanes: **1**
- DSI PLL: NDIV=**70**, IDF=DIV1, ODF=DIV1; TX escape clock div = **5**
- Auto clock lane control: Disable
- Video mode: **Burst**
- Color coding: **RGB565**
- Loosely packed: Disable; Packet size: **800**; chunks 0; null packet 0
- H: HSA=7, HBP=161, HLINE=2975 ; V: VSA=2, VBP=21, VFP=7, VACT=480
- Polarity: HSync **Active Low**, VSync **Active Low**, DE **Active High**
- LP command enable: ON; LPLargestPacketSize=28; LPVACTLargestPacketSize=8; všechny LP enables ON
- PhyTimings (často auto — ověř!): ClockLane HS2LP/LP2HS=35/35, DataLane HS2LP/LP2HS=35/35, MaxReadTime=0, **StopWait=10**
- Frame BTA ack: Disable

## LTDC
- Pixel clock 25 MHz (PLL3R)
- Polarity: HS AL, VS AL, DE AL, PC IPC
- Timing: HSync=1, VSync=1, AccHBP=47, AccVBP=22, AccActiveW=847, AccActiveH=502, TotalW=849, TotalH=509
  (= HSW2/HBP46/HACT800/HFP2 ; VSW2/VBP21/VACT480/VFP7)
- Layer0: X 0..800, Y 0..480, **pixel format RGB565**, Alpha 255, blending CA/CA, FB=**0xC0000000**, ImageW=800, ImageH=480

## SPI2 (FPGA čítač)
- Mode: **Full-Duplex Master**, 8-bit, MSB first
- CPOL **Low**, CPHA **1 Edge** (= SPI mode 0)
- NSS: **Software** (CS je ručně řízené GPIO PB12)
- Prescaler: libovolný (runtime přepisuje `fpga_freq_init` na **1 MHz**, strop dle FPGA ≤6 cíl / ~10 MHz max)
- Piny: MOSI=**PB15**, SCK=**PI1**, MISO=**PI2**

## I2C4 (panel ATTINY 0x45, TMP117 0x48, FT5x06 0x38)
- Speed: **100 kHz** (Timing 0x70303AEE) — funkční. 400 kHz jen přes CubeMX FM + ověřit scope!
- 7-bit; piny SCL=**PH11**, SDA=**PH12**
- **NEPOVOLOVAT I2C4 NVIC interrupt v IOC.** Dotek (FT5x06) i senzory se čtou **pollingem** pod `i2c4MutexHandle`. (Pozn.: dřívější IT infrastruktura pro touch byla odstraněna — IOC nech bez I2C4 NVIC, jinak vznikne mrtvý/duplicitní handler.)

## I2C1 (FPGA deska: TMP117 0x49/0x4A, ADS1115 0x48, Si5356 0x70) — MIMO IOC
- **NENÍ v IOC.** `MX_I2C1_Init` je self-contained v `i2c.c` USER CODE 1 (GPIO+clock+timing tam), voláno z `main.c` USER CODE 2. Timing **0x70303AEE** (~100 kHz), piny SCL=**PB8**, SDA=**PB9** (AF4).
- ⚠️ **Rezervuj PB8/PB9 v IOC** (jako GPIO, Locked), ať je CubeMX nepřiřadí jinam při regeneraci → jinak tichý pin-konflikt. Mutex `i2c1MutexHandle`.

## USART1 (UART pro GPS; printf-konzole je na USB CDC, viz níže)
- 🔴🔴 **USART1 MUSÍ BÝT POVOLEN V `.ioc` (Connectivity → USART1 → Asynchronous).** Když v `.ioc` chybí, **každý „Generate Code" SMAŽE CELÝ UART**: smaže HAL driver (`Drivers/.../stm32h7xx_hal_uart.c/.h`, `ll_usart.h`, `ll_lpuart.h`), zakomentuje `HAL_UART_MODULE_ENABLED` v `hal_conf.h`, zahodí `USART1_IRQHandler` z `stm32h7xx_it.c` i `MX_USART1_UART_Init()` z `main.c`. Pak se projekt **NESESTAVÍ** (`huart1`/`HAL_UART_*` undefined, `usart.h` missing). Ruční obnova vydrží jen do dalšího regenu — **jediná trvalá oprava = USART1 v `.ioc`.** (Zjištěno opakovaně 2026-06-28: regen po přidání USB CDC opakovaně shazoval UART, protože USART1 vypadl z `.ioc`.)
- **115200 8N1**, no flow control
- TX=**PB14**, RX=**PA10**
- NVIC: USART1 global IRQ **enabled, preempt priorita 5** (RX přes IT → musí být v NVIC, jinak GPS RX nenaskočí)
- Po regenu ověř: `USART1\:I` v `CortexM7.IPs`, `NVIC1.USART1_IRQn=true\:5\:0...`, `PB14.Signal=USART1_TX`, `PA10.Signal=USART1_RX`, a `Mcu.PinNN=PB14`.
- ⚠️ **PA10 (RX) = Pull-up.** Nastav v IOC (PA10 → GPIO settings → Pull-up). Bez kabelu RX plave → bouře IRQ (prio 5) → scheduler hladoví → „program nenaběhne". Pojistka je i v `usart.c` USER CODE MspInit (přepíše NOPULL na PULLUP) + `HAL_UART_ErrorCallback` (zotavení po ORE/FE/NE) — obojí regen-safe, ale do IOC dej pull-up taky kvůli konzistenci.

## GPIO
- **PB12 = Output Push-Pull** (CS k FPGA; štítek může být SPI2_RCK → klidně přejmenuj FPGA_CS) — MUSÍ zůstat output, **default Output Level = High** (CS deasserted od bootu; jinak ruší konfiguraci FPGA z flash → MISO mlčí RX0:FF)
- PI4 = Output (dříve SPI2_RES; nyní nepoužité, neškodí)
- **PG3 = LED_1** (output)
- (PG7 = bývalý Saleae trigger LED_2 — nepoužité)

## FreeRTOS (CMSIS_V2)
- Heap: **32768 B** (`configTOTAL_HEAP_SIZE`; drží ho i .ioc klíč
  `FREERTOS_M7.configTOTAL_HEAP_SIZE=32768` → regen nevrací default. Dřívějších
  15360 B = přesně součet stacků → nešel vytvořit žádný další task/objekt.)
  heap_4 ; PRIO_BITS=4 ; MAX_SYSCALL_INTERRUPT_PRIORITY=5
- Tasky (stack ve words; defaultTask/I2C4Task 384 už v .ioc — regen hlídat):
  | Task | Priorita | Stack (words) |
  |---|---|---|
  | defaultTask | Normal | 384 |
  | UartTask | Normal | 512 |
  | I2C4Task | Low | 384 |
  | **UiTask** (přidat) | BelowNormal | 2048 |
  | **FpgaTask** (přidat) | Normal | 512 |
- Queue **UartRxQueue**: 64 items, **item size uint8_t (1 byte)** ← teď je v IOC uint16_t, OPRAVIT (jinak stack overflow)
- Mutexy (přidat): **i2c4Mutex**, **uartTxMutex**

## Cortex-M7 / MPU
- MPU: **nech vypnuté v IOC** (MPU_Control = NULL). Regiony dělá `MPU_Config()` v `main.c` USER CODE (Boot_Mode_Sequence_0) — záměrně, aby přežil regeneraci.
- **Region 0: `0xC0000000`, 4 MB, Write-Through** (dříve 2 MB) — triple buffer FB0/FB1/FB2 + canvas pool (viz `prim_stm32_hal.c`).
- **Region 1: `0xC0400000`, 4 MB, WBWA** — `sdram` test buffer + scratch.
- (`.sdram` linker sekce `0xC0800000` = default/Device map; libprim glow + `bg_cache`.)

## ADC3 (interní teplota jádra / VREFINT / VBAT) — V IOC ✅ (hotovo)
- **ADC3** na **Cortex-M7**, kanály **Temperature Sensor / VREFINT / VBAT** (Rank 1/2/3, jen interní → žádné GPIO piny, jen `VP_ADC3_*` virtuální).
- **16-bit, Scan ON, Continuous OFF (single-shot), software trigger**, Sampling **810.5 cyklů** (vysokoimpedanční interní zdroje → dlouhý sampling kvůli přesnosti).
- **ADC clock source = PLL3 (PLL3R 25 MHz), prescaler ASYNC DIV8 → 3,125 MHz** (`AdcClockSelection=RCC_ADCCLKSOURCE_PLL3`, `ClockPrescalerADC3=ADC_CLOCK_ASYNC_DIV8`). Pomalý ADC clock + dlouhý sampling = přesné čtení pomalých senzorů (čteno ~2×/s). PLL3 sdílí s LTDC → viz pozn. u PLL3 výše (init v `PeriphCommonClock_Config`).
- Generate Code dotáhl `stm32h7xx_hal_adc.c/_ex.c` + `adc.c`/`MX_ADC3_Init` + `HAL_ADC_MODULE_ENABLED`. **ADC NVIC vypnuté** (čteme blocking/polling v SensorsTask, žádné IRQ).
- **App vrstva HOTOVA (regen-safe, `freertos_task_sensors.c` + `app_gpsdo.c`):** čtení v SensorsTask, přepočet TS_CAL1/2 (@0x1FF1E820/40) + VREFINT_CAL (@0x1FF1E860) → VDDA; VBAT = raw×VDDA/65535×4. Hodnoty v `g_sensors[SENS_CORE_T/VDDA/VBAT]` (`SENS_COUNT` 7→10). Zobrazení: diag karta „MCU (jádro/napájení)", okno SENZORY, UART `sensors`/`adcraw` (debug).
- ⚠️⚠️ **HW-tricky věci (NUTNÉ, jinak interní kanály railují na 0xFFFF):**
  0. **🔑🔑 VREF+ NENÍ na desce spojen s VDDA → reference budí `VREFBUF` (`main.c` USER CODE 2).** **KRITICKÉ: `__HAL_RCC_VREF_CLK_ENABLE()` PŘED konfigurací VREFBUF** — VREFBUF má vlastní clock `RCC_APB4ENR.VREFEN` (NE SYSCFG!); bez něj je `VREFBUF->CSR` mrtvý (zápisy ignorovány, ENVR nedrží, VREF+ visí ~0,5 V, ADC railuje). Pak `VoltageScalingConfig(SCALE0 ≈ 2,5 V)` + `HighImpedanceConfig(DISABLE)` + `EnableVREFBUF`. **VREF+ vyžaduje ≥1 µF kondenzátor** (na desce dodán) pro stabilitu (VRR ready). Reference je tedy **~2,5 V (NE 3,3 V)** → senzor „VREF", teplota má korekci `×vref/3300`. Ověření: `adcraw` → `CSR=0x09`.
  1. **Kalibrace jsou 16-BIT** (ne 12-bit jak tvrdí HAL komentář; VREFINT_CAL=24291=1,22 V). LL makra `__LL_ADC_CALC_*` u VREFINT předpokládají 12-bit → **dají špatně**. Počítáme ručně 16-bit (VREF_CHARAC=3300 mV = napětí při tovární kalibraci, NE aktuální VREF+).
  2. **`ClockPrescaler` CubeMX VYNECHAL** z generovaného `MX_ADC3_Init` (i když .ioc má DIV8) → ADC běžel na **25 MHz** (PRESC=DIV1) + BOOST na nižší rozsah → vysokoimpedanční VREFINT/teplota railovaly. **SensorsTask init nastaví `hadc3.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV8` + `HAL_ADC_Init`** → 3,125 MHz, správný BOOST. **Po regenu zkontroluj, že ClockPrescaler je pořád ošéfovaný v SensorsThas initu.**
  3. **Interní cesty** (VREFEN/TSEN/VBATEN v `ADC3_COMMON->CCR`) jistíme ručně.
  4. **Single-channel režim** (ScanConvMode=DISABLE, NbrOfConversion=1, čtení po jednom `adc3_read_chan`) — scan polling všech 3 byl nespolehlivý.

## RTC (LSE 32.768 kHz na PC14/PC15) — V IOC ✅ (hotovo)
- **LSE:** RCC → Low Speed Clock (LSE) = **Crystal/Ceramic Resonator** → PC14=`RCC_OSC32_IN`, PC15=`RCC_OSC32_OUT`.
- **RTC:** Timers → RTC → **Activate Clock Source + Activate Calendar**, přiřazeno **Cortex-M7** kontextu. Hour format 24, **AsynchPrediv=127, SynchPrediv=255** (→ 1 Hz), Data format **Binary**.
- **Clock source = LSE** (Clock Config RTC mux; `RCC_RTCCLKSOURCE_LSE`). RTCFreq=32768.
- Generate Code dotáhne `stm32h7xx_hal_rtc.c/_ex` + `HAL_RTC_MODULE_ENABLED` + `rtc.c`/`MX_RTC_Init`; do `SystemClock_Config` přidá LSE (`HAL_PWR_EnableBkUpAccess`, `LSEDRIVE_LOW`).
- ⚠️ **defaultTask stack 256→384 words** (`Tasks01`) — `rtc_app_tick` přidává `snprintf` do GPS-parse tasku.
- **App vrstva je v `rtc.c`/`rtc.h` USER CODE blocích (regen-safe):** `rtc_app_tick()` (sync z GPS UTC + format `g_rtc_text`), backup-register guard (`RTC_SYNC_MAGIC` v BKP_DR0) v `Check_RTC_BKUP`. Volá se z defaultTask. Viz CLAUDE.md „RTC".

## SDMMC1 (SD karta pro datalog, #28) — V IOC ✅ (zapnuto 2026-08-11)
- **Peripherals → SDMMC1 → Mode = `SD 4 bits Wide bus`**, přiřazeno **Cortex-M7** kontextu (`PinAttribute=CortexM7`).
- **Piny (ověřeno proti schématu, list `USB_SD_FLASH`):** PC8=`D0`, PC9=`D1`, PC10=`D2`, PC11=`D3`, PC12=`CK`, PD2=`CMD`. AF12 (`GPIO_AF12_SDIO1`), Speed **VERY_HIGH**.
- **PD2 (CMD) = `GPIO_PULLUP`**, datové linky `NOPULL` — na desce jsou externí pull-upy **R56–R61**, takže je to správně.
- **Clock:** `SdmmcClockSelection = PLL` (PLL1Q), `SDMMCFreq_Value = 64 MHz`, **`SDMMC1.ClockDiv = 2`**.
  → `SDMMC_CK = 64 MHz / (2 × ClockDiv)` = **16 MHz**. Init/identifikaci na 400 kHz řeší HAL sám.
  ⚠️ Deska má na SD VDD jen C75 100n (chybí bulk 4,7–10 µF) a na CK není sériový tlumicí odpor (~22–33 Ω) → teoreticky překmity při vyšším taktu; 16 MHz ale ověřeně jede (`sd test` bit po bitu shodné, 2026-08-14).
- 🔴🔴 **`SDMMC1.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE` — KRITICKÉ, v `.ioc` CHYBĚLO!**
  (V CubeMX: SDMMC1 → Parameter Settings → **Hardware Flow Control = Enable**.) Bez něj má `CLKCR`
  bit17 `HWFC_EN = 0` a na H7 SDMMC **datová cesta vůbec nejede**: příkazy (init/CID/CSD, karta až
  do `TRANSFER`) projdou, ale **blokový přenos nedostane ani bajt** — `DPSMACT` visí, `STA=0x1000`,
  `HAL_SD_ReadBlocks` skončí SW timeoutem. Přesně tohle blokovalo SD půl dne (2026-08-14); odhalil to
  funkční Frantův projekt `H757_SDcard_01`, který HWFC zapnutý má. **Dokud to není v `.ioc`**, řeší to
  regen-safe runtime override v `sd_export.c` (`sd_apply_init_config` nastaví `hsd1.Init.HardwareFlowControl`
  + `SDMMC_Init(SDMMC1, hsd1.Init)` po `HAL_SD_InitCard` zapíše CLKCR — protože init skládáme sami
  a vynecháváme `HAL_SD_ConfigWideBusOperation`, kde by ho HAL jinak aplikoval).
- Generate Code dotáhne `stm32h7xx_hal_sd.c/_ex`, `stm32h7xx_ll_sdmmc.c`, `HAL_SD_MODULE_ENABLED` a `sdmmc.c`/`MX_SDMMC1_SD_Init`.

### ⬅ JEŠTĚ DOPLNIT: card-detect (pro hot-plug + detekci přítomnosti)
Ověřeno ze schématu (list 7/7 `USB_SD_FLASH` + list 2/7 `CPU`), 2026-08-11:
- Socket **J13 `Micro_SD_DM3AT`**: card-detect je **mechanický spínač** mezi
  **DET_A (pin 10) = GND** a **DET_B (pin 9) = net `SDMMC1_DET`**, který má **47k pull-up**
  na +3V3 (jeden z R56–R61). → **karta vložena = LOW**, prázdný slot = HIGH.
- Net `SDMMC1_DET` končí na **PE3** — potvrzeno uživatelem proti schématu 2026-08-11.
  (Můj prvotní odečet z vykresleného PDF říkal PE2 — **byl špatně**. Oba piny jsou v `.ioc` volné,
  takže to `.ioc` samo nerozliší; platí PE3.)
- [ ] V CubeMX: **PE3 → `GPIO_Input`**, `GPIO_PuPd = PULLUP` (externí pull-up sice je, ale
      interní nic nestojí), `GPIO_Label = SD_DET`, kontext **Cortex-M7**.
      ⚠️ Není to nutné — `datalog_sd.c` si pin konfiguruje **sám** (idempotentně, stejný
      regen-safe vzor jako CS pin ve `fpga_freq_init`), takže to funguje i bez `.ioc`.
      V CubeMX to zapiš hlavně proto, aby ten pin nikdo omylem nepřiřadil jinam.
- [ ] **EXTI nepovolovat** — detekci dělej **pollingem** (stejný styl jako touch a senzory):
      ISR + debounce mechanického spínače je zbytečná komplikace.
- **NEPOTŘEBUJEŠ:** DMA (SDMMC má vlastní interní IDMA — ve FatFs Advanced Settings musí být
  `Use dma template = Disabled`).

### 🔴🔴 SDMMC1 global interrupt MUSÍ být povolený (od 2026-08-13)
**Dřív tu stálo „nepotřebuješ" — to platilo jen dokud jedinou cestou ke kartě byl `datalog_sd.c`
(blokující `HAL_SD_ReadBlocks/WriteBlocks` = FIFO polling, bez přerušení). Zapnutím FatFs to
přestalo platit a nikdo to nepřepsal.** ST-čkový `sd_diskio.c` dělá **každé** čtení i zápis přes
`BSP_SD_ReadBlocks_DMA()` a pak čeká na zprávu ve frontě `SDQueueID`. Tu pošle jedině řetězec

```
SDMMC1_IRQHandler → HAL_SD_IRQHandler → HAL_SD_RxCpltCallback
                  → BSP_SD_ReadCpltCallback → osMessageQueuePut(SDQueueID)
```

`SDMMC1_IRQHandler` ale v projektu **neexistoval** (ve `startup_stm32h757bitx.s` je `__weak`
napojený na `Default_Handler`) a NVIC nebyl povolený → zpráva nemohla nikdy přijít. Každý
`f_mount`/`f_read`/`f_write` proto čekal celých `SD_TIMEOUT` = **30 s**, pak vrátil chybu.
UartTask přitom točil `BSP_SD_GetCardState()` v těsné smyčce → **UiTask (BelowNormal) hladověl**,
`watchdog_kick_ui()` se nevolal a desku po ~4 s shodil IWDG. Navenek: „`sd fs` zamrzne konzoli
a restartuje desku".

Řešení je v kódu a je **regen-safe** — v `.ioc` nic zapínat nemusíš:
- obsluha `SDMMC1_IRQHandler` je v `stm32h7xx_it.c` v **`USER CODE BEGIN 1`**,
- `HAL_NVIC_SetPriority(SDMMC1_IRQn, 5, 0)` + `EnableIRQ` dělá `BSP_SD_Init()` v `sd_export.c`
  (idempotentní, stejný vzor jako CS pin ve `fpga_freq_init`).

⚠️ **Kdybys to přesto zapínal v CubeMX**, dej `SDMMC1 global interrupt` prioritu **5** (obsluha
volá FreeRTOS API, `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` = 5) a **vypni generování
obsluhy** (sloupec *Code generation* → `Generate IRQ handler` = ne), jinak vznikne
**duplicitní symbol** s tím naším v `USER CODE 1`.

- **FatFs:** viz vlastní sekce níže (jen pokud padne volba na FS).

### 🔴 KRITICKÉ po každé regeneraci: `MX_SDMMC1_SD_Init()` musí zůstat vyřazená
CubeMX generuje v `MX_SDMMC1_SD_Init()` na selhání `HAL_SD_Init` volání **`Error_Handler()`**,
což na CM7 znamená `__disable_irq(); bootled_fail();` = **nekonečné blikání LED_1, mrtvý přístroj
bez displeje a bez konzole**. `HAL_SD_Init` přitom selže **pokaždé, když není vložená karta** →
boot bez karty by přístroj zabil.

Řešení (regen-safe, protože je v USER CODE): v `sdmmc.c` se v bloku
`/* USER CODE BEGIN SDMMC1_Init 0 */` **vyplní handle a pak se udělá `return;`**, čímž se
přeskočí jen samotné `HAL_SD_Init` (a s ním `Error_Handler`). Skutečný init pak dělá
`BSP_SD_Init()` při mountu, kde je selhání legitimní výsledek (bez karty → datalog jede dál
na W25Q — stejná filozofie jako `goto display_skip` u panelu nebo `g_cm4_absent` u CM4).

### ⚠️⚠️ HANDLE SE MUSÍ VYPLNIT — holý `return;` NESTAČÍ (nález 2026-08-12)
První verze téhle opravy měla v USER CODE jen `return;`. **Rozbilo to celou SD cestu** a stálo
to půl dne ladění. Důvod: `HAL_SD_MspInit()` začíná

```c
if (sdHandle->Instance == SDMMC1)
```

takže s `hsd1.Instance == NULL` se **nezapnou hodiny SDMMC1 ani nenakonfigurují piny**.
Naměřeno přes GDB: `hsd1.Init` celé nulové a registry `@0x52007000` samé nuly — periferie
mrtvá. `BSP_SD_Init()` pak selhával bez ohledu na kartu.

Správně tedy:
```c
/* USER CODE BEGIN SDMMC1_Init 0 */
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd1.Init.ClockDiv            = 2;
  return;   /* preskoc jen HAL_SD_Init (Error_Handler pri chybejici karte) */
/* USER CODE END SDMMC1_Init 0 */
```
Plnění struktury na HW nesahá, takže boot bez karty zůstává bezpečný.

- [ ] Po regeneraci ověř, že blok v `SDMMC1_Init 0` **pořád je** — a že vyplňuje handle, ne jen `return`.
- [ ] Hodnoty drž v sync s generovanými níže (zdroj pravdy = `.ioc`) i se `sd_probe()` v `datalog_sd.c`.

### `get_fattime()` — časová razítka souborů
CubeMX generuje v `CM7/FATFS/App/fatfs.c` stub `return 0;` → soubory na kartě by měly na PC
neplatné datum. V USER CODE bloku je proto implementace čtoucí **`g_rtc_text_local`**
(RTC disciplinované z GPS).
⚠️ **Nesmí volat `HAL_RTC`** — běží z UartTasku a přístup k RTC registrům je vyhrazený
defaultTasku (viz `CLAUDE.md` „RTC / Vlákno"). Proto se čte hotový řetězec, jako to dělá UI.
- [ ] Když měníš v CubeMX konfiguraci SDMMC1, **srovnej hodnoty i v `sd_probe()`** (zrcadlí `Init` strukturu).

## 🔴 PO REGENERACI S NOVÝMI SOUBORY: Close Project → Open Project (F5 NESTAČÍ!)
Když CubeMX přidá **nové zdrojové soubory** (nová periferie, middleware), zapíše je do `.project`
jako `<link>` entry. Eclipse ale `.project` parsuje **jen při otevření projektu** — z něj staví
model workspace včetně linkovaných zdrojů. Generátor makefilů (`sources.mk`, `subdir.mk`) čerpá
z toho modelu.

⚠️ **`F5 (Refresh)` to NEVYŘEŠÍ** — jen odsvěží stav souborů, o kterých Eclipse *už ví*; definice
linkovaných zdrojů znovu nečte. `Clean` taky ne — vygeneruje makefily ze stejného zastaralého modelu.

Projev: kompilace proběhne, ale **link padne na `undefined reference`** na symboly z nově přidaných
souborů (`HAL_SD_*`, `f_mount`, `FATFS_LinkDriver`, …) + `Unknown destination type (ARM/Thumb)` a
`dangerous relocation`. Přitom `.project` i soubory na disku jsou v pořádku.

**Rychlá diagnóza** (potvrdí, že jde o tohle a ne o chybu v kódu):
```
grep SUBDIRS -A30 CM7/Debug/sources.mk          # chybí tam nová složka?
grep -c hal_sd CM7/Debug/Drivers/STM32H7xx_HAL_Driver/subdir.mk   # 0 = stale makefile
```

- [ ] **Close Project → Open Project** → Clean → Build.
- [ ] Ověř, že se ve stromu objevily nové soubory (`stm32h7xx_hal_sd.c`, `Middlewares/.../FatFs`).
- [ ] Poslední záchrana: smazat projekt z workspace **s odškrtnutým** „Delete project contents on
      disk" → `File → Import → Existing Projects into Workspace`. Model se postaví od nuly, na disku
      se nic neztratí.

### ✅ VYŘEŠENO 2026-08-12 — reimport zabral, žádná zaplata už není potřeba
Po **reimportu projektu do workspace** si IDE model konečně postavilo od nuly a nové soubory
zná: `Debug/Drivers/STM32H7xx_HAL_Driver/subdir.mk` obsahuje `stm32h7xx_hal_sd.c`,
`Debug/Middlewares/Third_Party/FatFs/subdir.mk` vzniklo, `objects.list` má FatFs objekty
a `Debug/makefile` má příslušný `-include`. Build z IDE i z příkazové řádky projde (`exit=0`).

**Mezitímní zaplata `CM7/makefile.defs` byla odstraněna** — jakmile IDE soubory zahrnulo samo,
linkovaly se dvakrát (117 chyb `multiple definition of 'HAL_SD_Init'` / `'f_mount'` …).
Tenhle symptom = *„zaplata už není potřeba, smaž ji"*, ne skutečná chyba.

<details><summary>Postup pro případ, že se problém vrátí (jiný projekt / jiná periferie)</summary>

Zásahy přímo do `Debug/` nemají smysl — IDE si makefily přegeneruje a přepíše je. Použij hook,
který CDT nikdy negeneruje: `Debug/makefile` dělá `-include ../makefile.defs` (řádek 43), tedy
**`CM7/makefile.defs`**. Načte se po `objects.mk` (kde je `USER_OBJS :=`) a před linkovacím
pravidlem `gcc -o ... @"objects.list" $(USER_OBJS) ...` → objekty jdou na linkovací řádku
**mimo `objects.list`**, takže je to na modelu IDE nezávislé. Do souboru patří `USER_OBJS +=`
pro každý chybějící objekt + překladová pravidla (do vlastního adresáře, např. `_extra/`).

⚠️ **Je to dočasné.** Kontrola, kdy zaplatu smazat:
`grep -c hal_sd Debug/Drivers/STM32H7xx_HAL_Driver/subdir.mk` → `>0` znamená, že IDE už soubory
zná, a zaplata musí pryč.
</details>

**Pasti, na které jsem narazil při ladění** (kdyby bylo někdy potřeba psát do `Debug/` přímo):
- `Debug/makefile` má **explicitní `-include <dir>/subdir.mk` pro každou složku** — `SUBDIRS`
  v `sources.mk` je jen kosmetika a přidání tam nic neudělá.
- `Debug/objects.list` **nemá v makefile žádné pravidlo** — objekty se přeloží, ale linker o nich
  neví, protože dostává starý seznam.
- Generované makefily mají **CRLF**; zapisovat s `newline=''`, jinak vznikne literální `\n`
  místo pokračování řádku.
- `syscall.c` FatFs je v `src/option/`, ne v `src/`.

Build mimo IDE: `PATH=<...externaltools.make.../tools/bin>:<...gnu-tools.../tools/bin>` + `make -j4 all`.

## FATFS (SD karta jako export) — V IOC ✅ (zapnuto 2026-08-11)
- Middleware → **FATFS → SD Card**; `FATFS0.BSP.instance = PE3`, `name = Detect_SDIO`, `mode = Input`
  → CubeMX si sám vygeneruje detekci karty do `fatfs_platform.c` (`SD_DETECT_PIN = GPIO_PIN_3`,
  `SD_DETECT_GPIO_PORT = GPIOE`). Polarita sedí s HW: **LOW = karta vložena**.
- Vygeneruje `CM7/FATFS/{App,Target}` + `Middlewares/Third_Party/FatFs`. `MX_FATFS_Init()` se volá
  z `main.c` — jen registruje driver, na HW nesahá.
- ✅ **`BSP_SD_Init()` sám kontroluje přítomnost karty** (vrátí `MSD_ERROR_SD_NOT_PRESENT`) a pak
  volá `HAL_SD_Init` + `HAL_SD_ConfigWideBusOperation(4B)`. Karta se tedy inicializuje **líně přes
  FatFs** → vyřazení `MX_SDMMC1_SD_Init()` (viz výše) je nejen bezpečné, ale **správné**.
- Konfigurace `ffconf.h`: `_USE_LFN = 0` → **jen 8.3 jména** (`GPSDO.CSV` vyhovuje);
  `_FS_REENTRANT = 1` (vyžaduje `src/option/syscall.c` — CubeMX ho dodá); `_FS_TINY = 0`
  → `FIL` má vlastní 512B buffer, takže `sd_export_run()` má **808 B rámec** (běží v UartTasku, 4 kB).
- 🔴🔴 **`ffconf.h` SE REGENERUJE Z `.ioc` A NAŠE ÚPRAVY LEŽÍ MIMO USER CODE (jen hlavička ho má).**
  `.ioc` přitom FatFs parametry **NEDRŽÍ** (má jen `FATFS.BSP.number=1` + PE3), takže Generate Code
  může `ffconf.h` **vrátit na defaulty a shodit build**. **PŘED regenerací nastav v CubeMX
  FATFS → Advanced Settings / Set defines tyto hodnoty** (jinak se rozbije):
  - **`USE_MKFS = Enabled`** (`_USE_MKFS 1`) — bez něj `f_mkfs` neexistuje → **FORMAT tlačítko/`sd format` NEZKOMPILUJE**.
  - **`USE_EXPAND = Enabled`** (`_USE_EXPAND 1`) — bez něj `f_expand` neexistuje → **předalokace v `sd_export.c` NEZKOMPILUJE**.
  - `CODE_PAGE = 850`, `USE_STRFUNC = 2`, `MAX_SS = MIN_SS = 512`, `USE_LFN = 0`, `FS_RPATH = 0`, `FS_EXFAT = 0`
    (dorovnat na aktuální `ffconf.h`, jinak se změní chování).
  ⚠️ Po regeneraci **VŽDY zkontroluj `ffconf.h` diff** — je to jediné místo těchto voleb a `.ioc` je nechytá.

### 🔴 KRITICKÉ: dvě volby v `sd_diskio.c`, které CubeMX nechává VYPNUTÉ
Obě jsou v USER CODE blocích, takže **přežijí regeneraci** — ale po prvním vygenerování je
nutné je zapnout ručně:

```c
#define ENABLE_SD_DMA_CACHE_MAINTENANCE  1   /* USER CODE enableSDDmaCacheMaintenance */
#define ENABLE_SCRATCH_BUFFER                /* USER CODE enableScratchBuffer        */
```

- **Bez prvního není ŽÁDNÁ cache maintenance.** CM7 má zapnutou D-cache (`SCB_EnableDCache()`,
  `main.c`) a FatFs buffery leží v cacheable AXI SRAM; SDMMC na H7 jede přes **IDMA** i u
  blokujících `HAL_SD_ReadBlocks/WriteBlocks`. Zápis by šel z RAM (stará data), čtení z cache
  (stará data) → **přesně symptom `STATUS.md` #69** „init projde, karta se vidí, přenos nejede".
- **Bez druhého je ta maintenance nebezpečná.** ST ji jinak dělá přímo nad bufferem volajícího:
  `alignedAddr = buff & ~0x1F; SCB_InvalidateDCache_by_Addr(alignedAddr, …)`. FatFs buffery ani
  uživatelské buffery **nejsou zarovnané na 32 B** → invalidace zasáhne i cache linku se
  sousedními daty a **zahodí do nich zapsané dirty hodnoty** (tichá korupce cizí paměti, u
  zásobníkového bufferu klidně živého stack framu). Scratch buffer je `ALIGN_32BYTES` a všechno
  jde přes něj → hazard mizí. Cena = jedno memcpy na 512B blok.
- [ ] Po regeneraci ověř, že **oba `#define` v `sd_diskio.c` pořád jsou**.

## 🔴 IP, které jsou v `.ioc` uvedené, ale NESMÍ se konfigurovat (audit 2026-08-11)

`.ioc` nese v `CortexM7.IPs` / `CortexM4.IPs` víc IP, než se reálně staví. Většina je neškodná,
**dvě jsou past**:

| IP | Proč se nesmí konfigurovat |
|---|---|
| **`IWDG1`** (CM7) | `HAL_IWDG_MODULE_ENABLED` je v `hal_conf` **vypnutý** — watchdog je **registrová** implementace ve `watchdog.c` (heartbeat model, crash black-box do BKP). Konfigurace v CubeMX by vygenerovala `MX_IWDG1_Init` a tloukla by se s `watchdog_init()`. |
| **`IWDG2`** (CM4) | Totéž pro `CM4/Core/Src/iwdg2.c`. Navíc **reset scope IWDG2 je pořád neověřený** (`DUALCORE_BRINGUP_CHECKLIST.md` §8). |

- [ ] **IWDG1 ani IWDG2 v CubeMX NIKDY nekonfiguruj** (nechat neaktivované v seznamu je v pořádku —
      generuje se jen to, co je nastavené).

**Aspirační IP — regen je může začít generovat** (nabobtná image, možné konflikty). Před
regenerací zkontroluj, že zůstaly nenastavené:

| Jádro | IP |
|---|---|
| CM7 | `OPENAMP_M7`, `PDM2PCM_M7`, `USB_HOST_M7`, `WWDG1`, `BDMA` |
| CM4 | `OPENAMP_M4`, `PDM2PCM_M4`, `USB_DEVICE_M4`, `USB_HOST_M4`, `FATFS_M4`, `WWDG2`, `VREFBUF` |

(CM4 seznam hlídá i `DUALCORE_BRINGUP_CHECKLIST.md` §5.)

**`VREFBUF`** je v `.ioc` u obou jader, ale konfiguruje se **ručně v `main.c` USER CODE 2**
(`__HAL_RCC_VREF_CLK_ENABLE()` + `HAL_SYSCFG_VREFBUF_*`) — CubeMX na H7 nenastaví ten
**klíčový clock enable** `RCC_APB4ENR.VREFEN`, bez kterého je celý `VREFBUF->CSR` mrtvý.
- [ ] **VREFBUF v CubeMX nekonfiguruj** — nechal by dojem, že je hotový, a ADC by railoval.

## NEJDE nastavit v IOC — zůstává v USER CODE / vlastních souborech (regenerace neohrozí)
- Vlastní moduly: `tc358762.c`, `ws_panel.c`, `ft5x06.c`, `fpga_freq.c`, `si5356.c`, `ads1115.c`, `beeper.c`
- Grafika `app/` + `libprim/` + `libui/` (triple buffering, present, off-screen canvas v `prim_stm32_hal.c`); `sensor_stat.h` (`g_sensors[]`)
- `MX_I2C1_Init` (i2c.c USER CODE), `MPU_Config`, `_write`, init sekvence displeje (main.c USER CODE 2)
- Split tasky `freertos_task_*.c` + globály v `freertos.c` USER CODE Variables (vč. `g_sensors`, `g_si5356_*`, `g_rtos_*`)
- Těla tasků, UART příkazy, mutex wrapy, SPI2 runtime prescaler, použití fronty (1B), CS pin (PB12) konfiguruje `fpga_freq_init`
- Po regeneraci ověř: **DSI hodnoty**, queue item size (1B), přidané tasky/mutexy, **PB12 output + default High**, MPU region 0 = 4 MB, **`return;` v `SDMMC1_Init 0`** (viz sekce SDMMC1)
- ⚠️ **PA10 pull-up se v `.ioc` NENASTAVUJE** (a nemá — je to `USART1_RX`, `Mode=Asynchronous`).
  Řeší se **regen-safe v `usart.c` USER CODE `USART1_MspInit 1`**, kde se generovaný `GPIO_NOPULL`
  přepíše na `GPIO_PULLUP`. Důvod je vážný: bez kabelu RX plave → falešné start bity → bouře
  USART1 IRQ (prio 5 = `configMAX_SYSCALL`) → ISR preemptuje tasky → *„program nenaběhne"*.
  **Po regeneraci ověř, že ten USER CODE blok pořád je** — ne že něco chybí v `.ioc`.
