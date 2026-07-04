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

## NEJDE nastavit v IOC — zůstává v USER CODE / vlastních souborech (regenerace neohrozí)
- Vlastní moduly: `tc358762.c`, `ws_panel.c`, `ft5x06.c`, `fpga_freq.c`, `si5356.c`, `ads1115.c`, `beeper.c`
- Grafika `app/` + `libprim/` + `libui/` (triple buffering, present, off-screen canvas v `prim_stm32_hal.c`); `sensor_stat.h` (`g_sensors[]`)
- `MX_I2C1_Init` (i2c.c USER CODE), `MPU_Config`, `_write`, init sekvence displeje (main.c USER CODE 2)
- Split tasky `freertos_task_*.c` + globály v `freertos.c` USER CODE Variables (vč. `g_sensors`, `g_si5356_*`, `g_rtos_*`)
- Těla tasků, UART příkazy, mutex wrapy, SPI2 runtime prescaler, použití fronty (1B), CS pin (PB12) konfiguruje `fpga_freq_init`
- Po regeneraci ověř: **DSI hodnoty**, queue item size (1B), přidané tasky/mutexy, **PB12 output + default High**, **PA10 pull-up**, MPU region 0 = 4 MB
