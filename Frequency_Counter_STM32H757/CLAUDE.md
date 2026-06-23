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
| defaultTask | Normal | 1024 B |
| UartTask | Normal | 2048 B |
| I2C4Task | Low | 1024 B |
| UiTask | BelowNormal | 8192 B |
| FpgaTask | Normal | 2048 B |
| UartRxQueue | — | 64 × 1 B |

PRIO_BITS=4, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5.

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
- **Dotek tlačítek:** **FT5x06 polluje SensorsTask** (@ I2C4, ~10 Hz, pod `i2c4MutexHandle`), dělá hranové spouštění a při DOWN edge zapíše `g_touch_xy` (=(x<<16)|y, už zrcadleno 799-x/479-y) + zvýší `g_touch_seq`. **UiTask jen drainuje** (porovná seq) a volá `app_gpsdo_handle_touch` — render MUSÍ být v UiTasku (libui není thread-safe). Blokující 31B I2C čtení je tím **mimo render task** (dřív bylo v UiTasku → ~4,5 % CPU). **Panel je zrcadlený v X i Y** (H+V flip).
- **FT5x06 MUSÍ číst celý touch frame** (`ft5x06_read_touch`, 31 B z TD_STATUS 0x02). Částečné čtení (jen 1. bod) → controller drží I2C při multitouchi → další transakce zatuhne → zamrznutí při 2. prstu. Parsuje se jen 1. bod, ale čte se celý rámec.
- **I2C4 mutex** (`i2c4MutexHandle`): TMP117 task + touch + backlight sdílí sběrnici. **Nepovolovat I2C4 interrupt v IOC.**

> **Historie:** dřívější ručně psané `gfx.c`/`touch_ui.c` UI i pokus o **LVGL v9** obrazovku (`lv_port_disp.c`, `ui_main_screen.c`, vendored `Middlewares/Third_Party/lvgl`) byly **odstraněny** a nahrazeny libprim/libui/app. K dohledání v git historii.

## UART příkazy (StartUartTask)
`led on/off`, `ram write/read`, `sdram write/read`, `temperature`, `sensors`, `scanner`, `testDSI`,
`testRED`, `test` (RGB565 sanity), `touch`, `touchloop`, `scan1`, `si5356`, `freq`, `fpgaraw`,
`fpgaloop`, `stats`, `status`, `ui`, **`ping`/`screen main`/`clear`/`version`/`help`**.
`temperature` = TMP117 0x48 + příznak `(STALE)` při chybě čtení. `sensors` = dump všech 7 senzorů
(`last/min/max/avg`, stav OK/ERR, `err_total/streak/samples`) — viz `g_sensors[]`/`sensor_stat.h`.
Neznámý příkaz → `ERR unknown command`. `ui` i `screen main` znovu vykreslí hlavní obrazovku
(`g_screen_req=3`), `clear` ji smaže (`g_screen_req=4`). Odpovědi protokolu CRLF (`ping`→`pong`, `version`→`gpsdo-ui v0.1`).

## FPGA čítač kmitočtu (fpga_freq.c/h, FpgaTask, SPI2)
STM32 = SPI master, FPGA = slave. **SPI2**: master, mode 0, MSB, 8-bit, MOSI=PB15, SCK=PI1,
MISO=PI2, **CS=PB12 (manuál GPIO, active-low)**. Bring-up **1 MHz** (`fpga_freq_init` zvolí
prescaler dle `HAL_RCCEx_GetPeriphCLKFreq(SPI123)`). **SCK strop dle kontraktu FPGA (GW1NR-9 oversampling): cíl ≤6 MHz, absolutní max ~10 MHz** — `FPGA_SCK_TARGET_HZ` (hlídá `#error` na `FPGA_SCK_MAX_HZ`).
- **Timing (kontrakt):** CS setup/hold ≥1 µs (dáváme 2), **mezi rámci ≥20 µs** (FPGA potřebuje ~124 cyklů @10 MHz na složení rámce; dáváme 25). Prodlevy přes **DWT cyklový čítač** (`delay_us`, ne NOP-loop — DWT už zapnut pro runtime staty). Po bootu **`osDelay(250)` v StartFpgaTask** než se začne clockovat (FPGA piny 54–57 jsou config piny, musí dokončit load z flash).
- **CRC self-test (akceptační krok 1):** `fpga_freq_crc_selftest()` ověří `crc16("123456789")==0x29B1`; při selhání se `g_init_ok=0` a SPI komunikace se **nezahájí** (poll/restart hned vrací false).
- **Stav SPI/komunikace na displeji:** `fpga_freq_format_status()` skládá řádek `SPI <x.xx>MHZ LINK:OK/-- SEQ:<n> CRC:<n>` (rychlost SCK, živost linky, posl. SEQ, počet CRC chyb). FpgaTask ho po každém pollu uloží do `g_spi_text`/`g_spi_ok` (překreslí jen při změně), UiTask vykreslí stav SPI při překreslení hlavní obrazovky — **zeleně** když link žije, **červeně** když ne.
- **Pevný 64B full-duplex rámec**: MAGIC 0xA5, VERSION, TYPE, FLAGS/STATUS, SEQUENCE(LE32),
  PAYLOAD_LEN, RESERVED, 50B payload, CRC16(LE) na konci. CRC = **CRC-16/CCITT-FALSE** (0x1021/0xFFFF), pokrývá byte 0..61.
