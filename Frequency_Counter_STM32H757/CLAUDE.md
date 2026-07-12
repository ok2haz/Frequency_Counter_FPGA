# H757_LED — projektová poznámka

## Přehled
STM32H757 dual-core (CM4 + CM7) projekt generovaný v STM32CubeIDE.
- **CM7** = hlavní jádro, veškerá logika (FreeRTOS, displej, SDRAM, I2C, UART).
- **CM4** = jednoduché (GPIO + timer).
- Veškerý vývoj displeje je v `CM7/Core/Src/`.

Hardware: STM32H757 → DSI (1 lane) → **TC358762** DSI-to-DPI bridge → Waveshare 800×480 IPS panel. ATTINY MCU @ I2C 0x45 (power/backlight/reset), TMP117 @ 0x48 (teplota), FT5x06 (touch).

## ⚠️ Funkční konfigurace displeje (nepřepisovat naslepo)
Displej funguje s **DSI BURST mode + RGB565**. Hard-won, jde to PROTI Linux rpi-6.6.y referenci:
- **RGB565 (ne RGB888)** odstranilo statický per-řádek shear (3bajtový RGB888 pixel se v DSI streamu rozjížděl; 2bajtový RGB565 sedí).
- **BURST (ne NB_PULSES/sync-pulse)** dal správné barvy (NB_PULSES způsoboval rotaci R→G→B).
- Linux driver předpokládá pre-init bridge přes Pi GPU firmware — na bare STM32 hostu neplatí.

Pokud displej regreduje (shear / špatné barvy), zkontroluj NEJDŘÍV `dsihost.c`: `VidCfg.Mode==DSI_VID_MODE_BURST` a `ColorCoding==DSI_RGB565`.

**⚠️ I2C4 timing NEMĚNIT ručně.** Panel power + backlight jdou přes ATTINY @ 0x45 na I2C4. Ručně spočítaná 400 kHz hodnota (`0x10903163`) na HW nefungovala → probe panelu selhal → **úplně tmavý displej**, a navíc zasekla ATTINY na sběrnici (drží SDA) → nepomohl reflash, jen **úplný power-cycle desky i panelu**. Funkční hodnota = `0x70303AEE` (~100 kHz). Vyšší rychlost jen přes CubeMX Fast Mode + ověřit SCL osciloskopem.

## Klíčové proměnné

### Hodiny (main.c)
| Veličina | Hodnota | Zdroj |
|---|---|---|
| HSE | 10 MHz (bypass) | |
| SYSCLK | 480 MHz | PLL1 M=1, N=96, P=2 |
| Pixel clock (LTDC) | 25 MHz | PLL3 N=17, FRACN=4096, R=7 |
| DSI bit clock | 700 Mbps/lane | DSI-PLL NDIV=70, IDF=1, ODF=1 |
| DSI byte clock | 87.5 MHz | bit/8 |
| DSI escape clock | 17.5 MHz | TXEscapeCkdiv=5 |

### DSI VidCfg (dsihost.c)
- Mode = **DSI_VID_MODE_BURST**, ColorCoding = **DSI_RGB565**, NumberOfLanes = 1, PacketSize = 800
- H (byteclk): HSA=7, HBP=161, HLINE=2975
- V: VSA=2, VBP=21, VFP=7, VACT=480
- HS/VS polarity = ACTIVE_LOW
- PhyTimings (clk HS2LP/LP2HS, data HS2LP/LP2HS, StopWait) = 35/35/35/35/10

### LTDC (ltdc.c)
- PixelFormat = **RGB565**
- H: HSA=2, HBP=46, HACT=800, HFP=2 → total 850
- V: VSA=2, VBP=21, VACT=480, VFP=7 → total 510
- FBStartAdress = 0xC0000000, Image 800×480

### TC358762 bridge (tc358762.c)
- LCDCTRL = **0x00100050** (RGB565, VTGEN on), SYSCTRL = 0x040F, LPTXTIMECNT = 3
- LCD H: HSW=2, HBP=47, HDISP=800, HFP=1 (rpi modeline)
- LCD V: VSW=2, VBP=21, VDISP=480, VFP=7

