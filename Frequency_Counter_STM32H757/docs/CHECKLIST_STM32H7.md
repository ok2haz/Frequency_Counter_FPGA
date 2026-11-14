# CHECKLIST_STM32H7.md — kontrolní body pro STM32H757BIT6

Seznam míst, kde na H7 (a zvlášť na dual-core H757) vzniká většina reálných
nefunkčností a nestabilit. U každého bodu je uvedeno, **jak se ověřuje**.
Nezaškrtávej bod, který jsi nedoložil odkazem `soubor:řádek`.

---

## A. Napájení, hodiny, náběh

- [ ] **`HAL_PWREx_ConfigSupply()` odpovídá zapojení desky** (LDO / SMPS / direct SMPS).
      Nesoulad = MCU po resetu nenaběhne nebo naběhne jen s debuggerem.
      *Ověř:* schéma vs. volání v `main.c` / `SystemClock_Config`.
- [ ] **Voltage scaling vs. cílová frekvence.** 480 MHz vyžaduje VOS0 + overdrive
      (`HAL_PWREx_EnableOverDrive`); rev Y silikonu je limitovaná na 400 MHz.
      *Ověř:* revizi čipu, `PWR_REGULATOR_VOLTAGE_SCALEx`, PLL násobiče.
- [ ] **Skutečné frekvence sběrnic** (SYSCLK, HCLK, D1/D2/D3 PCLK) proti tomu, co
      kód předpokládá v prescalerech (UART baudrate, I2C TIMINGR, SPI, TIM).
      *Ověř:* přepočítej ručně z RCC konfigurace, nevěř komentářům.
- [ ] **TIMPRE / násobič hodin timerů** — timer clock bývá 2× PCHK. Chyba dává
      dvojnásobné/poloviční periody.
- [ ] **FLASH latency (`FLASH_ACR_LATENCY`) a `WRHIGHFREQ`** odpovídají HCLK a VOS.
      Nízká latence při vysoké frekvenci = náhodné hardfaulty.
- [ ] **HSE: krystal vs. bypass**, CSS zapnutý, chování při selhání HSE
      (padá to na HSI 64 MHz → všechna časování jsou špatně, ale zařízení "jede").
- [ ] **Hodiny periferií povoleny PŘED prvním zápisem do jejich registrů**
      a pro správné jádro (viz sekce B).
- [ ] **Hodiny SRAM domény D2** (`AHB2ENR` SRAM1/2/3) povoleny, pokud se D2 SRAM používá.
      Bez toho čtení vrací nesmysly nebo busfault.
- [ ] Zapnutá **kompenzace I/O** (`SYSCFG` CCCSR) tam, kde to návrh vyžaduje (rychlé FMC/QSPI).

## B. Dual-core CM7 + CM4 (nejčastější zdroj "záhadných" chyb)

- [ ] **Boot sekvence je explicitní a bezpečná.** Zkontroluj option byty BCM7/BCM4
      a to, že kód s nimi souhlasí. Doporučený vzor: CM7 inicializuje hodiny,
      CM4 čeká v STOP na HSEM notifikaci.
      *Ověř:* `HAL_HSEM_ActivateNotification`, `HAL_PWREx_ClearPendingEvent`,
      `HAL_PWREx_EnterSTOPMode` v projektu CM4; `HAL_HSEM_FastTake/Release` v CM7.
- [ ] **Žádné jádro nekonfiguruje hodiny "podruhé".** CM4 nesmí volat
      `SystemClock_Config`, pokud to už udělal CM7.
- [ ] **Správné `CORE_CM7` / `CORE_CM4` definice** v obou buildech. Při záměně
      makra `__HAL_RCC_*_CLK_ENABLE()` píší do registrů druhého jádra (`RCC_C1_*`
      vs. `RCC_C2_*`) → periferie se nikdy nerozjede.
- [ ] **EXTI a NVIC per jádro.** EXTI má oddělené masky (`C1IMR` / `C2IMR`);
      přerušení musí být povolené na tom jádře, které ho obsluhuje.
- [ ] **Sdílená paměť: shodná adresa i velikost v obou linker skriptech**,
      shodná definice struktury, ideálně `_Static_assert(sizeof(...) == N)`.
      *Ověř:* diff obou `.ld` + hlavičku sdílených typů.
- [ ] **Sdílená paměť je nekešovaná nebo cache-managed na obou stranách**
      (MPU konfigurace v obou projektech!). Jinak jedno jádro vidí staré hodnoty.