- Model: `FpgaTask` polluje **~20 Hz** (`osDelay(50)`), posílá **ACK** (TYPE 0x06, SEQUENCE=poslední) → FPGA full-duplex vrací aktuální **DATA** (TYPE 0x80). Platné = CRC ok + DATA_VALID + DATA_FRESH + nová SEQUENCE. Polling je úmyslně rychlejší než tempo měření (FPGA gate **0,25 s reciproké → ~4 nová měření/s**) kvůli nízké latenci; protokol je pull/ACK, takže rychlejší polling nezpůsobí ztrátu měření (FPGA shodí DATA_FRESH až po ACK té SEQ). Každé čerstvé měření se zobrazí (žádný throttle).
- **Dva předděliče (4-fázové reciproké měření):** **pin28 = /4** (primár, nejlepší rozlišení), **pin27 = /16** (vyšší rozsah / cross-check), měří se současně. `fpga_freq_select()` volí zobrazovaný zdroj: **/4 dokud je bez chyby a < ~380 MHz, jinak /16**. Rozsah: na pinu do ~100 MHz → reálně ~400 MHz (/4) / ~1,6 GHz (/16). Headline ukazuje zvolený zdroj, info řádek `<src> PH:<present>/<fine> GATE:<ns>NS SEQ:<n>[ chyba]`.
- **Detekce ztráty signálu (SIGNAL_LOST):** FPGA má **autoritativní watchdog** — ~2,5 s bez dokončeného měření → `error_flags` bit1 SIGNAL_LOST + DATA_VALID=0. FpgaTask čte `fpga_freq_signal_lost()` (latch posledního DATA rámce, funguje i při VALID=0) a při ztrátě **nebo mrtvém linku** nastaví `g_freq_stale=1` → UiTask ztlumí kmitočet na **šedou** (čte `g_freq_text`/`g_freq_stale` při překreslení). **Dřívější SEQ-staleness heuristika ODSTRANĚNA** (falešně hlásila stale u nízkých kmitočtů, kde se reciproké okno legitimně protáhne) — teď se věří FPGA flagu.
- **Auto-re-START:** když ~3 s nepřijde žádný platný rámec (`fpga_freq_link_ok()`==0, tj. mrtvý link, ne jen „bez nového měření"), FpgaTask znovu pošle START (20 Hz polling → práh `fails>=60`) — pokrývá FPGA který nabootuje/resetuje až po STM32.
- DATA payload (`fpga_meas_t`, parse v `parse_data()`): `frequency_x100000`(abs12, /4), `edge_count`(20), `gate_time_ns`(28), `timestamp`(36), `channel`(44), `measurement_status`(45), `error_flags`(46), **`phase_status`(50)**, **`status2`(51)**, **`freq16_x100000`(52, /16)**. **`freq*_x100000` = reálný kmitočet × 1e5 (děličku /4 i /16 už zahrnuje FPGA → STM NEnásobí 4 ani 16); `gate_ns` ≈ 250e6 a kolísá.**
  - **`error_flags`:** bit0=`FPGA_ERR_MEAS` (/4 Δt==0), bit1=`FPGA_ERR_SIGNAL_LOST`, bit2=`FPGA_ERR_OVERFLOW` (okno >~21,5 s). **`status2`** bit0=`FPGA_ST2_DIV16_ERR` (/16 Δt==0).
  - **`phase_status`:** bity3:0=present[3:0] (živost 4 fází), bity7:4=fine_seen[3:0] (viděné jemné 2,5 ns kódy). **Zdravé = `PH:F/F`** (obě nibble plné). Mezera v fine_seen → chybějící/špatně posunutá fáze (kontrola 90° rozestupu). ⚠️ SEQUENCE roste pomaleji u nízkých f (okno čeká na hrany) — to NENÍ chyba, skutečnou ztrátu hlásí SIGNAL_LOST.
- **POZOR: SPI2 dřív používal `ShiftRegister_SendByte` (74HC595) přes PB12 — odstraněno**, PB12 je teď CS k FPGA. V IOC/main.h je PB12 pořád pod starým názvem **`SPI2_RCK`** (a PB4 = `SPI2_RES`, nepoužitý). CS pin si `fpga_freq_init` konfiguruje **sám** (push-pull, idle high) — regen-safe, nezávisí na gpio.c.
- **⚠️ CS boot level MUSÍ být HIGH:** `gpio.c` ručně upraven — `MX_GPIO_Init` budí PB12 na **`GPIO_PIN_SET`** (bylo RESET). Jinak STM drží CS asertovaný (LOW) od bootu až do `fpga_freq_init` (po scheduleru), tj. **během konfigurace FPGA z flash** → GW1NR-9 nemusí naběhnout, MISO mlčí (`RX0:FF`). **Při regeneraci z IOC nastav PB12 default Output Level = High.**
- **⚠️ SCK idle LOW (AFCNTR):** `fpga_freq_init` nastavuje `hspi2.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE` (CFG2.AFCNTR=1). Bez toho STM mezi přenosy uvolní SCK/MOSI piny → SCK plave, pull-up na FPGA ho táhne HIGH → FPGA vidí při CS↓ falešnou hranu, rozhodí počítání bitů → `RX0:FF`. (Projev na LA: „initial state of CLK does not match settings".) Regen-safe (v driveru, ne v `spi.c`).
- **Bring-up diagnostika:** status řádek při chybějícím linku ukáže `SPI <x.xx>MHZ NOLINK HAL:<OK|ERR> RX0:<hex> CRC:<n>` (HAL=stav přenosu, RX0=první bajt MISO). UART `fpgaraw` vypíše HAL stav + všech 64 přijatých bajtů. `RX0:FF`/samé FF = FPGA nebudí MISO (CS/SCK/MISO zapojení, zem, nebo FPGA neběží).
- Formát na displeji: `123.456.789,01234Hz` (tečky tisíce, čárka des., 5 míst, bez mezery před Hz). Headline nahoře, žlutě. UART příkaz `freq` vypíše poslední hodnotu.

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

## Beeper (beeper.c/h) — PH9, 800 Hz
Pasivní beeper na **PH9** (pin95). Tón **800 Hz** generuje **TIM7** přerušením @1600 Hz
(`HAL_TIM_PeriodElapsedCallback` v main.c → `beeper_isr_toggle()` přepíná PH9). `beeper_init`
(GPIO+TIM7+NVIC) voláno v main.c USER CODE 2. Ovládá **tlačítko BEEP** v UI (toggle, zelené=hraje).
TIM7_IRQHandler v stm32h7xx_it.c USER CODE. **Nepoužívej TIM7 jinde / nepovoluj v IOC.**

## I2C1 — senzory na FPGA desce (i2c.c MX_I2C1_Init, ads1115.c/h)
Druhá I2C sběrnice **I2C1**: SCL=**PB8**, SDA=**PB9** (AF4, ~100 kHz, Timing 0x70303AEE jako I2C4).
`MX_I2C1_Init` je **self-contained v i2c.c USER CODE 1** (GPIO+clock tam, regen-safe) — voláno v main.c USER CODE 2 před schedulerem. Mutex `i2c1MutexHandle`.
- **TMP117** @ 0x49, 0x4A (čteno v StartI2C4 každou 1 s).
- **ADS1115** @ 0x48 (4 single-ended kanály AIN0–3, PGA ±4.096 V, 128 SPS, single-shot). Driver `ads1115_start`/`ads1115_read_raw`/`ads1115_raw_to_mv`. **AIN0/1 přímo; AIN2 = 12V větev přes odporový dělič (×13417/2814, kalibrace 13.417 V @ 2.814 V), AIN3 = 5V větev (×4978/2526, 4.978 V @ 2.526 V)** — přepočet ve StartI2C4 ukládá skutečné napětí větve.
- **⚠️ Stav senzorů = `g_sensors[SENS_COUNT]` (`sensor_stat.h`), NE staré skalární `g_temp*`/`g_ads_mv`.** Jednotná struktura pro 3× TMP117 + 4× ADS: `last` (poslední hodnota, °C nebo mV), `min`/`max`/`mean` (statistika z platných vzorků, running mean), `samples`, `valid` (1=poslední čtení OK), `err_total`/`err_streak` (čítače chyb). Index = `sensor_id_t` (`SENS_T48/T49/T4A/ADS0..3`). **Zápis VÝHRADNĚ SensorsTask** přes `sensor_update(id,val)` (platný vzorek) / `sensor_fail(id)` (chyba I2C → `valid=0`, `last` drží poslední dobrou → matematika/log ignorují podle `valid`). **Bez zámku** (mění se ~2×/s, roztržené čtení tolerováno; přesná matematika patří dovnitř SensorsTask). `sensor_stat.h` je čistý C header (smí ho includovat i app/ vrstva). **⚠️ SensorsTask smyčka běží ~10 Hz** kvůli touch pollu (FT5x06 @ I2C4, viz UI vrstva); senzory jsou uvnitř gated na 2×/s.
- **Ošetření chyb senzorů:** při selhání I2C čtení (HAL chyba / mutex nezískán) → `sensor_fail`: `valid=0`, hodnota se NEpřepisuje (žádný sentinel, neotráví průměry). **Log** přes UART jen na PŘECHODU stavu (první chyba po OK / obnovení) — žádný 1 Hz spam. **Displej** (diagnostika): neplatná hodnota se kreslí ztlumeně (`UI_COLOR_INK_3`) + malý červený `!` vlevo. **NEdělá se auto-reinit I2C** (CLAUDE.md varuje: reset I2C4 umí zaseknout ATTINY → tmavý panel) — jen se opakuje čtení v další smyčce.
- **Si5356A** @ 0x70 (clock generator) → `si5356.c/h`, `si5356_init(&hi2c1)` v main.c USER CODE 2 (po `MX_I2C1_Init`, před schedulerem → bez mutexu). Aplikuje **ClockBuilder Pro register map** (`REGMAP[]`, oficiální CBPro „C Code Header" export) přes paging (reg 255 page0/page1) + SiLabs apply proceduru (OEB_ALL off → E2 pulse → SOFT_RESET 0xF6 → OEB_ALL on). Status reg 218 (0xDA): bit0 SYS_CAL, **bit2 LOS_CLKIN** (chybí vstupní ref. hodiny!), bit4 PLL_LOL. UART `si5356` = re-init + status.
- **⚠️ KONFIGURACE: 4× 100 MHz, fáze 0/90/180/270° (= 0/2,5/5/7,5 ns), Vstup 10 MHz → VCO 2,2 GHz (N=220) → /22.** Ty 4 fázově posunuté hodiny jsou **reference pro 4-fázový vernier TDC ve FPGA** (reciproký čítač, jemný krok 2,5 ns). **MUSÍ být 90°, NE 45°** — při 45° jemné kódy nesednou na 2,5 ns mřížku TDC → systematická chyba kmitočtu. Fáze v reg (LSB=Tvco/128=3,551 ps): CLK1 r111/112=704=2,5 ns, CLK2 r115/116=1408=5,0 ns, CLK3 r119/120=2112=7,5 ns. **Přesnost čítače = přesnost těch 100 MHz (= ppm 10 MHz vstupu Si5356).**
- **Při změně konfigurace: v CBPro nastav fáze (90° krok!), exportuj „C Code Header" a nahraď `REGMAP[]`** (formát {addr,val,mask} — adresy DECIMÁLNĚ pro 1:1 diff s exportem). **Vynech řádky s `mask==0x00`** (CBPro „do not write"); `mask<0xFF` → read-modify-write, `mask 0xFF` → přímý zápis (`wr_masked` to respektuje, `mask 0` přeskočí).
- **UART `scan1`** = I2C scan na I2C1 (s popisky zařízení). `scanner` zůstává pro I2C4.
- **Diagnostická obrazovka** (`app_gpsdo_render_diag`, tlačítko MENU → ZPĚT): **dvousloupcový layout** (`DG_*` makra v `app_gpsdo.c`). **Levý sloupec:** Teploty TMP117 (0x48/0x49/0x4A) — `last` + `min/max` (z `g_sensors[]`); ADC ADS1115 (AIN0–3). **Pravý sloupec:** *Komunikace + měření FPGA* (`g_spi_text` zeleně/červeně + `g_freq_info` = gate/PH/SEQ), *Reference Si5356* (lock z `g_si5356_status`: LOCK OK / LOS CLKIN! / PLL UNLOCK! / CALIB…), *System/RTOS* (heap free/min, CPU %, uptime). Neplatný senzor = ztlumeně + červený `!`. Refresh ~2×/s z `app_gpsdo_tick` (UiTask), tearing-free přes `prim_stm32_present`.
  - **Datové zdroje:** Si5356 status čte SensorsTask (`si5356_read_status`, reg 218) do `g_si5356_status`/`g_si5356_ok`; RTOS zdraví počítá UiTask (`xPortGetFreeHeapSize`, idle-delta CPU %) do `g_rtos_*`/`g_uptime_s`.
- **NEPOVOLOVAT I2C1 v IOC** (init je ručně v USER CODE).

## UART TX
`_write` (main.c) chráněn `uartTxMutexHandle` (serializace printf z více tasků, jen za běhu
scheduleru) + timeout 100 ms (~1150 B/řádek, bez utínání). Pořád blokující HAL_UART_Transmit.

## Build / flash
STM32CubeIDE: vyber projekt **H757_LED_CM7** → Build (Ctrl+B) → Run (Ctrl+F11, config CM7).
Toolchain (arm-none-eabi) není v PATH tohoto prostředí — build/flash jen z IDE.