### Framebuffer + MPU (main.c)
- 0xC0000000 (SDRAM/FMC), RGB565, 800×480×2 = 750 KB / buffer
- **⚠️ Boot vyčistí FB na černo** (`memset(0xC0000000, 0, 3 MB)` v USER CODE 2, za MX_FMC_Init): SDRAM přežije **soft reset** → jinak LTDC při bootu krátce zobrazí poslední snímek před restartem. (Kosmetika; hlavní „boot do trendu" bug byl v touchi — viz níže.)
- **MPU region 0: 4 MB, Write-Through** (dříve 2 MB — rozšířeno kvůli triple bufferingu)
- **SDRAM = 32 MB** (FMC: 9 col + 13 row bits × 4 banky × 16 bit).
- **SDRAM mapa:** **Region 0 (4MB WT, `0xC0000000`):** triple buffer **FB0 `0xC0000000`, FB1 `0xC0100000`, FB2 `0xC0200000`** + **off-screen canvas pool `0xC0300000`** (1 MB), vše RGB565, 1 MB stride. **Region 1 (4MB WBWA cached, `0xC0400000`):** `sdram` test buffer (`sdram write/read`) + scratch; dřív bignum workspace. **`.sdram` linker sekce `0xC0800000`** (16 MB, default/Device map): libprim glow scratch + `bg_cache` (předrenderované pozadí). SDRAM celkem 32 MB.

### Triple buffering / tearing-free (prim_stm32_hal.c) — AKTIVNÍ
- **3 framebuffery** (FB0 `0xC0000000` / FB1 `0xC0100000` / FB2 `0xC0200000`) v MPU region 0 (4 MB WT). Render cílí VŽDY skrytý **back**; `prim_stm32_present()` flipne LTDC na back **při vblanku** (`LTDC->SRCR=LTDC_SRCR_VBR`, NE `HAL_LTDC_SetAddress`=immediate → tearing). **Non-blocking:** čeká na PŘEDCHOZÍ flip, ne na aktuální → při nízké kadenci žádný ~17 ms spin (3. buffer garantuje, že copy-forward nepíše do scanovaného bufferu).
- **Dirty-rect copy-forward:** po flipu se do nového back zkopírují **jen změněné oblasti** (ne 768 KB) — levné. Sledování v DMA2D backendu: každý fill/blit zaznamená svůj obdélník (`mark_dirty`). ⚠️ **Každý partial redraw MUSÍ začít fill/blit (clear)**, jinak se ta oblast nezkopíruje dopředu (problikávání). Triple → nový back je 2 snímky starý → kopíruje se sjednocení dirty z posledních 2 snímků.
- **`present` jen při změně:** `draw_diag_values`/`screen_main_redraw_time` vracejí, zda kreslily; volající flipne jen pak (jinak zbytečný flip).
- Volá `app_gpsdo` po každém vykreslení; UiTask LTDC adresu neřídí.
- **⚠️ Cache koherence:** DMA2D obchází CPU D-cache → po každém fill/blit se zneplatní cílová oblast (`SCB_InvalidateDCache_by_Addr`); WT → bez dirty řádků. Bez toho AA hrany textu čtou stará data („px šum").
- **⚠️ `testRED`/`test` UART příkazy** píšou natvrdo do FB0 — při page-flipu nemusí být vidět (bring-up reziduum).
- **Historie:** buffer byl dočasně vypnut při hledání freezu — ten byl ale **printf v SensorsTasku** (malý stack), NE buffer; po opravě znovu zapnut. CPU dopad ~0 % (dirty-rect). Off-screen canvas API odstraněno (nepoužité).

### Akcelerace / linker
- Grafiku dělá `libprim`/`libui` (viz `CM7/GPSDO_UI_README.md`); DMA2D backend je volitelný v libprim.
- **DMA2D glyph blend (velký text bez CPU rasterizace):** `prim_draw_text` umí velké glyfy (`h≥24 px`, neprůhledná barva, neořezané) blendovat přes DMA2D místo CPU per-pixel smyčky. Glyf se JEDNOU expanduje do **A8 dlaždice v `.sdram` atlasu** (256 KB, Device paměť — DMA2D čte přímo jako `bg_cache`), pak už jen HW blend (FG=A8 alfa, barva z `FGCOLR`). Cache klíč = ukazatel na coverage data. **Default VYPNUTO**; zapíná se cíleně přes `prim_set_glyph_accel(1/0)` — teď jen kolem **měřeného kmitočtu** (`screen_main.c`), zbytek textu jede CPU. Srazilo UiTask 85→58 %.
  - ⚠️ **Cache platí jen pro statické `const` fonty** (klíč = adresa coverage dat). Runtime-generované/škálované glyfy by ji rozbily (různé glyfy stejná adresa) → nutná invalidace. Cache nemá eviction (bump alokátor); po naplnění (96 položek / 256 KB) nové glyfy padají na CPU.
  - ⚠️ **Zrychlený glyf NEdělá `mark_dirty`** (jako CPU text) → spoléhá na dirty rect předchozího clearu (fill/blit) kvůli copy-forwardu přes 3 buffery. Každý partial redraw velkého textu MUSÍ začít clear, jinak bliká.
- **Pozn.: dřívější ITCM/DTCM sekce (`.itcm_text`/`.dtcm_data`) v linkeru + kopírovací smyčka v `main.c` USER CODE 1 byly odstraněny** — využíval je jen smazaný gfx hot-path, po jeho odebrání zůstaly prázdné (kopie byla no-op). K dohledání v git historii, kdyby bylo potřeba ITCM zrychlení vrátit.
- Pozn.: největší CPU výhra zůstává **-O2/Release**.

### FreeRTOS tasky (freertos.c)
| Task | Priorita | Stack |
|---|---|---|
| defaultTask | Normal | 1536 B (GPS drain + rtc_app_tick snprintf + syscfg persist + alarm_tick + watchdog_supervise + USB pump) |
| UartTask | Normal | 2048 B |
| I2C4Task | Low | 1536 B |
| UiTask | BelowNormal | 8192 B |
| FpgaTask | Normal | 2048 B |
| UartRxQueue | — | 64 × 1 B |
| GpsRxQueue | — | 256 × 1 B |

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

### Periferie
- USART1: 115200 8N1, TX=PB14, RX=PA10, NVIC prio 5. printf přes `_write` (blokující TX, timeout 10 ms ⇒ limit ~115 B/řádek).
- I2C4: TMP117 @ 0x48 (0.0078125 °C/LSB, 1×/s), panel ATTINY @ 0x45 (backlight 200), FT5x06 touch.

## UI vrstva (libprim / libui / app, UiTask)
Grafika je **třívrstvý in-place renderer** (detaily v `CM7/GPSDO_UI_README.md`, zadání v `.claude/zadani_*.md`):
- **`libprim`** — generická 2D primitiva: fill, shapes, path, gradient, glow, text (RGB565). Nezná `ui/*`.
- **`libui`** — vizuální slovník: theme, dimensions, komponenty (card, button, chart, bargraph, big_number, pill, sparkline, digit_group), ikony, fonty. Nezná `app/*`.
- **`app/`** — GPSDO hlavní obrazovka (`screens/screen_main.c`) + HAL most (`hal/stm32/prim_stm32_hal.c`).

Veřejné API: `app_gpsdo_render_main()` / `app_gpsdo_render_diag()` / `app_gpsdo_clear()` / `app_gpsdo_handle_touch()` / `app_gpsdo_tick()`.
- **Kreslí VÝHRADNĚ `UiTask`** (libprim/libui nejsou thread-safe). UART nastaví `g_screen_req` (3 = hlavní obrazovka, 4 = clear), UiTask ho obslouží. Obrazovka startuje automaticky po bootu.
- **Barevný model = RGB565** všude (FB i pipeline). `prim_color_t` je ARGB8888 jen jako pracovní barva v paměti (alfa matematika), na hranici FB se balí do RGB565.
- **Dotek tlačítek:** UiTask polluje FT5x06 (@ I2C4, ~15 Hz, pod `i2c4MutexHandle`), hranové spouštění, volá `app_gpsdo_handle_touch(799-x, 479-y)`. **Panel je zrcadlený v X i Y** (H+V flip). **⚠️ Pokus přesunout touch poll do SensorsTasku (uvolnit UiTask) SELHAL** — při saturaci CPU touch čtení v Low-prio tasku timeoutovalo → `vTaskDelayUntil` free-run → I2C4 33 % + touch nefunkční. Vráceno do UiTask (proven ~4,5 %).
- **FT5x06 MUSÍ číst celý touch frame** (`ft5x06_read_touch`, 31 B z TD_STATUS 0x02). Částečné čtení (jen 1. bod) → controller drží I2C při multitouchi → další transakce zatuhne → zamrznutí při 2. prstu. Parsuje se jen 1. bod, ale čte se celý rámec.
- **⚠️ Boot-priming touche** (`s_touch_primed` v UiTask): FT5x06 NENÍ resetem nulován → při Menu→Restart může uživatel **držet prst na „Ano" přes reset** a první poll po bootu vidí „down" jako hranu → spustí akci na souřadnici „Ano", která se na hlavní obrazovce zrcadlí na **kartu Trend** (= „bootuje do trendu"!). Dokud po bootu nevidíme „prst nahoře" (`t.valid==0`), doteky se **ignorují** (jen se sleduje `was_down`) → reziduální/držený dotek se absorbuje.
- **I2C4 mutex** (`i2c4MutexHandle`): TMP117 task + touch + backlight sdílí sběrnici. **Nepovolovat I2C4 interrupt v IOC.**

> **Historie:** dřívější ručně psané `gfx.c`/`touch_ui.c` UI i pokus o **LVGL v9** obrazovku (`lv_port_disp.c`, `ui_main_screen.c`, vendored `Middlewares/Third_Party/lvgl`) byly **odstraněny** a nahrazeny libprim/libui/app. K dohledání v git historii.

## UART příkazy (StartUartTask)
`led on/off`, `ram write/read`, `sdram write/read`, `temperature`, `sensors`, `adcraw`, `scanner`, `testDSI`,
`testRED`, `test` (RGB565 sanity), `touch`, `touchloop`, `scan1`, `si5356`, `freq`, `gps`, `gpsraw`, `rtc`, `fpgaraw`,
`fpgaloop`, `stats`, `status`, `ui`, `qspiid`/`qspitest`/`qspispeed`/`storetest` (W25Q),
**`beep`/`beep test`** (testovací pípnutí, mute platí i pro test — odpověď na to upozorní),
**`beep on`/`beep off`** (globální mute, persist BKP_DR2), **`selftest`** (čistě-logické unit testy
za běhu: CRC16 vektor, hystereze /4↔/16 přes `fpga_freq_select_core` (bezstavové jádro), GPS parser
helpery (`gps_selftest`), fmt_frac+hist_h vektory (`screen_main_selftest`) — žádný HW, žádný sdílený
stav; destruktivní testy zvlášť: `qspitest`/`storetest`), **`ping`/`screen main`/`clear`/`version`/`help`**.
`rtc` = RTC čas (`g_rtc_text`) + zda je synchronizovaný z GPS (viz „RTC").
`temperature` = TMP117 0x48 + příznak `(STALE)` při chybě čtení. `sensors` = dump všech 10 senzorů
(`last/min/max/avg`, stav OK/ERR, `err_total/streak/samples`) — viz `g_sensors[]`/`sensor_stat.h`.
Neznámý příkaz → `ERR unknown command`. `ui` i `screen main` znovu vykreslí hlavní obrazovku
(`g_screen_req=3`), `clear` ji smaže (`g_screen_req=4`). Odpovědi protokolu CRLF (`ping`→`pong`,
`version`→`FW_VERSION_FULL`).

## Verze firmware (version.h) — JEDNA definice pro UART i displej
`CM7/Core/Inc/version.h`: `FW_VERSION_FULL` = `"gpsdo-ui vX.Y.Z"` (SemVer). Používá ho **UART `version`**
i **displej** (okno „O přístroji" + boot splash) → dřív se lišily (UART `v0.2-diag` vs displej `v0.1`),
teď jsou identické. ⚠️ **Verzuj numericky KONZISTENTNĚ s Git:** každé zvýšení = commit + `git tag vX.Y.Z`
na tomtéž commitu (verze na displeji přesně = git tag → dohledatelnost buildu). PATCH=oprava, MINOR=feature, MAJOR=zlom.

## FPGA čítač kmitočtu (fpga_freq.c/h, FpgaTask, SPI2)
STM32 = SPI master, FPGA = slave. **SPI2**: master, mode 0, MSB, 8-bit, MOSI=PB15, SCK=PI1,
MISO=PI2, **CS=PB12 (manuál GPIO, active-low)**. Bring-up **1 MHz** (`fpga_freq_init` zvolí
prescaler dle `HAL_RCCEx_GetPeriphCLKFreq(SPI123)`). **SCK strop dle kontraktu FPGA (GW1NR-9 oversampling): cíl ≤6 MHz, absolutní max ~10 MHz** — `FPGA_SCK_TARGET_HZ` (hlídá `#error` na `FPGA_SCK_MAX_HZ`).
- **Timing (kontrakt):** CS setup/hold ≥1 µs (dáváme 2), **mezi rámci ≥20 µs** (FPGA potřebuje ~124 cyklů @10 MHz na složení rámce; dáváme 25). Prodlevy přes **DWT cyklový čítač** (`delay_us`, ne NOP-loop — DWT už zapnut pro runtime staty). Po bootu **`osDelay(250)` v StartFpgaTask** než se začne clockovat (FPGA piny 54–57 jsou config piny, musí dokončit load z flash).
- **CRC self-test (akceptační krok 1):** `fpga_freq_crc_selftest()` ověří `crc16("123456789")==0x29B1`; při selhání se `g_init_ok=0` a SPI komunikace se **nezahájí** (poll/restart hned vrací false).
- **Stav SPI/komunikace na displeji:** `fpga_freq_format_status()` skládá řádek `SPI <x.xx>MHZ LINK:OK/-- SEQ:<n> CRC:<n>` (rychlost SCK, živost linky, posl. SEQ, počet CRC chyb). FpgaTask ho po každém pollu uloží do `g_spi_text`/`g_spi_ok` (překreslí jen při změně), UiTask vykreslí stav SPI při překreslení hlavní obrazovky — **zeleně** když link žije, **červeně** když ne.
- **Pevný 64B full-duplex rámec**: MAGIC 0xA5, VERSION, TYPE, FLAGS/STATUS, SEQUENCE(LE32),
  PAYLOAD_LEN, RESERVED, 50B payload, CRC16(LE) na konci. CRC = **CRC-16/CCITT-FALSE** (0x1021/0xFFFF), pokrývá byte 0..61.
- Model: `FpgaTask` polluje **~20 Hz** (`osDelay(50)`), posílá **ACK** (TYPE 0x06, SEQUENCE=poslední) → FPGA full-duplex vrací aktuální **DATA** (TYPE 0x80). Platné = CRC ok + DATA_VALID + DATA_FRESH + nová SEQUENCE. Polling je úmyslně rychlejší než tempo měření (FPGA gate **0,25 s reciproké → ~4 nová měření/s**) kvůli nízké latenci; protokol je pull/ACK, takže rychlejší polling nezpůsobí ztrátu měření (FPGA shodí DATA_FRESH až po ACK té SEQ). Každé čerstvé měření FpgaTask naformátuje do `g_freq_text`/`g_freq_info`. **`xfer()` je pod SPI mutexem** (`s_spi_mtx` v driveru) — UART `fpgaraw`/`fpgaloop` jinak kolidoval s pollingem FpgaTasku (dva tasky na jednom SPI+CS).
- **Dvě odbočky JEDNOHO děliče (4-fázové reciproké měření):** **pin28 = /4** (primár, víc hran/nižší latence), **pin27 = /16** (rozšíření rozsahu), čtou se současně. ⚠️ **`/4` i `/16` jsou dva Q výstupy TÉHOŽ binárního čítače `MC100EP016A`** (Q1=÷4, Q3=÷16; front-end viz `../Frequency_Counter_FPGA_Module/FPGA_module_schematic.pdf` list 2) — tvarovač = `MAX9601` komparátor, strop řetězce **~1,4 GHz**. `fpga_freq_select()` volí zobrazovaný zdroj: **/4 dokud je bez chyby a < ~380 MHz, jinak /16** — s **hysterezí** (nahoru 380 MHz, zpět na /4 až pod 360 MHz; sticky stav → žádné přeblikávání zdrojů u prahu; volat jen z FpgaTasku). Rozsah: na pinu do ~100 MHz → reálně ~400 MHz (/4) / až ~1,4 GHz (/16, limit tvarovače). Headline ukazuje zvolený zdroj, info řádek `<src> PH:<present>/<fine> GATE:<ns>NS SEQ:<n>[ chyba]`.
  - ⚠️ **`/16` NENÍ nezávislý cross-check** (dřívější zavádějící popis): sdílí tvarovač i společný čítač s `/4`, takže společné chyby (výpadek tvarovače u 1,4 GHz, miscount čítače) se projeví v OBOU shodně → porovnání `freq_x100000` vs `freq16_x100000` je jen **downstream sanity** (chytne rozbité čítání/timestamp na jednom pinu ve FPGA, ne front-end). Nesoudělný poměr je s binárními odbočkami nedosažitelný; skutečně nezávislá kontrola by chtěla druhý samostatný dělič (HW). Dvě odbočky slouží hlavně **rozsahu + hladkému handoveru**, ne validaci.
- **Detekce ztráty signálu (SIGNAL_LOST):** FPGA má **autoritativní watchdog** — ~2,5 s bez dokončeného měření → `error_flags` bit1 SIGNAL_LOST + DATA_VALID=0. FpgaTask čte `fpga_freq_signal_lost()` (latch posledního DATA rámce, funguje i při VALID=0) a při ztrátě **nebo mrtvém linku** nastaví `g_freq_stale=1` → UiTask ztlumí kmitočet na **šedou** (čte `g_freq_text`/`g_freq_stale` při překreslení). **Dřívější SEQ-staleness heuristika ODSTRANĚNA** (falešně hlásila stale u nízkých kmitočtů, kde se reciproké okno legitimně protáhne) — teď se věří FPGA flagu.
- **Auto-re-START:** když ~3 s nepřijde žádný platný rámec (`fpga_freq_link_ok()`==0, tj. mrtvý link, ne jen „bez nového měření"), FpgaTask znovu pošle START (20 Hz polling → práh `fails>=60`) — pokrývá FPGA který nabootuje/resetuje až po STM32.
- DATA payload (`fpga_meas_t`, parse v `parse_data()`): `frequency_x100000`(abs12, /4), `edge_count`(20), `gate_time_ns`(28), `timestamp`(36), `channel`(44), `measurement_status`(45), `error_flags`(46), **`phase_status`(50)**, **`status2`(51)**, **`freq16_x100000`(52, /16)**. **`freq*_x100000` = reálný kmitočet × 1e5 (děličku /4 i /16 už zahrnuje FPGA → STM NEnásobí 4 ani 16); `gate_ns` ≈ 250e6 a kolísá.**
  - **`error_flags`:** bit0=`FPGA_ERR_MEAS` (/4 Δt==0), bit1=`FPGA_ERR_SIGNAL_LOST`, bit2=`FPGA_ERR_OVERFLOW` (okno >~21,5 s). **`status2`** bit0=`FPGA_ST2_DIV16_ERR` (/16 Δt==0).
  - **`phase_status`:** bity3:0=present[3:0] (živost 4 fází), bity7:4=fine_seen[3:0] (viděné jemné 2,5 ns kódy). **Zdravé = `PH:F/F`** (obě nibble plné). Mezera v fine_seen → chybějící/špatně posunutá fáze (kontrola 90° rozestupu). ⚠️ SEQUENCE roste pomaleji u nízkých f (okno čeká na hrany) — to NENÍ chyba, skutečnou ztrátu hlásí SIGNAL_LOST.
- **POZOR: SPI2 dřív používal `ShiftRegister_SendByte` (74HC595) přes PB12 — odstraněno**, PB12 je teď CS k FPGA. V IOC/main.h je PB12 pořád pod starým názvem **`SPI2_RCK`** (a PB4 = `SPI2_RES`, nepoužitý). CS pin si `fpga_freq_init` konfiguruje **sám** (push-pull, idle high) — regen-safe, nezávisí na gpio.c.
- **⚠️ CS boot level MUSÍ být HIGH:** `gpio.c` ručně upraven — `MX_GPIO_Init` budí PB12 na **`GPIO_PIN_SET`** (bylo RESET). Jinak STM drží CS asertovaný (LOW) od bootu až do `fpga_freq_init` (po scheduleru), tj. **během konfigurace FPGA z flash** → GW1NR-9 nemusí naběhnout, MISO mlčí (`RX0:FF`). **Při regeneraci z IOC nastav PB12 default Output Level = High.**
- **⚠️ SCK idle LOW (AFCNTR):** `fpga_freq_init` nastavuje `hspi2.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE` (CFG2.AFCNTR=1). Bez toho STM mezi přenosy uvolní SCK/MOSI piny → SCK plave, pull-up na FPGA ho táhne HIGH → FPGA vidí při CS↓ falešnou hranu, rozhodí počítání bitů → `RX0:FF`. (Projev na LA: „initial state of CLK does not match settings".) Regen-safe (v driveru, ne v `spi.c`).
- **Bring-up diagnostika:** status řádek při chybějícím linku ukáže `SPI <x.xx>MHZ NOLINK HAL:<OK|ERR> RX0:<hex> CRC:<n>` (HAL=stav přenosu, RX0=první bajt MISO). UART `fpgaraw` vypíše HAL stav + všech 64 přijatých bajtů. `RX0:FF`/samé FF = FPGA nebudí MISO (CS/SCK/MISO zapojení, zem, nebo FPGA neběží).
- Formát: `123.456.789,01234Hz` (tečky tisíce, čárka des., 5 míst, bez mezery před Hz). UART příkaz `freq` vypíše poslední hodnotu; diagnostika ukazuje `g_spi_text` + `g_freq_info`.
- **⚠️ REALITA ZOBRAZENÍ: velké číslo na hlavní obrazovce je zatím SIMULACE** (`screen_main.c` iteration-1: `freq_step()` random walk kolem 10 MHz, stejně tak trend, offset/σ, Allan — vše ze simulace). Reálné měření z FPGA teče jen do `g_freq_text`/`g_freq_info` (UART `freq` + diag okno). **Napojení reálných dat na headline + statistiky = hlavní otevřený úkol UI** (infrastruktura připravena: `g_freq_dirty`/`g_freq_stale`, digit segmenty s UI_DIGIT_SIGMA/FLOOR pro nejisté číslice, ADEV pyramida).
- **Signal bargraf = REÁLNÝ** (už ne simulace): RF vstupní výkon z **AD8307** log-detektoru přes ADS1115 **AIN1** (SensorsTask fast-path ~10 Hz). `app_gpsdo_tick_signal` převádí mV→dBm (`dBm = mV/AD8307_SLOPE_MV_DB + AD8307_INTERCEPT_DBM`, typ. 25 mV/dB, intercept −84 dBm), bargraf mapuje pásmo `RF_DBM_MIN..MAX` (−80..+10 dBm), text „−45.5 dBm". ⚠️ slope/intercept jsou datasheet-typické → přesná **kalibrace do CALIB store** (viz [[w25q-flash]]).

## FPGA strana protokolu (specifikace, co musí FPGA implementovat)
STM32 = SPI master (generuje SCK+CS), FPGA = slave. Pevný **64B full-duplex** rámec: STM32
vždy vyclockuje 64 B a FPGA ve stejném přenosu vrátí svůj 64B response. FPGA drží poslední
hotové měření v TX bufferu. Handshake je **in-band** (STATUS/FLAGS), žádné extra GPIO.

**SPI:** mode 0 (CPOL=0, CPHA=0), MSB first, 8-bit. Aktuálně ~0,78 MHz (cíl 1 MHz); **strop ≤6 MHz cíl / ~10 MHz max** (NE 20 MHz). Detaily viz `FPGA_INSTANCE_BRIEF.md`.
CS active-low (STM idle HIGH). Prodlevy STM (DWT): CS↓→1.SCK 2 µs, posl.SCK→CS↑ 2 µs, **mezi rámci 25 µs** (≥20 µs).
FPGA piny: PIN54=MOSI, PIN57=MISO, PIN55=SCK, PIN56=CS.

**64B rámec:**
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

**DATA payload (TYPE 0x80), offsety v payloadu / absolutní** (4-fázové reciproké, 2 předděliče):
| P-off | Abs | Size | Pole |
|---|---|---|---|
| 0 | 12 | 8 | frequency_x100000 (uint64 LE) — **pin28 /4** (primár) |
| 8 | 20 | 8 | edge_count (počet period v okně) |
| 16 | 28 | 8 | gate_time_ns (≈250e6, kolísá) |
| 24 | 36 | 8 | timestamp_10MHz_ticks |
| 32 | 44 | 1 | channel_id |
| 33 | 45 | 1 | measurement_status (bit0 VALID, bit1 FRESH) |
| 34 | 46 | 4 | error_flags (u32 LE): bit0 meas err(/4), bit1 SIGNAL_LOST, bit2 overflow |
| 38 | 50 | 1 | **phase_status**: bity3:0 present[3:0], bity7:4 fine_seen[3:0] (zdravé=0xFF) |
| 39 | 51 | 1 | **status2**: bit0 = chyba dělení pin27 (/16) |
| 40 | 52 | 8 | **freq16_x100000** (u64 LE) — **pin27 /16** |
| 48 | 60 | 2 | spare = 0 |

**Škálování:** `freq_x100000` i `freq16_x100000` = reálný kmitočet × 1e5, **dělička (/4, /16) už zahrnutá ve FPGA → STM NEnásobí**. `edge_count` = počet period (diag), ne Hz.

**Model:** STM32 polluje ~20 Hz, posílá ACK (TYPE 0x06, SEQUENCE = poslední přijatá) → FPGA full-duplex vrací DATA. Po validním ACK smí FPGA shodit DATA_FRESH. **Měření je platné když:** CRC OK ∧ DATA_VALID ∧ DATA_FRESH ∧ SEQUENCE ≠ poslední přečtená. SEQUENCE roste pomaleji u nízkých f (okno čeká na hrany) — ztrátu signálu hlásí error_flags bit1, ne zamrzlá SEQ.

**Formát kmitočtu (BEZ float):** `frequency_x100000` = kmitočet × 100000 (5 desetinných míst v Hz).
`integer_hz = v / 100000`, `frac = v % 100000`. Zobrazení: české oddělení tisíců **tečkou**,
desetinná **čárka**, přesně 5 míst, bez mezery před Hz → např. **`123.456.789,01234Hz`**.

## RTC (rtc.c/h, LSE 32.768 kHz na PC14/PC15) — disciplinovaný z GPS
RTC běží z **LSE krystalu 32.768 kHz** (PC14=OSC32_IN, PC15=OSC32_OUT), prescalery **127/255 → 1 Hz**, clock source **LSE** (ne LSI). Zapnuto **v CubeMX/.ioc** (RTC na CM7 kontextu) → `MX_RTC_Init` + HAL RTC driver vygenerované; LSE přidané v `SystemClock_Config`. Viz `CUBEMX_CHECKLIST.md` „RTC".
- **App vrstva je regen-safe v `rtc.c`/`rtc.h` USER CODE blocích** (jako `MX_I2C1_Init` v i2c.c — žádný nový soubor). `rtc_app_tick()` (telo v `USER CODE 1`) se volá z **defaultTask** (vedle GPS drainu), throttle **1 Hz** uvnitř.
- **Sync z GPS:** při platném a „sane" GPS fixu (`gps_get`, rok 2024–2099 atd.) srovná RTC z UTC — **první fix hned, pak re-sync každých 10 min** (`RTC_RESYNC_MS`, drift LSE ~ppm). Přesnost = přesnost GPS UTC, mezi syncy drží LSE.
- **Perzistence přes reset:** po syncu zapíše `RTC_SYNC_MAGIC` (0x32F2) do **BKP_DR0**; guard v `MX_RTC_Init` (`USER CODE Check_RTC_BKUP`) při tom magicu **přeskočí** defaultní `SetTime/SetDate 0:00` → RTC drží správný čas přes warm reset (dokud žije backup domain). ⚠️ Bez VBAT baterie přežije jen reset (NRST/SW/WDG), ne plný power-cycle.
- **BKP registry:** **DR0** = RTC sync magic; **DR1** = UI config (mode/chan/gate/run, magic `RTC_UICFG_MAGIC`, save `rtc_save_uicfg_if_dirty`); **DR2** = systémové nastavení (bity7:0 jas, bit8 mute, bit9 auto-dim en, bity10:15 auto-dim prodleva /15 s; magic `RTC_SYSCFG_MAGIC`); **DR3–DR5** = crash black-box (`RTC_CRASH_MAGIC`, hook → kind+jméno tasku, po přečtení smazáno); **DR6** = nastavení 2 (bit0 světlé schéma, bit1 english; `RTC_SYSCFG2_MAGIC`). Save `rtc_save_syscfg_if_dirty` (DR2+DR6). Načtení všech v `MX_RTC_Init` (před schedulerem), zápis výhradně defaultTask (kromě crash hooků).
- **Vlákno:** VEŠKERÝ přístup k RTC registrům je **výhradně z defaultTask** (`rtc_app_tick`). UART/UI čtou jen sdílené `g_rtc_text` ("YYYY-MM-DD HH:MM:SS") / `g_rtc_synced` (1=sync z GPS) — žádná cross-task HAL_RTC kolize. `g_rtc_text`/`g_rtc_synced` definované ve `freertos.c`, extern ve `freertos_shared.h`.
- **Zobrazení:** UART příkaz **`rtc`** (čas + sync stav). **Hlavní obrazovka** (header, `screen_main_redraw_time` + `render_header` date) i **GPS okno** karta „Čas / datum" čtou RTC přes helper `rtc_time_date()` (parse `g_rtc_text`) — **hodiny tikají plynule 1×/s i při ztrátě fixu** (dřív GPS-direct → mezi RMC stály a bez fixu zamrzly). Před prvním GPS syncem `--:--:--` / `no GPS`; v GPS okně nesynchronizovaný čas **ztlumený** (`UI_COLOR_INK_3`). GNSS/SAT pilulky zůstávají z GPS (odráží fix). **Diagnostika** (karta „System / RTOS / RTC") ukazuje RTC čas `HH:MM:SS` (ztlumený `no GPS` dokud nesrovnán).
- ⚠️ **defaultTask stack 256→384 words** kvůli `snprintf` v `rtc_app_tick` (historie: formátování v malém tasku už jednou přeteklo stack).
- ⚠️ **Při čtení RTC vždy `HAL_RTC_GetTime` PŘED `HAL_RTC_GetDate`** (čtení TR odemkne shadow registry, jinak se DR zasekne). **NEpovoluj RTC NVIC** (Alarm/WakeUp) v IOC — jen kalendář.

## Beeper (beeper.c/h) — PH9, 800 Hz + alarm vrstva (alarm.c/h)
Pasivní beeper na **PH9** (pin95). Tón **800 Hz** generuje **TIM7** přerušením @1600 Hz
(`HAL_TIM_PeriodElapsedCallback` v main.c → `beeper_isr_toggle()` přepíná PH9). `beeper_init`
(GPIO+TIM7+NVIC) voláno v main.c USER CODE 2. TIM7_IRQHandler v stm32h7xx_it.c USER CODE.
**Nepoužívej TIM7 jinde / nepovoluj v IOC.**
- **`beeper_tone(freq_hz)`** = libovolný tón (přepíše TIM7 ARR = 1 MHz/(2·freq)); `beeper_set(true)` resetuje ARR na 800 Hz (alarm). **`beeper_boot_melody()`** = vzestupný C-dur arpeggio G5→C6→E6→G6 („power-on" jingle ~0,5 s, blokující `osDelay`); volá UiTask jednou při startu, **jen když není mute** (respektuje `g_sound_muted` z BKP). Watchdog grace (8 s) blokující melodii kryje.
- **`alarm.c/h` = jediný volající `beeper_set()`.** `alarm_tick()` (defaultTask ~100 Hz) hlídá
  **hrany** dvou stavů: FPGA `g_freq_stale` (SIGNAL_LOST/mrtvý link) a GPS lock (`gps_get`:
  valid ∨ fix_mode≥2). Ztráta signálu = 3 pípnutí, ztráta GPS locku = 2, obnovení = 1.
  **Start tichý** (guardy `s_*_ever` — první link-up/první fix nepípne, bench bez antény taky ne).
  Pattern neblokující (HAL_GetTick fáze), vyhodnocení stavů jen 5×/s (gps_get kopíruje ~200 B
  v kritické sekci), časování pípnutí 100×/s. **Mute** = `g_sound_muted` (okno Nastavení /
  UART `beep off`) umlčí okamžitě i test; prev-stavy se při mute dál aktualizují (po odmutení
  žádné pípnutí na starou hranu). UART: `beep`/`beep test`, `beep on`, `beep off`.

## IWDG watchdog (watchdog.c/h) — ~4 s, heartbeat UiTask+FpgaTask
**IWDG1** (LSI ~32 kHz, /64, reload 2000 → ~4 s), **registrová implementace** (KR/PR/RLR) —
`HAL_IWDG_MODULE_ENABLED` je v hal_conf VYPNUTÝ, modul je nezávislý a regen-safe.
- `watchdog_init()` v main.c USER CODE 2 (těsně před schedulerem). ⚠️ **Sekvence dle RM0399:
  nejdřív START (0xCCCC)** — ten HW zapne LSI; teprve pak UNLOCK+PR+RLR+wait SR+RELOAD
  (bez běžícího LSI se PR/RLR update nikdy nepotvrdí — stejné pořadí jako HAL_IWDG_Init).
- **Heartbeat model:** `watchdog_kick_ui()`/`watchdog_kick_fpga()` na začátku smyček UiTask/
  FpgaTask; `watchdog_supervise()` (defaultTask ~100 Hz) obnoví IWDG **jen když oba heartbeaty
  < 2,5 s staré** → zatuhnutí jednoho tasku (ne jen celého scheduleru) = HW reset. Startup
  grace 8 s. **UartTask se nemonitoruje** (legitimně blokuje: `scanner` ~2,5 s, `fpgaloop` ~3 s).
- V DEBUG buildu `__HAL_DBGMCU_FREEZE_IWDG1()` (breakpoint neresetuje). Release bez freeze.

## W25Q512JV — externí QSPI flash 64 MB (w25q.c/h, QUADSPI)
Winbond **W25Q512JVFIQ** (512 Mbit = **64 MB**) na **QUADSPI Bank1**. Osazená na STM desce
(schéma `STM32H747BIT.pdf`, sheet `USB_SD_FLASH`). **Bring-up HOTOVÝ** — `qspiid`→`EF4020`,
`qspitest` erase/write/read/verify OK.
- **Piny (v .ioc, CubeMX-managed → regen-safe):** CLK=**PF10**(AF9), NCS=**PG6**(AF10),
  IO0=**PD11**(AF9), IO1=**PD12**(AF9), IO2=**PF7**(AF9), IO3=**PD13**(AF9), /RESET=**PH1**(GPIO out high).
  ⚠️ `SPI2_RCK_Pin`(PB12/FPGA CS) i `QSPI_BK1_IO1_Pin`(PD12) mají stejnou masku `GPIO_PIN_12`, ale
  různé porty (B vs D) → není konflikt.
- **CubeMX QUADSPI:** `FlashSize=25` (2²⁶ = 64 MB — KRITICKÉ), `SampleShifting=HALFCYCLE`, `ClockMode=0`,
  single flash. `MX_QUADSPI_Init` generovaný v `quadspi.c` (`hqspi`). CubeMX prescaler=23 (10 MHz) je jen
  default — **driver `w25q_init` ho přebíjí na `W25Q_SCK_PRESCALER=3` → SCLK 60 MHz** (regen-safe, jako
  FPGA SPI baud). **Read = Quad Fast Read 0x6C** (4-line, 8 dummy) — ⚠️ nad 50 MHz nutné dummy (plain
  Read 0x13 je stropován 50 MHz). Ověřeno `qspispeed` verify=OK @ 60 MHz.
  - **⚠️ Propustnost pollovaného čtení ~4,6 MB/s** (strop `HAL_QSPI_Receive` = CPU čte FIFO bajt po bajtu,
    ~215 ns/B), NE limit SCK. Raw 60 MHz quad = ~30 MB/s → odemkne až **DMA (`HAL_QSPI_Receive_DMA`/MDMA)
    nebo memory-mapped mód**. Pro malá data (config/kalib) je 4,6 MB/s hluboko nad potřebou; DMA přidat
    až u bulk read (fonty XIP / čtení logů). Viz [[revize-2026-07-03]] TODO.
- **⚠️ 4-byte adresování:** 64 MB > 16 MB → 3bajtová adresa nestačí. Driver dělá `EN4B` (0xB7) v initu
  + nativní 4-byte příkazy (READ `0x13` / PP `0x12` / SE `0x21`, `QSPI_ADDRESS_32_BITS`).
- **Driver `w25q.c`:** `w25q_read_jedec` (bez init), `w25q_init` (SW reset 66h/99h → JEDEC check →
  EN4B → quad-enable SR2), `w25q_read` / `w25q_write` (handluje 256B stránky; ⚠️ cíl musí být předem
  smazán) / `w25q_erase_sector` (4 KB), WIP polling (`wait_ready`). **Read = Quad Fast Read 0x6C** (4-line,
  `s_quad` z quad-enable; fallback Fast Read 0x0C 1-line); zápis/erase/registry 1-line. `w25q_format_status` pro diag.
- **UART:** `qspiid` (JEDEC ID, čeká EF4020), `qspitest` (init + destruktivní self-test sektoru 0),
  `qspispeed` (64 KB timed read + verify → KB/s), `storetest` (blob store self-test na CALIB regionu).

### Region mapa + storage vrstva (w25q_map.h, w25q_store.c/h) — HOTOVO/ověřeno
Deska je **generická** (použitelná i jinam) → regiony obecné, ne GPSDO-specifické. Zarovnané na 64 KB:
| Offset | Velikost | Region | Účel |
|---|---|---|---|
| `0x000000` | 64 KB | **CONFIG** | runtime nastavení (časté změny), wear-leveled store |
| `0x010000` | 64 KB | **CALIB** | kalibrace + zařízení param (zřídka), wear-leveled store |
| `0x020000` | ~63,9 MB | **DATA** | generický bulk / logy (app-defined, zatím nevyužito) |

- **`w25q_store`** = generický **kruhový wear-leveled blob store** nad regionem: 1 blob (max **4080 B** = 1 sektor)
  na region, každý zápis jde do dalšího sektoru (round-robin → N× endurance), nejnovější platný `seq` vyhrává.
  **Power-safe:** payload se zapíše první, hlavička (magic+seq+CRC16) NAPOSLED → výpadek uprostřed zápisu =
  magic chybí = záznam neplatný, starý zůstává. API `w25q_store_init/read/write`. Nezná app obsah (jen bajty).
  Ověřeno `storetest` (write/read/CRC + rotace sektorů OK).
- **⚠️ Cap 4080 B/blob** (1 sektor). Stačí na kalib params; velký LUT → multi-sektor rozšíření nebo DATA region (viz [[revize-2026-07-03]]).
- **Config split (revidovatelné, viz [[revize-2026-07-03]]):** **malé UI nastavení zůstává v RTC BKP** (instantní,
  bez wear); **QSPI flash na store/log/kalibraci** (velká/trvalá data). BKP a flash config se NEmíchají.
- **Plánované využití:** reálná kalibrace → CALIB store, **datalogging stability** (Allan/drift/holdover — killer
  app GPSDO) → DATA region, externí flash v paměťové diagnostice, volitelně memory-mapped XIP (@0x90000000) + quad read.

## I2C1 — senzory na FPGA desce (i2c.c MX_I2C1_Init, ads1115.c/h)
Druhá I2C sběrnice **I2C1**: SCL=**PB8**, SDA=**PB9** (AF4, ~100 kHz, Timing 0x70303AEE jako I2C4).
`MX_I2C1_Init` je **self-contained v i2c.c USER CODE 1** (GPIO+clock tam, regen-safe) — voláno v main.c USER CODE 2 před schedulerem. Mutex `i2c1MutexHandle`.
- **TMP117** @ 0x49, 0x4A (čteno v SensorsTask 2×/s). **⚠️ 0x4A NENÍ osazený** → vrací NACK (rychlá chyba `sensor_fail`, červený `!` na diagu) — to je očekávané, NEodstraňovat (ať se připojí, až bude). 0x49 osazený.
- **ADS1115** @ 0x48 (4 single-ended kanály AIN0–3, PGA ±4.096 V, 128 SPS, single-shot). Driver `ads1115_start`/`ads1115_read_raw`/`ads1115_raw_to_mv`. **AIN0/1 přímo; AIN2 = 12V větev přes odporový dělič (×13417/2814, kalibrace 13.417 V @ 2.814 V), AIN3 = 5V větev (×4978/2526, 4.978 V @ 2.526 V)** — přepočet ve StartI2C4 ukládá skutečné napětí větve.
- **⚠️ Stav senzorů = `g_sensors[SENS_COUNT]` (`sensor_stat.h`), NE staré skalární `g_temp*`/`g_ads_mv`.** Jednotná struktura pro 3× TMP117 + 4× ADS + **3× ADC3 (MCU jádro teplota, VDDA, VBAT)** = 10 senzorů: `last` (poslední hodnota, °C nebo mV), `min`/`max`/`mean` (statistika z platných vzorků, running mean), `samples`, `valid` (1=poslední čtení OK), `err_total`/`err_streak` (čítače chyb). Index = `sensor_id_t` (`SENS_T48/T49/T4A/ADS0..3/CORE_T/VDDA/VBAT`). **Zápis VÝHRADNĚ SensorsTask** přes `sensor_update(id,val)` (platný vzorek) / `sensor_fail(id)` (chyba I2C → `valid=0`, `last` drží poslední dobrou → matematika/log ignorují podle `valid`). **Bez zámku** (mění se ~2×/s, roztržené čtení tolerováno; přesná matematika patří dovnitř SensorsTask). `sensor_stat.h` je čistý C header (smí ho includovat i app/ vrstva). SensorsTask čte senzory **2×/s** (`vTaskDelayUntil` 500 ms); touch NENÍ v tomto tasku (viz UI vrstva).
- **⚠️ ADC3 = interní MCU senzory (teplota jádra / VREF / VBAT), `SENS_CORE_T/VDDA/VBAT`.** Čte SensorsTask (`adc3_read_chan`), zobrazení v diag „MCU" kartě + okně SENZORY + UART `sensors`/`adcraw`. **Rozchození bylo HW-tricky — 5 vrstev problémů (interní kanály railovaly na `0xFFFF`):**
  0. **🔑🔑 VREF+ není spojen s VDDA (HW desky) → reference budí vnitřní `VREFBUF`.** Pin 43 má jen `C15` 100n + dodaný **1 µF** (nutný pro stabilitu VREFBUF), s VDDA NEspojen (záměr — kvůli SMPS šumu). Bez reference VREF+ visel (~0,5 V) → VREFINT (1,22 V) i teplota saturovaly na `0xFFFF`. **Zapnutí VREFBUF v `main.c` USER CODE 2** (`HAL_SYSCFG_VREFBUF_VoltageScalingConfig(SCALE0 ≈ 2,5 V)` + `HighImpedanceConfig(DISABLE)` + `HAL_SYSCFG_EnableVREFBUF`). **⚠️🔑 KRITICKÉ: VREFBUF má VLASTNÍ clock `RCC_APB4ENR.VREFEN` (NE přes SYSCFG!) — bez `__HAL_RCC_VREF_CLK_ENABLE()` je celý `VREFBUF->CSR` MRTVÝ** (zápisy ignorovány, čtení vrací 0, ENVR „nedrží"). Tohle byla nejskrytější příčina — VREFBUF vypadal jako HW vada reference, ale chyběl jen ten clock enable. Ověření: `adcraw` → `VREFBUF CSR=0x09` (ENVR+VRR), `CCR≠0` (trim), `vref≈32000`. **Reference je ~2,5 V, NE VDDA 3,3 V** → label senzoru „VREF".
  1. **`PCSEL=0`** — na H7 musí mít každý kanál bit v `ADC3->PCSEL`, jinak je analogový vstup **odpojený** → railuje. `HAL_ADC_ConfigChannel` ho na této verzi NEnastavil → zapínáme ručně (`ADC3->PCSEL |= (1<<kanál)`, kanál z `SQR1` rank1; ADEN musí být 0).
  2. **`ClockPrescaler` CubeMX vynechal** z `MX_ADC3_Init` (i když `.ioc` má DIV8) → ADC běžel na 25 MHz + špatný BOOST. SensorsTask init nastaví `ADC_CLOCK_ASYNC_DIV8` (→3,125 MHz) + `HAL_ADC_Init`.
  3. **Kalibrace jsou 16-bit** (ne 12-bit jak tvrdí HAL komentář; VREFINT_CAL=24291). LL makra `__LL_ADC_CALC_*` u VREFINT dělí 12-bit → špatně. Počítáme ručně 16-bit: **`vref(+) = VREFINT_CAL×3300/vref_data`** (reference-agnostické → vrací skutečné VREF+, tj. ~2500 z VREFBUF), `Temp = (ts×vref/3300 − TS_CAL1)×80/(TS_CAL2−TS_CAL1)+30` (**korekce `×vref/3300`** protože TS_CAL je @ 3,3 V ale VREF+ je 2,5 V), `VBAT = vbat×vref/65535×4` (vnitřní dělič /4). **„VDDA" senzor teď měří VREF+ (~2,5 V), label = „VREF".**
  4. **Single-channel režim** (ScanConvMode=DISABLE, NbrOf=1, čteno po jednom) — scan polling 3 kanálů byl nespolehlivý. VBAT = pin 8 = záložní CR2032 (BT1) → VBAT senzor monitoruje tu baterii.
  Vše regen-safe v `freertos_task_sensors.c` + `main.c` USER CODE 2 (ne v generovaném `adc.c`). Diagnostika: UART `adcraw` (raw + spočítané). Detail viz `CUBEMX_CHECKLIST.md`.
- **Ošetření chyb senzorů:** při selhání I2C čtení (HAL chyba / mutex nezískán) → `sensor_fail`: `valid=0`, hodnota se NEpřepisuje (žádný sentinel, neotráví průměry). **Log** přes UART jen na PŘECHODU stavu (první chyba po OK / obnovení) — žádný 1 Hz spam. **Displej** (diagnostika): neplatná hodnota se kreslí ztlumeně (`UI_COLOR_INK_3`) + malý červený `!` vlevo. **I2C4 auto-reinit NE** (reset I2C4 umí zaseknout ATTINY → tmavý panel) — jen se opakuje čtení. **I2C1 MÁ recovery** (`i2c1_recover` v SensorsTask): při „wedge" chybě (BERR/ARLO/TIMEOUT — slave drží SDA) udělá 9 SCL pulzů (PB8) + **re-init BEZ `MX_I2C1_Init`** → jeden vadný/zaseknutý čip neshodí zbytek (ADS). Spouští se max 2×/cyklus, JEN na wedge, NE na NACK (absentní 0x4A re-init nezpůsobí). I2C1 nemá ATTINY → reset bezpečný. **⚠️ Recovery NESMÍ volat `MX_I2C1_Init`** — ta má `Error_Handler()` (nekonečná smyčka) při selhání `HAL_I2C_Init`; při ODPOJENÉM/plovoucím busu (pull-upy jsou na FPGA desce!) re-init selže → **zamrzl by CELÝ program** (zjištěno 2026-06-25). Recovery proto dělá GPIO AF + `HAL_I2C_Init` inline a selhání ignoruje (zkusí příští cyklus). **Recovery běží pod `i2c1MutexHandle`** (nekoliduje s UART `si5356`/`scan1`). **Back-off (`i2c1_backoff_ms`):** při trvalém selhání celé I2C1 (mrtvý bus) se polling zpomaluje **3×@500 ms → 3×@1 s → 2×@2 s → @10 s** (šetří CPU); jakékoli HAL_OK → reset na normál (NACK absentního 0x4A se nepočítá → při připojeném kabelu back-off nevznikne). TMP117 0x48 (I2C4) čte dál 2×/s, gate je jen na I2C1.
- **Si5356A** @ 0x70 (clock generator) → `si5356.c/h`, `si5356_init(&hi2c1)` v main.c USER CODE 2 (po `MX_I2C1_Init`, před schedulerem → bez mutexu). Aplikuje **ClockBuilder Pro register map** (`REGMAP[]`, oficiální CBPro „C Code Header" export) přes paging (reg 255 page0/page1) + SiLabs apply proceduru (OEB_ALL off → E2 pulse → SOFT_RESET 0xF6 → OEB_ALL on). Status reg 218 (0xDA): bit0 SYS_CAL, **bit2 LOS_CLKIN** (chybí vstupní ref. hodiny!), bit4 PLL_LOL. UART `si5356` = re-init + status.
- **⚠️ KONFIGURACE: 4× 100 MHz, fáze 0/90/180/270° (= 0/2,5/5/7,5 ns), Vstup 10 MHz → VCO 2,2 GHz (N=220) → /22.** Ty 4 fázově posunuté hodiny jsou **reference pro 4-fázový vernier TDC ve FPGA** (reciproký čítač, jemný krok 2,5 ns). **MUSÍ být 90°, NE 45°** — při 45° jemné kódy nesednou na 2,5 ns mřížku TDC → systematická chyba kmitočtu. Fáze v reg (LSB=Tvco/128=3,551 ps): CLK1 r111/112=704=2,5 ns, CLK2 r115/116=1408=5,0 ns, CLK3 r119/120=2112=7,5 ns. **Přesnost čítače = přesnost těch 100 MHz (= ppm 10 MHz vstupu Si5356).**
- **Při změně konfigurace: v CBPro nastav fáze (90° krok!), exportuj „C Code Header" a nahraď `REGMAP[]`** (formát {addr,val,mask} — adresy DECIMÁLNĚ pro 1:1 diff s exportem). **Vynech řádky s `mask==0x00`** (CBPro „do not write"); `mask<0xFF` → read-modify-write, `mask 0xFF` → přímý zápis (`wr_masked` to respektuje, `mask 0` přeskočí).
- **UART `scan1`** = I2C scan na I2C1 (s popisky zařízení). `scanner` zůstává pro I2C4.
- **Diagnostická obrazovka** (`app_gpsdo_render_diag`, tlačítko MENU → ZPĚT): **dvousloupcový layout** (`DG_*` makra v `app_gpsdo.c`). **Levý sloupec:** Teploty — **labely dle umístění, ne adres** (pořadí: „STM board" = TMP117 0x48, „MCU jadro" = ADC3 CORE_T, „OCXO" = 0x49, „FPGA board" = 0x4A), všechny `last` + `min/max`; Napětí — „OCXO_VC" (AIN0, ladicí napětí), „RF_Level" (AIN1), AIN2 (12V), AIN3 (5V), VREF + VBAT. **Pravý sloupec:** *Komunikace + měření FPGA* (`g_spi_text` zeleně/červeně + `g_freq_info` = gate/PH/SEQ), *Reference Si5356* (lock z `g_si5356_status`: LOCK OK / LOS CLKIN! / PLL UNLOCK! / CALIB…), *System / RTOS / RTC* (heap free/min, CPU %, uptime, **RTC čas HH:MM:SS** z `g_rtc_text` — ztlumený + `no GPS` dokud nesrovnán z GPS). Neplatný senzor = ztlumeně + červený `!`. Refresh ~2×/s z `app_gpsdo_tick` (UiTask), tearing-free přes `prim_stm32_present`.
  - **Datové zdroje:** Si5356 status čte SensorsTask (`si5356_read_status`, reg 218) do `g_si5356_status`/`g_si5356_ok`; RTOS zdraví počítá UiTask (`xPortGetFreeHeapSize`, idle-delta CPU %) do `g_rtos_*`/`g_uptime_s`.
- **Okna** (`s_view`: 0=main, 1=diag, 2=gps, 3=health, 4=senzory, 5=pamět, 6=histogram, 7=nastavení, 8=screensaver, 9=trend-fullscreen, 10=o-přístroji, 11=boot-splash, 12=menu-rozcestník, 13=confirm-restart, 14=reference, 15=kalibrace, **16=holdover, 17=datalog, 18=alarmy**).
- **⚠️ Navigace = zásobník** (`s_nav_stack`/`nav_push`/`nav_back` v app_gpsdo.c). Každý forward přechod pushne aktuální s_view; **BACK (`nav_back`) se vrací k tomu, ODKUD bylo okno otevřeno** — takže okno otevřené z Menu → BACK → zpět do Menu; podpora vnoření (Menu→Nastavení→O přístroji→BACK→Nastavení→BACK→Menu). `app_gpsdo_render_main` resetuje zásobník (kořen). `goto_view` renderuje cíl (jen 0/3/7/12 mohou být návratové = spawnují podokna).
- **MENU** (footer tlačítko na hlavní obrazovce, `b==4`) → **Menu rozcestník** (`app_gpsdo_render_menu`, s_view=12): **mřížka 3×4 = 12 dlaždic** (`MENU_ITEMS[MENU_N]` + `menu_activate()`, `ACT_*` enum — tabulka místo if-řetězce), jen SYSTEM/NÁSTROJE (kontextová okna GPS/Histogram/Trend NEJSOU v menu — dostupná přímo z hlavní obrazovky přes pilulku/tap): Diagnostika, Nastavení, System Health, Senzory, Paměť, **Reference** (Si5356 stav + config 4×100 MHz vernier), **Holdover** (s_view=16, živý stav disciplinace WARMUP/LOCK/HOLDOVER/NO-LOCK z GPS fixu + FPGA linku + timepulse + OCXO teploty 0x49), **Datalog** (s_view=17, vstupní bod pro logování do W25Q DATA regionu — zatím NEAKTIVNÍ, ukazuje base/kapacitu/JEDEC + plán ~32 B/10 s → ~600 dní), **Alarmy** (s_view=18, přehled hlídaných podmínek + počitadla `g_alarm_fpga_lost`/`g_alarm_gps_lost` + živý mute stav — doplňuje Nastavení, které má jen globální mute), **Kalibrace** (konstanty read-only, editace→CALIB store TODO), O přístroji, **Restart**. Restart → **potvrzovací okno** „Opravdu restartovat?" ANO/NE (s_view=13); ANO → `g_reboot_req=1` → defaultTask `NVIC_SystemReset`. Reference/Kalibrace/Holdover/Datalog/Alarmy = `app_gpsdo_render_reference/kalib/holdover/datalog/alarms` (s_view=14/15/16/17/18); Holdover/Reference/Alarmy jsou živé (v `app_gpsdo_tick`), Datalog/Kalibrace statické.
- **SYS pilulka barevně** (`compute_sys_level` v screen_main.c): agregace VŠECH chyb → zelená „SYS OK" / amber „SYS !" / červená „SYS ERR". **AMBER** (degradace, funguje) = FPGA SIGNAL_LOST/no-link, Si5356 necteno/kalibrace, sensor err_streak, watchdog/crash reset (zotaveno). **RED** (kritické) = Si5356 LOS_CLKIN/PLL unlock (ztráta 10 MHz reference!), selftest FAIL. `screen_main_sys_poll` (v tick_clock) překreslí header při změně úrovně.
- **Trend fullscreen — relativní ± časové okno** (tlačítka `−`/`+` v dolní liště krokují presety 10/20/30/60/120 s, hodnota mezi nimi): `screen_main_trend_set_secs` omezí graf na posledních N vzorků (ring drží max STAT_N=120 s).
- **⚠️ Radiální gradient pozadí — rychlý isqrt** (`gradient.c`): per-pixel vzdálenost byla dříve lineární hledání `while ((d+1)²≤d2) d++` = až ~540 iterací/pixel × 384k px → **stovky ms** (citelné hlavně při přepnutí schématu = rebuild `bg_cache`). Nahrazeno Newtonovým `isqrt32` (~O(log), pixel-identické) → ~40× rychleji. Rect pilulek se zachytává v `render_header` (`s_gnss_pill_rect`/`s_sys_pill_rect`), `screen_main_hit_gnss/sys` testuje zásah; `screen_main_hit_allan/hit_trend` = tap na Allan/trend kartu → fullscreen okno („↗" náznak v hlavičce karet):
  - **GNSS pill → GPS/GNSS okno** (`app_gpsdo_render_gps`, s_view=2): **ŽIVÉ** (refresh ~2×/s v `app_gpsdo_tick`, first/values split jako diag). **NEsymetrické sloupce** (`GPS_LX/LW/RX/RW` makra): levý široký (502 px) = FIX + Družice, pravý úzký (250 px, ~⅔) = Čas/Poloha/Lokator/Přijímač. **Řádek FIX**: „FIX 3D" (mono_25) vlevo + **TP 100 kHz/10 Hz** vpravo (timepulse přesunut z vlastní karty; s fixem 100 kHz = GPSDO PLL ref disciplinovaná na GNSS, bez fixu 10 Hz = holdover indikátor). **Karta Lokator** (bývalá TIMEPULSE): „Locator JN89NS85KN" = 10-znakový Maidenhead grid (`fmt_locator` z lat/lon, mono_18 accent). Čas+datum z **RTC** (UTC), poloha lat/lon/alt (mono_16), HDOP/PDOP, **Přijímač** = „NEO-7M" v headeru + živé `Vet:`/`Fix:` (z `g.sentences`/`g.fixes`). **Karta Družice = JEDNO zobrazení na plnou šířku, přepínatelné DOTYKEM** (`s_gps_polar`, tap na kartu `GPS_SAT_RECT`): **bargraf C/N0** (default, až 14 nejsilnějších, C/N0 nad + PRN pod sloupcem) ↔ **polární sky plot** (kruh r=86, 3 elevační kružnice + N/S/E/W kříž, tečka=družice: azimut 0=sever po směru hodin, poloměr ∝ 90−elevace = zenit ve středu, PRN vedle tečky). Barvy zelená/žlutá/červená dle C/N0. Data z GSV: `parse_gsv` plní `g.sats[]` PRN+elev+**azim**+C/N0; `gps_sat_t`/`GPS_MAX_SATS` v `gps.h`. **Změnový klíč = mód + hash az/el/snr VŠECH družic** (tečka/bar se pohne i u slabé). GLONASS zatím vypnuté (jen GPGSV, viz [[gps-todo]]).
  - **SYS pill → System Health okno** (`app_gpsdo_render_health`, s_view=3): **živé** (refresh 2×/s v `app_gpsdo_tick`, stejný first/values split jako diag). RTOS (heap/CPU), **volný stack tasků** (`osThreadGetStackSpace`, byty; <64 B → červený `!`), I2C chybovost (agregace `g_sensors[].err_total/streak`, **0x4A vyřazen** = neosazen), linky (FPGA/Si5356/senzory n/10), karta **System**: „Power supplies: OK/Unkn/FAIL" (verdikt z 12V/5V ±10 %, konkrétní napětí jen v Diag/SENZORY), Uptime, „Reset: <příčina>" (červeně při IWDG/crash), „Selftest: PASS/FAIL". **SENZORY > podmenu** (`app_gpsdo_render_sensors`, s_view=4) = přehled **aktuálních hodnot** všech 10 senzorů, dvousloupcově (vlevo Teploty, vpravo Napětí), `dlabel`+`dval` jako diag. Min/max/avg/err jen přes UART `sensors`. `osThreadGetStackSpace` jen při otevřeném okně (scan stacku nezatěžuje běžný provoz). **PAMET podmenu** (`app_gpsdo_render_mem`, s_view=5) = využití interní FLASH/RAM (linker symboly), RTOS heap (used/total), SDRAM 32 MB, W25Q 64 MB (JEDEC). **NASTAVENI podokno** (`app_gpsdo_render_settings`, s_view=7, tlačítko v Health footeru, **dvousloupcové**): vlevo mute zvuku (ikona `ui_icon_speaker/_muted`), jas −/+ (bargraf + %, clamp 25..255), auto-dim zap/vyp + prodleva −/+ (presety 15..600 s); vpravo **Vzhled** (TMAVÉ/SVĚTLÉ — runtime přepnutí palety) a **Jazyk** (ČESKY/ENGLISH — zatím jen infrastruktura `g_lang_en`, texty se překlápí postupně). Persist DR2+DR6 přes `g_sys_cfg_dirty`. Statické okno (není v ticku, překreslí se při tapu).
  - **Barevná schémata (libui/src/theme.c):** `UI_COLOR_*` makra derefují ukazatel `g_ui_theme` (runtime tabulka TMAVÁ/SVĚTLÁ) → přepnutí `ui_theme_select()` NEVYŽADUJE změnu volajících. ⚠️ `UI_COLOR_*` proto NELZE použít ve file-scope `static const` inicializátorech. Po přepnutí NUTNÉ `screen_main_invalidate()` + `screen_main_init()` (bg_cache je předrenderovaná ve starých barvách) — dělá to THEME handler v `app_gpsdo_handle_touch`. Uložené schéma aplikuje `app_gpsdo_init` před prvním renderem.
  - **Histogram okno** (`app_gpsdo_render_histogram`, s_view=6, **tap na Allan kartu** na hlavní obrazovce — ztlumené „↗" za titulkem karty značí klikatelnost, `screen_main_hit_allan`): vlevo **histogram rozdělení y=(f−f₀)/f₀** (24 binů, auto-range, mean=zelená + medián=amber čára, **Gaussova referenční křivka** z (mean,σ), overlay N/x̄/s/med), vpravo **σy(τ) Allan tabulka** (τ=1/10/100/1k/10k s z ADEV pyramidy, `--` bez dat). Tlačítko **Y: LIN/LOG** (levý footer slot) přepíná osu (`screen_main_hist_logy/toggle`). **Change-key skip:** tick 2×/s překreslí JEN při změně `screen_main_stats_version()` (čítač vzorků, ~1×/s při RUN) nebo lin/log osy — jinak žádný sort/Gauss/ADEV/flip naprázdno. **Vzorkování statistiky běží nezávisle na zobrazeném okně** (`app_gpsdo_tick_stats_sample` gatuje jen RUN/STOP; mimo main krokuje simulaci `screen_main_freq_sim_step` bez kreslení) → Allan/histogram rostou 24/7 i při screensaveru — dřívější vazba na main obrazovku zastavovala pyramidu na krátkých τ (bug „Allan nejde přes ~250 s"). Plot i tabulka si čistí svůj rect (`PRIM_BLEND_REPLACE`) → text MUSÍ mít baseline uvnitř rectu (ascent!), jinak AA hrany mimo clear oblast při refreshi tuhnou.
- **Auto-dim** (UiTask): po `g_autodim_sec` bez doteku ztlumí backlight na 20/255 (`AUTODIM_LEVEL`, nikdy tma); **první dotek jen probudí** (nespustí akci tlačítka). Aplikace jasu = výhradně UiTask (`ws_panel_set_backlight` @ I2C4 pod mutexem, jen při změně cíle); app vrstva mění jen `g_brightness`.
- **Header hlavní obrazovky** (`screen_main_redraw_time`): čas HH:MM:SS + „UTC" popisek (RTC běží v UTC) + datum; vlevo **stavové mikro-ikony** — přeškrtnutý reproduktor (`ui_icon_speaker_muted`) při `g_sound_muted`, amber „H" při holdoveru (`!g.valid && g.fixes>0` = fix ztracen po tom, co už jednou byl). Změnový klíč zahrnuje i stav ikon → překreslí se i mimo tik sekundy.
- **Trend fullscreen** (s_view=9, tap trend karty): celá historie ringu (až `STAT_N`=120 s vs. 60 s na kartě), auto-scale + min/max frac popisky, drift overlay; change-key jako histogram. **O přístroji** (s_view=10, tlačítko v Nastavení): FW `gpsdo-ui v0.1` + `__DATE__ __TIME__` build, autoři OK2JNJ & OK2HAZ, MCU, uptime, selftest verdikt, sériové č. (zatím nepřiděleno → CALIB store). **Boot splash** (s_view=11): logo „GPSDO" (mono_75) + build + živý řádek Selftest (PASS/FAIL z `g_selftest_res`); UiTask ho drží ~1,4 s před hlavní obrazovkou.
- **NEPOVOLOVAT I2C1 v IOC** (init je ručně v USER CODE).

## UART TX
`_write` (main.c) chráněn `uartTxMutexHandle` (serializace printf z více tasků, jen za běhu
scheduleru) + timeout 100 ms (~1150 B/řádek, bez utínání). Pořád blokující HAL_UART_Transmit.

## UART RX (usart.c)
RX přes IT + fronta `UartRxQueue`. `RxCpltCallback` zařadí bajt a znovu nahodí `Receive_IT`.
**⚠️ `HAL_UART_ErrorCallback` (ORE/FE/NE/PE — typicky šum při hot-plugu kabelu) MUSÍ před
re-armem zavolat `HAL_UART_AbortReceive` + vynulovat `ErrorCode`.** Bez toho po chybě `RxState`
zůstane `BUSY` → `HAL_UART_Receive_IT` vrátí `HAL_BUSY` → **RX se už nikdy nenahodí (mrtvá
konzole, TX/výpisy jedou dál).** AbortReceive v IT režimu neblokuje (ISR-safe).

## Build / flash
STM32CubeIDE: vyber projekt **H757_LED_CM7** → Build (Ctrl+B) → Run (Ctrl+F11, config CM7).
Toolchain (arm-none-eabi) není v PATH tohoto prostředí, ale **je na disku**:
`C:\ST\STM32CubeIDE_2.1.0\...\gnu-tools-for-stm32.14.3...\tools\bin\arm-none-eabi-gcc.exe`
(GCC 14.3) — použitelný pro **kompilátorový audit** (`-Wall -Wextra -Wshadow` +
`-fanalyzer`) jednotlivých souborů bez IDE. Flash/link jen z IDE.
**Testy:** UART `selftest` = neblokující pure-logic unit testy na targetu (idiom
projektu — testy běží na zařízení, ne na hostu; `run_selftests` ve freertos.c, i při bootu):
CRC16, hystereze /4↔/16 (`fpga_freq_select_core`), GPS parser (`gps_selftest`), fmt_frac+hist_h
(`screen_main_selftest`), Maidenhead lokátor (`app_gpsdo_selftest`) → „SELFTEST: 5/5 PASS".
`qspitest`/`storetest` = destruktivní HW testy.
