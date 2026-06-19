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
- PLL3: M=1, N=17, P=2, Q=2, R=7, VCO Medium, **FRACN=4096** → **25 MHz** → LTDC pixel clock
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
- **I2C4 EV/ER přerušení**: povolené ručně v `i2c.c` HAL_I2C_MspInit (USER CODE, prio 6), handlery v `stm32h7xx_it.c` (USER CODE BEGIN 1). Slouží pro asynchronní (IT) čtení doteku. **NEPOVOLOVAT I2C4 interrupt v IOC** — vznikly by duplicitní handlery (konflikt). Nech IOC bez I2C4 NVIC.

## USART1 (konzole/printf)
- **115200 8N1**, no flow control
- TX=**PB14**, RX=**PA10**
- NVIC: USART1 global IRQ **enabled, preempt priorita 5**

## GPIO
- **PB12 = Output Push-Pull** (CS k FPGA; štítek může být SPI2_RCK → klidně přejmenuj FPGA_CS) — MUSÍ zůstat output, **default Output Level = High** (CS deasserted od bootu; jinak ruší konfiguraci FPGA z flash → MISO mlčí RX0:FF)
- PI4 = Output (dříve SPI2_RES; nyní nepoužité, neškodí)
- **PG3 = LED_1** (output)
- (PG7 = bývalý Saleae trigger LED_2 — nepoužité)

## FreeRTOS (CMSIS_V2)
- Heap: **15360 B**, heap_4 ; PRIO_BITS=4 ; MAX_SYSCALL_INTERRUPT_PRIORITY=5
- Tasky (stack ve words):
  | Task | Priorita | Stack (words) |
  |---|---|---|
  | defaultTask | Normal | 128 |
  | UartTask | Normal | 512 |
  | I2C4Task | Low | 128 |
  | **UiTask** (přidat) | BelowNormal | 1024 |
  | **FpgaTask** (přidat) | Normal | 512 |
- Queue **UartRxQueue**: 64 items, **item size uint8_t (1 byte)** ← teď je v IOC uint16_t, OPRAVIT (jinak stack overflow)
- Mutexy (přidat): **i2c4Mutex**, **uartTxMutex**

## Cortex-M7 / MPU
- MPU: **nech vypnuté v IOC** (MPU_Control = NULL). SDRAM region (0xC0000000, 2MB, Write-Through) dělá `MPU_Config()` v USER CODE — záměrně, aby přežil regeneraci.

## NEJDE nastavit v IOC — zůstává v USER CODE / vlastních souborech (regenerace neohrozí)
- Vlastní moduly: `tc358762.c`, `ws_panel.c`, `ft5x06.c`, `gfx.c`, `touch_ui.c`, `fpga_freq.c`
- `MPU_Config`, `_write`, init sekvence displeje (main.c USER CODE 2)
- Těla tasků, UART příkazy, mutex wrapy, SPI2 runtime prescaler, použití fronty (1B)
- Po regeneraci ověř: DSI hodnoty, queue item size, přidané tasky/mutexy, PB12 jako output
