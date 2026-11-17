# Kontrola: požadavky na firmware z revize HW desky STM32H747/757 rev. 2.1 (2026-11-15)

Fáze: AUDIT, kód se nemění. Vstup: `POZADAVKY_FW_Z_HW_REVIZE_2026-11-15.md` (druhá instance, kontrola KiCad rev. 2.1).
⚠️ Deska rev. 2.1 **není osazená ani připojená**; dnes běží současná deska (KiCad v repu `STM32H747BIT/`). Úprava pro
rev. 2.1 nesmí rozbít současnou desku a na HW ji zatím ověřit nejde.

## Výsledek po bodech

| # | Požadavek | Stav ve FW | Doklad | Poznámka |
|---|---|---|---|---|
| 1 | SMPS → LDO jádra | ✅ | `CM7/Core/Src/main.c:614` (`PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO`), kontrola `main.c:224` | neměnit |
| 2 | HSE 25 MHz bypass | ✅ | `main.c:632` `RCC_HSE_BYPASS` | X1 = ECS TXO-3225MV-250: výstup **HCMOS** (datasheet ECS), bypass sedí |
| 3 | VREFBUF jako reference | ✅ | `main.c:455-469` (`__HAL_RCC_VREF_CLK_ENABLE`, SCALE0, HiZ off, enable) | |
| 4 | teplota čipu, VBAT na ADC3 | ✅ | `CM7/Core/Src/freertos_task_sensors.c:933-975` | |
| 4b | VBAT < ~1,5 V = „baterie chybí“, ne „vybitá“ | ✅ v0.45.22 (`04f8940`), ⬜ HW | `freertos_task_sensors.c:973` jen `pin + vbat_corr_mv`; alarm/autocal znají jen 2,70 / 2,40 V | |
| 4c | rev. 2.1 bez diody → korekce VBAT 0 | ⚠️ | `g_calib.vbat_corr_mv` výchozí 75,6 mV (dioda D3 současné desky) | na rev. 2.1 nastavit v okně Kalibrace na 0 (nebo podle revize) |
| 5 | nabíjení CR2032 vypnuté | ✅ | `freertos_task_sensors.c:709` `HAL_PWREx_DisableBatteryCharging()` | neměnit |
| 6 | PG12 (ETH_INT) jen vstup bez pull-upu | ✅ | `CM4/Core/Src/gpio.c:58-62` | neměnit |
| 7a | VDDA naběhne později → opakovat zapnutí VREFBUF | ✅ v0.45.22 (`04f8940`), ⬜ HW | `main.c:468-469` jen `[WARN] VREFBUF nenabehl`, žádný další pokus | |
| 7b | VREF+ mimo 2,4-2,6 V → teplota a VBAT neplatné | ✅ v0.45.22 (`04f8940`), ⬜ HW | `freertos_task_sensors.c:937-975` počítá s `vref` bez kontroly | týká se i současné desky (VREF+ 2,515 V) |
| 8 | USB-C: PI3 (EN TPS2065C) hned v 0, VBUS jen při připojeném spotřebiči, /FLT PI8, FUSB302 INT PI0 | 🔴 chybí (jen rev. 2.1) | v `.ioc` ani kódu nic | TPS2065C: EN aktivní v log. 1 (TI) → 0 = vypnuto. **Na současné desce vede PI3 / PI8 / PI0 na konektor** (síť `PI3`, `PI8`, `TIM5_CH4`, `STM32H747BIT/Connectors.kicad_sch`) → buzení jen podmíněně pro rev. 2.1 |
| 9 | PH1 (QSPI /RESET) výstup v log. 1 před `MX_QUADSPI_Init` | ✅ v0.45.22 (`04f8940`), ⬜ HW | `H757_LED.ioc:744-750` `PinAttribute=Free` → negeneruje se; v `gpio.c` není | **týká se i současné desky** (PH1 = `QUADSPI_BK1_RESET` i tam); dnes funguje, HYPOTÉZA: W25Q512JV má na /RESET vnitřní pull-up (W25Q32JV ho má, u 512JV nepotvrzeno) |
| 10 | bývalé analogové piny PA0/3/4/5/6, PB0/1 | ✅ (nepoužívají se) | v `.ioc` ani kódu nenastavené → analogový režim po resetu | při budoucím použití jako GPIO `GPIO_SPEED_FREQ_LOW` |
| 11 | I2C2 (PH4/PH5) bez pull-upů | ✅ (nepoužívá se) | I2C2 není v kódu | při zavedení: timeouty, obnova 9 pulzy SCL, analogový filtr + DNF 2-4 |

## Návrh oprav (po odsouhlasení, pořadí podle rizika)

1. **PH1 v log. 1** v `USER CODE BEGIN SysInit` (`main.c:347`, před `MX_GPIO_Init` / `MX_QUADSPI_Init`), bez pulzu
   (`w25q_init` dělá softwarový reset 66h/99h). Platí pro obě desky, `.ioc` se nemění. Cena: jeden zápis GPIO při startu.
2. **VREF+ věrohodnost** (7b) v SensorsTask: mimo 2400-2600 mV → `sensor_fail` pro `SENS_CORE_T`, `SENS_VBAT` (a `SENS_VDDA`
   zůstane s hodnotou + chyba), počítadlo do `status`. Obě desky.
3. **Opakování VREFBUF** (7a): když `VRR` při startu nenaběhne, SensorsTask zkusí `HAL_SYSCFG_EnableVREFBUF` znovu (po 100 ms,
   nejvýš 10×) a stav hlásí `status`. Obě desky.
4. **VBAT chybí** (4b): < 1500 mV = stav „chybí“ (bez alarmu vybité baterie), v `status`, okně Diagnostika a webu.
5. **USB-C VBUS (rev. 2.1)**: přepínač revize desky `HW_REV_STM` (výchozí současná deska) a pro rev. 2.1 PI3 výstup v 0 co
   nejdřív (SysInit), PI8 vstup; logika FUSB302 (detekce Rd na CC, cizí VBUS, /FLT, chyba komunikace → VBUS vypnout) až
   s osazenou deskou. Do té doby PI3 trvale 0 (USB-C jen jako zařízení), jak doporučuje požadavek 8.4.
6. **Korekce VBAT podle revize** (4c): výchozí `vbat_corr_mv` 0 pro rev. 2.1.

⬜ Nic z toho nejde na rev. 2.1 ověřit, dokud deska není osazená; body 1-4 jdou ověřit na současné desce.

**Opraveno 2026-11-17 (v0.45.22, `04f8940`, uživatel schválil body 1-4):** návrhy 1-4 provedeny podle tabulky výše (PH1 v `main.c` SysInit; `vrefbuf_service` a kontrola VREF+ v SensorsTasku, meze `VREF_OK_MIN/MAX_MV`; `vbat_missing()` v `sensor_stat.h` pro alarm, AUTO-CAL, Diagnostiku, PRAHY, `status` a web). Odchylka od návrhu 3: zapnutí VREFBUF se opakuje 1× za cyklus ADC3 (1 s), ne po 100 ms; `status` čte VRR přímo z `VREFBUF->CSR`. Návrhy 5 (USB-C, rev. 2.1) a 6 (korekce VBAT podle revize) zůstávají otevřené (#387, #386). Lekce L-0290..L-0292. ⬜ neověřeno na HW.