- [ ] **Vzájemné vyloučení přes HSEM**, ne přes "flag v RAM". Rezervace HSEM ID
      je zdokumentovaná a nekoliduje.
- [ ] **Přechod CM4 do STOP nesmí shodit D2 domény**, které používá CM7
      (většina periferií je v D2). *Ověř:* `PWR_CPU2CR`, `HAL_PWREx_*` nastavení
      RUN_D3/D2 a co se stane, když jedno jádro spí.
- [ ] **Překladové příznaky per jádro:** CM7 `-mcpu=cortex-m7 -mfpu=fpv5-d16`,
      CM4 `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16`, konzistentní `-mfloat-abi`.
      Sdílená statická knihovna přeložená pro jedno jádro = hardfault na druhém.
- [ ] **Vlastnictví periferií je jednoznačné** (tabulka v `ARCHITECTURE.md`).
      Dvě jádra do jedné periferie bez zámku = nestabilita.

## C. DMA, cache, umístění bufferů (2. nejčastější zdroj)

- [ ] **DMA1/DMA2 nemají přístup do DTCM** (`0x2000_0000`). Buffer pro DMA v DTCM
      = přenos se nikdy neuskuteční nebo padá busfault. MDMA do DTCM umí.
      *Ověř:* sekce každého DMA bufferu v `.map`, ne jen atribut ve zdrojáku.
- [ ] **BDMA umí jen domény D3** (SRAM4 `0x3804_0000`) a D3 periferie
      (LPUART1, SPI6, I2C4, ADC3, LPTIM2/3, SAI4).
- [ ] **Cache maintenance u každého DMA přenosu:**
      TX = `SCB_CleanDCache_by_Addr` před startem; RX = `SCB_InvalidateDCache_by_Addr`
      po dokončení, ne před.
- [ ] **Zarovnání a velikost pro cache operace na 32 B** (délka cache line).
      Buffer nezarovnaný na 32 B → invalidace zničí sousedící data.
      *Ověř:* `__attribute__((aligned(32)))` a zaokrouhlení délky nahoru.
- [ ] **MPU oblasti pro DMA buffery** (Device / Non-cacheable) skutečně pokrývají
      celý buffer, mají povolenou velikost (2^n) a správné zarovnání,
      a MPU je zapnutá **před** zapnutím cache.
- [ ] **Deskriptory Ethernetu / USB / SDMMC v nekešované oblasti.**
- [ ] Kombinace **`SCB_EnableDCache()` + zapisování do periferních struktur v RAM**
      bez barrier (`__DSB()`, `__DMB()`) — zkontroluj pořadí operací.
- [ ] **Double-buffer / circular DMA:** obsluha `HT` i `TC` callbacku, správný
      výpočet indexu z `NDTR`, žádné čtení "za" aktuální pozicí.
- [ ] **DMA stream se korektně vypíná a čistí příznaky** před rekonfigurací
      (jinak "DMA transfer error" po prvním úspěšném přenosu).
- [ ] **RAMECC / ECC:** dvojitá chyba ECC generuje NMI — existuje handler,
      nebo aspoň záznam do error logu?

## D. Přerušení, souběh, RTOS

- [ ] **`volatile` u každé proměnné sdílené mezi ISR a zbytkem kódu**
      (a mezi jádry, kde volatile nestačí — tam bariéry + nekešovaná paměť).
- [ ] **Čtení/zápis > 32 bit nebo struktur z ISR** není atomické → zámek nebo
      double-buffering.
