# ARCHITECTURE.md — skutečný stav projektu

> Vyplňuje se ve fázi F1 auditu. **Jen doložená fakta** s odkazem `soubor:řádek`.
> Nedoložené položky zůstávají `?` — to je samo o sobě nález (chybí dokumentace).
>
> **Stav:** doplněno 2026-09-10 z hotových auditů modulů 1–4 a I2C. Všechna čísla níže
> jsou **přepočítaná nebo dohledaná v obrazu** (`nm`, `.map`, `status` z běžícího přístroje),
> ne opsaná z komentářů — to je celý smysl tohoto souboru.

## 1. Rozdělení jader

| Jádro | Frekvence | Role | Vstupní bod | Boot |
|---|---|---|---|---|
| CM7 | **480 MHz** (CPU), HCLK/AXI 240 MHz | veškerá logika: FreeRTOS, displej, měření, senzory, úložiště | `CM7/Core/Src/main.c` `main()` | bank1 `0x08000000`, option byte `BOOT_CM7_ADD0` |
| CM4 | HCLK 240 MHz, bez FreeRTOS (bare-metal smyčka) | konektivita: ETH / lwIP / SCPI / HTTP | `CM4/Core/Src/main.c` `main()` | bank2 `0x08100000`, option byte `BOOT_CM4_ADD0` |

- **`SCB->VTOR` nenastavuje software ani na jednom jádře** — `USER_VECT_TAB_ADDRESS` je
  zakomentovaný (`Common/Src/system_stm32h7xx_dualcore_boot_cm4_cm7.c:93`), boot řídí option byty.
  Že remap funguje, dokládá provoz: obě jádra obsluhují přerušení a CM4 běží z banky 2.
- Synchronizace při náběhu: **HSEM 0**. CM7 `HAL_HSEM_FastTake` + `Release` (`main.c:325-327`);
  CM4 `HAL_HSEM_ActivateNotification` → `HAL_PWREx_EnterSTOPMode(…, PWR_D2_DOMAIN)` → `CLEAR_FLAG`.
  Obě čekání na `RCC_FLAG_D2CKRDY` mají **timeout** a při jeho vypršení nastaví `g_cm4_absent`
  a pokračují degradovaně (`main.c:230-235`, `:263-268`) — nespadnou do `Error_Handler`.
- **HSEM 1** serializuje konfiguraci sdílených GPIO mezi jádry (`freertos.c:391-416`).

## 2. Mapa paměti (skutečná, z linker skriptů a obrazu)

| Oblast | Adresa | Velikost | Kdo používá | Cache (CM7) | Poznámka |
|---|---|---|---|---|---|
| ITCM | `0x00000000` | 64 kB | **nikdo** | — | v linkeru deklarované, žádná sekce |
| DTCM | `0x20000000` | 128 kB | **nikdo** | — | linker sem neumisťuje nic → `L-0001` splněno konstrukcí |
| AXI SRAM (D1) | `0x24000000` | 512 kB | `.data`, `.bss`, halda, zásobník | default WBWA | `.bss` končí `0x2402C408`, `_estack` `0x24080000` → **~343 kB rezervy** |
| SRAM1 (D2) | `0x30000000` | 128 kB | CM7 sem nelinkuje nic; `membench`/`ram write` absolutní adresou | default WBWA | vyžaduje `__HAL_RCC_D2SRAM1_CLK_ENABLE` |
| SRAM2 (D2) | `0x10020000` | 128 kB | CM4 `.data`/`.bss` (alias jádra CM4) | CM4 nemá cache | |
| SRAM3 (D2) | `0x30040000` | 32 kB | ETH DMA deskriptory + RX pool | — | **systémová** adresa nutná pro DMA |
| SRAM4 (D3) | `0x38000000` | 64 kB | sdílená paměť IPC | **MPU R2: non-cacheable + shareable** | `g_ipc` je makro nad pevnou adresou |
| Flash bank 1 | `0x08000000` | 1 MB | obraz CM7 | — | `.text` ~597 kB (Release) |
| Flash bank 2 | `0x08100000` | 1 MB | obraz CM4 | — | ~231 kB |
| SDRAM FB | `0xC0000000` | 4 MB | FB0/FB1/FB2 (3×1 MB) + rezerva | **MPU R0: Write-Through** | WT proto, že LTDC čte přímo |
| SDRAM scratch | `0xC0400000` | 4 MB | `membench`, `screenshot` | **MPU R1: WBWA** | |
| SDRAM `.sdram` | `0xC0800000` | 4 MB | `s_glyph_atlas` 256 kB, **`bg_cache` 768 000 B** @`0xC0840000`, `g_mask_a/b` | **mimo MPU → Device** | Device kvůli koherenci s DMA2D |
| — volné — | `0xC0C00000` | 4 MB | | | |
| SDRAM `.measlog` | `0xC1000000` | 8 MB | `s_buf` (datová cache měření) | **MPU R3: WBWA** | 262 144 záznamů po 32 B ≈ 18 h |
| — volné — | `0xC1800000` | 8 MB | | | rezerva pro zdvojnásobení logu |

