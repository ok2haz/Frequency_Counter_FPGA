# USB CDC (Virtual COM Port) — plán řízeného regenu („přístup 1")

Cíl: PC konzole přes **USB OTG FS** (PA11=D−, PA12=D+) jako CDC-ACM (VCP). Tím se
**uvolní USART1 celý pro GPS** (NEO-7M @ 9600). FS = 12 Mbps, na konzoli bohatě.

**Restore point před regenem:** commit `339c986` (pushnutý). Když regen něco rozbije:
`git reset --hard 339c986` (working tree) nebo cherry-pick zpět jednotlivé patche.
**Doporučení:** udělej regen na větvi `feature/usb-cdc`, po ověření merge do `main`.

Pinout (ověřeno volné v .ioc): USB PA11/PA12; **VBUS sensing VYPNOUT** (PA9 je encoder B).
Encoder (samostatně): PA8=TIM1_CH1, PA9=TIM1_CH2 (HW kvadratura), PC13=tlačítko — neřeší tento plán.

---

## 1) CubeMX nastavení (GUI, jádro CM7)

**Connectivity → USB_OTG_FS:**
- Mode = **Device_Only**
- **Activate VBUS = Disable** (jinak zabere PA9 = encoder!)
- (PA11/PA12 se přiřadí automaticky jako AF10 OTG_FS DM/DP)

**Middleware → USB_DEVICE:**
- Class For FS IP = **Communication Device Class (Virtual Port Com)**
- (volitelně uprav VID/PID/Manufacturer/Product string v descriptoru)

**Clock Configuration (KRITICKÉ — USB chce přesných 48 MHz):**
- Zapni **HSI48** (RCC → HSI48 ON) — interní 48 MHz oscilátor.
- USB clock mux: **RCC_USBCLKSOURCE_HSI48** (na clock diagramu zdroj USB = HSI48).
- Zapni **CRS** (System Core → RCC nebo CRS blok): SYNC source = **USB SOF** →
  autokalibrace HSI48 na ±0,25 % z USB rámců. Bez CRS HSI48 driftuje, enumerace nestabilní.
- ⚠️ **NESAHAT** na PLL1 (480) ani PLL3 (pixel clock 25 MHz N=17/FRACN/R=7) — HSI48 je
  nezávislý, proto se volí před PLL3Q (ten by rozhodil pixel clock → shear/barvy).

**NVIC:**
- OTG_FS global interrupt = **Enabled**, priorita **5** (= `configMAX_SYSCALL_INTERRUPT_PRIORITY`,
  stejně jako USART1) — protože CDC RX callback bude volat `osMessageQueuePut` z ISR.

**Project Manager:** ujisti se, že je „Generate peripheral init as pair of .c/.h" a
**„Keep User Code when re-generating"** (jinak smaže USER CODE bloky!).

→ **Generate Code.**

---

## 2) Post-regen re-apply checklist (co regen MŮŽE rozbít)

Po regenu udělej `git diff` a projdi tyhle ručně patchované soubory. CubeMX má USER CODE
bloky respektovat (pokud byl zapnutý „Keep User Code"), ale tyhle jsou mimo bloky / citlivé:

| Soubor | Patch | Akce po regenu |
|---|---|---|
| `gpio.c` | **PB12 (`SPI2_RCK`) boot level HIGH** (`HAL_GPIO_WritePin(...GPIO_PIN_SET)`, ř.59) — FPGA CS deasserted od bootu | V CubeMX nastav PB12 default Output Level = **High**, NEBO ověř že řádek přežil |
| `usart.c` | PA10 **PULLUP** v `USART1_MspInit` USER CODE 1; RX `ErrorCallback` **AbortReceive** + `RxCpltCallback` re-arm | USER CODE bloky → měly by přežít, ověř |
| `i2c.c` | **`MX_I2C1_Init` self-contained** v USER CODE 1 (GPIO+clock+init, regen-safe) | ověř |
| `stm32h7xx_it.c` | **`TIM7_IRQHandler`** (beeper) v USER CODE | ověř (+ nový `OTG_FS_IRQHandler` přibude — OK) |
| `main.c` | USER CODE 2: `MX_I2C1_Init()`, `si5356_init(&hi2c1)`, `beeper_init()`, DWT enable; USER CODE 1; **MPU 4 MB region**; `SystemClock_Config` (PLL3 pixel!) | ⚠️ ověř že `SystemClock_Config` má pořád PLL3 pixel clock + přibyl HSI48/CRS; init volání v USER CODE 2 |
| `freertos.c` | task **stuby** `StartUartTask`/`StartI2C4` → `*_run`; `UiTask`/`FpgaTask` v USER CODE Variables + RTOS_THREADS; queue **uint8_t** | ověř |
| `spi.c`/`fpga_freq.c` | CS + AFCNTR v driveru (regen-safe) | bez akce |

**Tip:** `git diff --stat` po regenu ukáže rozsah; sporné soubory porovnej s `339c986`.

---

## 3) Integrační kód (NAPÍŠU JÁ po regenu)

Generovaný CDC je jen skeleton — dolepí se:

- **`usbd_cdc_if.c` → `CDC_Receive_FS`:** příchozí bajty cpát do **stávající `UartRxQueue`**
  (`osMessageQueuePut` z ISR kontextu) → UartTask parser beze změny. Hned re-arm
  `USBD_CDC_SetRxBuffer` + `USBD_CDC_ReceivePacket`.
- **`_write` (main.c) → USB TX přes ring buffer:** `CDC_Transmit_FS` vrací `USBD_BUSY`,
  dokud předchozí přenos neskončí (NENÍ blokující jako UART!). Přidat malý **TX ring buffer**
  + flush ve smyčce / TxCplt. Bez toho se printf ztrácí. Zachovat `uartTxMutexHandle`.
- **Volba výstupu:** `_write` primárně USB; USART1 zůstává volný pro GPS (9600).
  (Volitelně: dokud není GPS, nechat fallback na USART1.)
- **`MX_USB_DEVICE_Init()`** volá CubeMX v `main()` — ověřit pořadí (po `MX_GPIO_Init`,
  klidně před schedulerem).

---

## 4) Pořadí kroků

1. (volitelně) `git checkout -b feature/usb-cdc`
2. CubeMX: bod 1 → Generate Code
3. `git diff` → projít checklist bodu 2, re-aplikovat co chybí
4. Říct mi „regen hotov" → napíšu integrační kód (bod 3)
5. Build + flash z CubeIDE → na PC nový COM port → test konzole
6. Merge do `main`