- [ ] **Priority IRQ vs. RTOS:** každá ISR volající `...FromISR()` musí mít
      číselnou prioritu ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`.
      *Ověř:* `HAL_NVIC_SetPriority` pro každou použitou IRQ + `NVIC_PRIORITYGROUP_4`.
- [ ] **HAL timebase při RTOS** je na TIM (ne SysTick, který si bere FreeRTOS).
- [ ] **Žádné `HAL_Delay`, blokující `HAL_*_Transmit`, `malloc`, printf v ISR.**
- [ ] **Vyprázdnění příznaků periferie s bariérou** (`__DSB()` před návratem z ISR),
      jinak se ISR spustí podruhé (Cortex-M7 write buffering).
- [ ] **Zásobníky:** velikost MSP/PSP, `configCHECK_FOR_STACK_OVERFLOW`,
      hlídání `_sbrk` (heap vs. stack kolize), stack v DTCM a jeho velikost v `.ld`.
- [ ] **FPU v ISR** — u FreeRTOS zapnuté ukládání FPU kontextu; lazy stacking
      zvyšuje potřebu stacku o 132 B na úroveň.
- [ ] **Reentrance driverů** (dvě tasky do jednoho UART bez mutexu).
- [ ] **Watchdog:** IWDG1 pro CM7, IWDG2 pro CM4, debug freeze bity nastavené;
      kopání watchdogu **není** v ISR časovače nezávisle na tom, jestli aplikace žije.

## E. Ošetření chyb a robustnost

- [ ] **Návratové hodnoty `HAL_*` se vyhodnocují** (nebo je vědomě ignorováno
      a je to zdůvodněné komentářem). `HAL_OK` bez kontroly = tichá nefunkčnost.
- [ ] **Každá čekací smyčka má timeout** (`while (!(REG & FLAG))` je nález S2/S3).
- [ ] **`Error_Handler()` nedělá `while(1)` bez logu, resetu nebo watchdogu.**
- [ ] **HardFault/MemManage/BusFault/UsageFault handlery** ukládají alespoň
      minimální kontext (SCB->CFSR, HFSR, BFAR, zásobníkový rámec) do RAM,
      která přežije reset.
- [ ] **Chování po neočekávaném resetu**: `RCC_CSR` reset flagy se čtou a mažou.
- [ ] **Rozpoznání a ošetření vypadlé periferie** (I2C NACK/bus lock, SPI overrun,
      UART ORE/FE/NE, CAN bus-off, USB reset).

## F. Flash, konfigurace, bootloader

- [ ] **Zápis do Flash má granularitu 256 bitů (32 B) na banku** a musí být
      zarovnaný; částečný zápis končí chybou.
- [ ] **Read-while-write:** čtení z banky, do které se zapisuje, stojí.
      Kód, který se za běhu přepisuje, musí běžet z druhé banky / RAM.
- [ ] **Option byty a dual-bank / swap** — kód s nimi souhlasí, přepis option bytů
      je zdůvodněný (blokuje jádro, může "zamknout" desku).
- [ ] **RDP / write protection** nekoliduje s aktualizací firmwaru.
- [ ] **Přepínání vektorové tabulky (`SCB->VTOR`)** pro obě jádra a `SystemInit`.

## G. Periferie — časté konkrétní chyby na H7

- [ ] **SPI:** správné nastavení `TSIZE`, FIFO thresholdů, korektní ukončovací
      sekvence (jinak druhý přenos zamrzne); `MasterKeepIOState` u multi-slave.
- [ ] **I2C:** `TIMINGR` odpovídá reálné frekvenci hodinového zdroje periferie.
- [ ] **UART/USART:** `ORE` ošetřen; při DMA + IDLE line detekci ověř výpočet
      délky z `NDTR`; oversampling vs. baudrate na vysokých rychlostech.
- [ ] **ADC:** ADC1/2 (D1/AXI) vs. ADC3 (D3 → jen BDMA); boost mode pro rychlé
      hodiny; provedena kalibrace (offset + linearita); 16bit vs. 12bit zarovnání.
- [ ] **OCTOSPI/QSPI memory-mapped:** MPU atributy (Normal, cacheable, XN pro data),
      timeout counter, chování při čtení nevalidní adresy.
- [ ] **FMC/SDRAM:** refresh count z reálné HCLK, MPU pro externí RAM,
      žádné bufferované zápisy před resetem řadiče.
- [ ] **Ethernet:** deskriptory nekešované, správný PHY reset a autonegotiace,
      `RMII` hodiny.
- [ ] **USB HS/FS:** napájení PHY, ULPI hodiny, buffery mimo DTCM.
- [ ] **SDMMC:** 4bit/8bit, hodiny ≤ limit, buffery zarovnané a nekešované.

## H. Errata a verze

- [ ] **Errata sheet pro konkrétní revizi silikonu projitá**, dotčené workaroundy
      jsou v kódu a jsou označené komentářem s číslem erraty.
- [ ] **Verze HAL/CMSIS** zaznamenána; kód nepředpokládá chování opravené/rozbité
      v jiné verzi.
- [ ] **Warningy překladače nejsou vypnuté** globálně (`-w`, `#pragma GCC diagnostic
      ignored` bez zdůvodnění).