Celkem obsazeno 20 MB z 32 MB SDRAM. Rozvrh je autoritativně zapsaný v
`CM7/STM32H757BITX_FLASH.ld:66-78` a shoduje se s MPU i s obrazem.

## 3. Konfigurace MPU (CM7; CM4 MPU nemá a nepotřebuje — nemá D-cache)

| # | Adresa | Velikost | Atributy | Účel |
|---|---|---|---|---|
| 0 | `0xC0000000` | 4 MB | Normal, **Write-Through**, XN | framebuffery (LTDC čte přímo) |
| 1 | `0xC0400000` | 4 MB | Normal, WBWA, XN | SDRAM scratch (čte jen CPU) |
| 2 | `0x38000000` | 64 kB | Normal, **non-cacheable + shareable**, XN | IPC CM7↔CM4 |
| 3 | `0xC1000000` | **8 MB** | Normal, WBWA, XN | `.measlog` |

- Všechny čtyři jsou mocnina 2, přirozeně zarovnané a **nepřekrývají se** (ověřeno výpočtem).
- **MPU se konfiguruje před `SCB_EnableDCache()`** — `main.c:211` vs. `:217`/`:220`. ✅
- ⚠️ Oblast 3 nenastavuje pole `Enable` a přebírá ho ze zbytku po oblasti 2 (audit F-0008).
- ⚠️ Komentář u oblasti 3 uvádí 16 MB, kód i linker mají 8 MB (audit F-0009).

## 4. Hodinový strom

- Zdroj: **HSE 25 MHz v režimu BYPASS** (`main.c:500`; `HSE_VALUE` shodné v obou `hal_conf.h`).
- **CSS je záměrně vypnutý** (`main.c:298-309`); zapíná se vědomě přes UART `css on`.
- PLL1: M=5 → ref 5 MHz (`PLL1VCIRANGE_2`), N=192 → **VCO 960 MHz** (`VCOWIDE`), P=2
  → **SYSCLK 480 MHz**. VCO 960 MHz umí až revize V — nezávislé potvrzení hypotézy o silikonu.
- **VOS0** = `D3CR.VOS` scale 1 **+** `SYSCFG_PWRCR.ODEN`; napájení
  `PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO` (Vcore z LDO, což je pro VOS0 podmínka).
- Sběrnice: D1CPRE=1 (CPU 480), **HCLK/AXI 240 MHz**, APB1/2/3/4 = /2 → **120 MHz** (všechny).
- FLASH: `LATENCY_4`; `WRHIGHFREQ` **= 3** (odečteno z běžícího přístroje přes `status`).
- **I/O kompenzační cela zapnutá** (`main.c` `USER CODE SysInit`, před `MX_FMC_Init`), napájená CSI.

**Odvozené kernel clocky (přepočítáno, ne opsáno):**

| Konzument | Zdroj | Frekvence | Ověření |
|---|---|---|---|
| FMC / SDRAM | PLL2**R** (VCO 200/2) | 100 MHz → **SDCLK 50 MHz** (`CLOCK_PERIOD_2`) | `REFRESH_COUNT` 371 z toho vychází přesně |
| LTDC | PLL3**R** (VCO 175/7) | **25 MHz** | 850×510 taktů → **57,7 Hz** na 800×480 |
| ADC | PLL3**R**, pak `ASYNC_DIV8` | 25 / 8 = **3,125 MHz** | |
| SPI123 (SPI2/FPGA) | PLL2**P** (VCO 200/1) | **200 MHz** | driver si dělič počítá za běhu |
| SDMMC | PLL1**Q** (960/15) | 64 MHz → SDMMC_CK **16 MHz** | `ClockDiv=2` |
| USB | HSI48 | 48 MHz | `usbd_conf.c:78-79` |
| I2C1 / I2C4 | PCLK1 / PCLK4 | **120 MHz** | ⚠️ `TIMINGR` dává **~50 kHz**, ne 100 (F-0004) |
| TIM6 / TIM7 / TIM1 | 2× PCLK (TIMPRE=0) | **240 MHz** | timebase i beeper to počítají správně |

⚠️ **LTDC a ADC sdílejí jeden dělič PLL3R** — změna pixel clocku mění hodiny ADC a naopak.

## 5. Vlastnictví periferií

| Periferie | Doména | Jádro | Vlastník (úloha) | Zámek |
|---|---|---|---|---|
| I2C4 (ATTINY 0x45, TMP117 0x48, FT5x06 0x38) | D3 | CM7 | UiTask (dotyk, jas), SensorsTask (TMP117) | `i2c4MutexHandle` |
| I2C1 (TMP117 0x49/0x4A, ADS1115 0x48, Si5356 0x70) | D2 | CM7 | **výhradně SensorsTask** (zápisy) | `i2c1MutexHandle` |
| SPI2 (FPGA) | D2 | CM7 | FpgaTask | mutex v driveru |
| QUADSPI (W25Q) | D1 | CM7 | defaultTask, UiTask, UartTask | `qspiMutexHandle` |
| LTDC + DMA2D + FMC | D1 | CM7 | **výhradně UiTask** (libprim/libui není thread-safe) | — |
| RTC / BKP | D3 | CM7 | **výhradně defaultTask** (registry) | — |
| USART1 (GPS) | D2 | CM7 | ISR → fronta → defaultTask | fronta |
| ETH / lwIP | D2 | **CM4** | hlavní smyčka CM4 | — |
| GPIOG (sdílený!) | — | **oba** | CM7 FMC/QSPI, CM4 ETH/LED | HSEM 1 + `gpio_guard_tick()` |

## 6. Přerušení

| IRQ | Jádro | Priorita | Volá RTOS API? | Handler |
|---|---|---|---|---|
| USART1 | CM7 | **5** | ano (`osMessageQueuePut` přes CMSIS wrapper) | `usart.c` callbacky |
| SDMMC1 | CM7 | **5** | ano | `stm32h7xx_it.c` |
| OTG_FS (USB) | CM7 | **5** | ano | USB device |
| TIM7 (beeper) | CM7 | 6 | ne | `HAL_TIM_PeriodElapsedCallback` |
| TIM6 (HAL tick) | CM7 | **15** (`TICK_INT_PRIORITY`) | ne | timebase |
| PendSV / SysTick | CM7 | 15 | — | FreeRTOS port |
| NMI (CSS) | CM7 | −2 | ne | zapíše black-box, pak zamrzne (IWDG) |

`NVIC_PRIORITYGROUP_4` nastavuje `HAL_Init()` (`stm32h7xx_hal.c:147`).
`configMAX_SYSCALL_INTERRUPT_PRIORITY = 5` → **všechny obsluhy volající RTOS API jsou ≥ 5** ✅
(nejtěsnější je USART1 přesně na 5, což je povolené).
`configASSERT` je definovaný, takže běží i runtime kontrola `vPortValidateInterruptPriority`.

## 7. Tasky (CM7) — rezervy zásobníku změřené na běžícím přístroji (`stats`)

| Task | Priorita | Volný stack | Watchdog | Blokující práce |
|---|---|---|---|---|
| UartTask | Normal (24) | 1024 B | **ne** | `scanner`, `membench`, QSPI, SD — smí blokovat |
| defaultTask | Normal (24) | 1728 B | krmí IWDG | RTC, GPS, syscfg, datalog |
| FpgaTask | Normal (24) | 988 B | **ano** (heartbeat) | SPI2 poll 20 Hz |
| UiTask | BelowNormal (16) | 5268 B | **ano** (heartbeat) | veškeré kreslení + dotyk |
| I2C4Task (senzory) | Low (8) | 932 B | ne | I2C1/I2C4 čtení 2 Hz |
| Tmr Svc | 2 | 852 B | — | — |
| IDLE | 0 | 420 B z 512 B | — | — |

Halda FreeRTOS: 32 768 B, volných 11 064 B, **min-ever shodné s aktuálním** → po rozjezdu
se už nealokuje. `configCHECK_FOR_STACK_OVERFLOW = 2`.

⚠️ UartTask má **vyšší prioritu než UiTask** a smí blokovat sekundy → viz audit F-0020.

## 8. Moduly a závislosti

```
main.c ─ SystemClock_Config ─ PeriphCommonClock_Config
   ├── MPU_Config ──────────── SDRAM (FMC) ── LTDC ── DSI ── TC358762 ── panel
   ├── I/O kompenzace + CSI ── (musí být před FMC)
   ├── IPC (SRAM4) ─────────── CM4: ETH ── lwIP ── SCPI/HTTP
   ├── FreeRTOS ── UiTask ──── libui ── libprim ── DMA2D
   │              ├ FpgaTask ─ SPI2 ── FPGA
   │              ├ Sensors ── I2C1 ── TMP117/ADS1115/Si5356
   │              ├ UartTask ─ konzole, diagnostika
   │              └ default ── RTC, GPS, syscfg, datalog, watchdog
   └── W25Q (QSPI) ────────── syscfg / calib / datalog / flightrec / errlog
```

## 9. Otevřené otázky pro zadavatele

1. **Revize silikonu není změřená** — VOS0 + VCO 960 MHz ji implikují, ale `HAL_GetREVID()`
   nikdo nespustil. Jedno čtení to uzavře.
2. **Skutečná frekvence I2C nebyla ověřena osciloskopem** (F-0004) — přepočet dává ~50 kHz.
3. **`FMC_A9`/`PF15`** prozvoněn 2026-09-09, v pořádku; adresní překryv definitivně vyloučen
   po opravě čtecí cesty FMC.
4. **Reset ATTINY není vyveden na GPIO** — dokud to platí, firmware na zatuhlou I2C4 nedosáhne.
