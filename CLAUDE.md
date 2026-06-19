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
- 0xC0000000 (SDRAM/FMC), RGB565, 800×480×2 = 750 KB
- MPU region 0: 2 MB, Write-Through
- **SDRAM = 32 MB** (FMC: 9 col + 13 row bits × 4 banky × 16 bit). FB1/FB2 se vejdou snadno.
- **SDRAM mapa:** **Region 0 (2MB WT, `0xC0000000`):** FB1 `0xC0000000`, FB2 `0xC0100000` (double buffering bench/bounce), `sdram` test buffer `0xC01C0000` (4 KB, ZA framebuffery — dřív byl na `0xC0001000` = uvnitř FB1 a přepisoval displej, opraveno). **Region 1 (4MB WBWA cached, `0xC0400000`):** bignum workspace pro `pi` (10×256 KB). SDRAM celkem 32 MB.

### Akcelerace / linker (POZOR při CubeMX regen)
- **Linker `STM32H757BITX_FLASH.ld` ručně upraven:** přidány sekce `.itcm_text` (load z FLASH do ITCM, hot gfx funkce — kopie v `main.c` USER CODE 1 přes `_siitcm/_sitcm/_eitcm`) a `.dtcm_data` (NOLOAD, plasma LUTs). CubeMX linker NEpřepisuje, ale po ruční regeneraci ověřit.
- `ITCM_FUNC` (gfx.h) = `__attribute__((section(".itcm_text")))` na gfx_pixel/fill/line/fillcircle.
- **DMA2D R2M** (`gfx_fill` ≥1024 px) akceleruje výplně — OK i s LTDC.
- **⚠️ DMA2D L8/CLUT M2M plasma ZAHOZENO:** celoobrazovkový M2M (čte L8 + píše RGB565) konkuroval LTDC o SDRAM sběrnici → podtékání LTDC → problikávání. Plasma kreslí **přímým CPU zápisem RGB565** (pal565 v DTCM). Helpery `gfx_dma2d_*` i `gfx_text_small_w` odstraněny (nevyužité).
- Pozn.: ITCM/DTCM efekt je malý (I/D-cache už pokrývají malý working set); největší CPU výhra zůstává **-O2/Release**.

### FreeRTOS tasky (freertos.c)
| Task | Priorita | Stack |
|---|---|---|
| defaultTask | Normal | 512 B |
| UartTask | Normal | 2048 B |
| I2C4Task | Low | 1024 B |
| UiTask | BelowNormal | 4096 B |
| FpgaTask | Normal | 2048 B |
| UartRxQueue | — | 64 × 1 B |

PRIO_BITS=4, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY=5.

### Periferie
- USART1: 115200 8N1, TX=PB14, RX=PA10, NVIC prio 5. printf přes `_write` (blokující TX, timeout 10 ms ⇒ limit ~115 B/řádek).
- I2C4: TMP117 @ 0x48 (0.0078125 °C/LSB, 1×/s), panel ATTINY @ 0x45 (backlight 200), FT5x06 touch.

## Dotykové UI (gfx.c/h + touch_ui.c/h, UiTask)
Funkční dotykové rozhraní: živá teplota TMP117 + **strip-chart graf** (15–35 °C),
tlačítka (LED, BL± s **výpisem jasu**, CLR, CAL), **paleta 6 barev** pro pero, kreslicí
plocha, indikace doteku, interaktivní rohová kalibrace. `gfx` = RGB565 primitivy + **dva fonty**:
hlavní 5×7 v 8×8 buňce (škálovatelný `gfx_text(...,scale)`) a malý **4×6 `gfx_text_small()`**
(renderuje 2×, advance 10 px). Oba: 0–9, A–Z, symboly, velká písmena + h/z. `gfx_line` = obecná čára (Bresenham).
**Rotující úsečka** (`touch_ui_spin`, 50 px, 24 kroků po 7,5°, ~65 překreslení/s) vpravo nahoře — volaná z UiTask každou iteraci. `UiTask` (BelowNormal, 4 KB).
- **UART:** UI **startuje automaticky po bootu** (`g_ui_enabled=1`). `uioff` vrátí displej test příkazům, `ui` zpět, `cal` kalibrace. UI a test příkazy se o displej dělit nemají.
- **Touch je zrcadlený v X i Y** (panel má H+V flip) — default kalibrace invertuje obě osy: `screen = (799-rawX, 479-rawY)`. `cal` přepíše změřenými hodnotami.
- **I2C4 mutex** (`i2c4MutexHandle`): TMP117 task + touch + backlight sdílí sběrnici, nutná synchronizace.
- **Touch čtení je asynchronní (IT)**: `rd_touch` v touch_ui.c → `HAL_I2C_Mem_Read_IT` + čeká na `i2cDoneSemHandle` (semafor), UiTask přitom spí (ne busy-wait → ~25 % CPU ušetřeno). Callbacky `HAL_I2C_MemRxCpltCallback`/`ErrorCallback` v touch_ui.c, IRQ handlery `I2C4_EV/ER_IRQHandler` v stm32h7xx_it.c (USER CODE), NVIC prio 6 v i2c.c MspInit. TMP117 a backlight zůstávají polling (pod mutexem). **Nepovolovat I2C4 interrupt v IOC** (duplicitní handlery).
- **FT5x06 MUSÍ číst celý touch frame** (31 B z TD_STATUS 0x02, `ft5x06_read_touch`). Částečné čtení (jen 1. bod) → controller drží I2C při multitouchi → další transakce zatuhne → program zamrzne při 2. prstu. Parsuje se jen 1. bod, ale čte se celý rámec.
- Kalibrace je v RAM (po resetu se ztratí, default inverze funguje i bez ní).

## UART příkazy (StartUartTask)
`led on/off`, `ram write/read`, `sdram write/read`, `temperature`, `scanner`, `testDSI`,
`testRED`, `test` (základní RGB565 sanity), `touch`, `touchloop`, `ui`/`uioff`/`cal`, `freq`, `fpgaraw`, `fpgaloop`, `si5356`, `scan1`, `stats`, `bench`, `bounce`, `pi`/`stoppi`, `status`, **`ping`/`screen main`/`clear`/`version`/`help`** (GPSDO LVGL). Neznámý příkaz → `ERR unknown command`.
**`pi`** = počítá číslice π **Gibbonsovým unbounded spigotem** (vlastní bignum, base 2^32) a vypisuje je fontem (scale 2 ≈ 16px, **zeleně**; při přetečení obrazovky nová stránka = clear + odshora); běží v `defaultTask` **naplno bez throttle** (rané číslice levné → letí, hlouběji se zpomaluje — povaha π). `stoppi` zastaví a obnoví UI; po zastavení `run_pi` vypíše **na UART** počet spočítaných desetinných míst + čas (ms) + číslic/s. Bignum workspace v **cached SDRAM** (MPU region 1 @ `0xC0400000`, 4 MB WBWA), 10 slotů × 256 KB (`PI_MAXLIMB`), strop ~90 000 číslic než přetečení. Dělení malého podílu binárním hledáním. Yield (`osDelay 1`) každých 64 číslic kvůli `stoppi`/ostatním taskům.
`bench` = plasma benchmark (per-pixel). `bounce` = 16 odrážejících se tvarů (čtverce/kruhy/troj/pětiúhelníky/úsečky). Oba: HUD vlevo nahoře **`FPS:NN M7:NN%`** (M7 load = 100−idle% přes `ulTaskGetIdleRunTimeCounter`). VSYNC čekání používá `osDelay(1)` (yield → idle běží → load má smysl, ne pořád 100 %).
**DMA2D (Chrom-ART)** akceleruje `gfx_fill` ≥1024 px (UI mazání, tlačítka, fills) — registrově v gfx.c, `gfx_accel_init()` v bootu. Per-pixel matematiku (plasma) DMA2D nezrychlí → na to `-O2`/Release build.
(Diagnostické příkazy testY/testM/test*Long a pure_r/g/b byly po vyřešení displeje odebrány.)

## GPSDO LVGL obrazovka (theme.h, lv_port_disp.c, ui_main_screen.c — screen-mode)
Statická hlavní obrazovka GPSDO čítače (mockup v9) přes **LVGL v9**, spouštěná UART příkazem. **Iterace 1: bez živých dat — všechny hodnoty pevné konstanty.**
- **Příkazy:** `ping`→`pong`, `screen main`→vykreslí (OK), `clear`→prázdná obrazovka bg (OK), `version`→`gpsdo-ui v0.1`, `help`. Odpovědi CRLF.
- **Screen-mode (`g_screen_mode`):** po bootu běží **stávající gfx UI**; `screen main`/`clear` nastaví `g_screen_req` (UART task), **UiTask** ho obslouží — **LVGL kreslí VÝHRADNĚ z UiTask** (není thread-safe). `ui` vrátí kontrolu gfx UI (`g_screen_mode=0`). Koexistence přepínačem.
- **Display port (`lv_port_disp.c`):** LVGL renderuje **přímo do framebufferu @0xC0000000** (RGB565, `LV_DISPLAY_RENDER_MODE_DIRECT`, buffer == FB → žádná kopie, flush jen `__DSB`). `lv_tick_set_cb(HAL_GetTick)`. Při vstupu nastaví LTDC adresu zpět na 0xC0000000 (bench/bounce ji dočasně mění).
- **⚠️ Externí závislosti (NEJSOU v repu, nutno dodat):** (1) **samotná LVGL v9** do `Middlewares/Third_Party/lvgl` + `lv_conf.h` na include path (`LV_CONF_INCLUDE_SIMPLE`); (2) **vygenerované fonty** JetBrains Mono + Inter (12–75 px, `tnum`) přes LVGL font converter → `theme.h` mapuje role; dokud nejsou, `FONTS_READY=0` mapuje vše na `montserrat_14` (layout sedí, typografie ne).
- **Fidelita:** LVGL zvládá gradienty/zaoblení/stíny/dash z mockupu. **Radiální gradient pozadí** ale LVGL neumí → fallback plná výplň `THEME_BG_0` (dle briefu 3.3). Grafy (Allan, sparkline) jako `lv_line` polylinie, anténa zjednodušená na úsečky. Pixel-perfect až s reálnými fonty.
- **Paleta** v `theme.h` (RGB888 → `lv_color_hex`). Layout absolutními souřadnicemi dle sekce 3 briefu.
- **POZOR:** kód psán mimo IDE (bez toolchainu/LVGL/fontů) → **neověřený compile**; první build v CubeIDE doladí (chybějící LVGL/fonty/případné v9 API odchylky).

## FPGA čítač kmitočtu (fpga_freq.c/h, FpgaTask, SPI2)
STM32 = SPI master, FPGA = slave. **SPI2**: master, mode 0, MSB, 8-bit, MOSI=PB15, SCK=PI1,
MISO=PI2, **CS=PB12 (manuál GPIO, active-low)**. Bring-up **1 MHz** (`fpga_freq_init` zvolí
prescaler dle `HAL_RCCEx_GetPeriphCLKFreq(SPI123)`). **SCK strop dle kontraktu FPGA (GW1NR-9 oversampling): cíl ≤6 MHz, absolutní max ~10 MHz** — `FPGA_SCK_TARGET_HZ` (hlídá `#error` na `FPGA_SCK_MAX_HZ`).
- **Timing (kontrakt):** CS setup/hold ≥1 µs (dáváme 2), **mezi rámci ≥20 µs** (FPGA potřebuje ~124 cyklů @10 MHz na složení rámce; dáváme 25). Prodlevy přes **DWT cyklový čítač** (`delay_us`, ne NOP-loop — DWT už zapnut pro runtime staty). Po bootu **`osDelay(250)` v StartFpgaTask** než se začne clockovat (FPGA piny 54–57 jsou config piny, musí dokončit load z flash).
- **CRC self-test (akceptační krok 1):** `fpga_freq_crc_selftest()` ověří `crc16("123456789")==0x29B1`; při selhání se `g_init_ok=0` a SPI komunikace se **nezahájí** (poll/restart hned vrací false).
- **Stav SPI/komunikace na displeji:** `fpga_freq_format_status()` skládá řádek `SPI <x.xx>MHZ LINK:OK/-- SEQ:<n> CRC:<n>` (rychlost SCK, živost linky, posl. SEQ, počet CRC chyb). FpgaTask ho po každém pollu uloží do `g_spi_text`/`g_spi_ok` (překreslí jen při změně), UiTask vykreslí přes `touch_ui_set_spi()` fontem scale 3 (~24px) nad touch statusem — **zeleně** když link žije, **červeně** když ne.
- **Pevný 64B full-duplex rámec**: MAGIC 0xA5, VERSION, TYPE, FLAGS/STATUS, SEQUENCE(LE32),
  PAYLOAD_LEN, RESERVED, 50B payload, CRC16(LE) na konci. CRC = **CRC-16/CCITT-FALSE** (0x1021/0xFFFF), pokrývá byte 0..61.
- Model: `FpgaTask` polluje **~20 Hz** (`osDelay(50)`), posílá **ACK** (TYPE 0x06, SEQUENCE=poslední) → FPGA full-duplex vrací aktuální **DATA** (TYPE 0x80). Platné = CRC ok + DATA_VALID + DATA_FRESH + nová SEQUENCE. Polling je úmyslně rychlejší než tempo měření (FPGA gate **0,25 s reciproké → ~4 nová měření/s**) kvůli nízké latenci; protokol je pull/ACK, takže rychlejší polling nezpůsobí ztrátu měření (FPGA shodí DATA_FRESH až po ACK té SEQ). Každé čerstvé měření se zobrazí (žádný throttle).
- **Dva předděliče (4-fázové reciproké měření):** **pin28 = /4** (primár, nejlepší rozlišení), **pin27 = /16** (vyšší rozsah / cross-check), měří se současně. `fpga_freq_select()` volí zobrazovaný zdroj: **/4 dokud je bez chyby a < ~380 MHz, jinak /16**. Rozsah: na pinu do ~100 MHz → reálně ~400 MHz (/4) / ~1,6 GHz (/16). Headline ukazuje zvolený zdroj, info řádek `<src> PH:<present>/<fine> GATE:<ns>NS SEQ:<n>[ chyba]`.
- **Detekce ztráty signálu (SIGNAL_LOST):** FPGA má **autoritativní watchdog** — ~2,5 s bez dokončeného měření → `error_flags` bit1 SIGNAL_LOST + DATA_VALID=0. FpgaTask čte `fpga_freq_signal_lost()` (latch posledního DATA rámce, funguje i při VALID=0) a při ztrátě **nebo mrtvém linku** nastaví `g_freq_stale=1` → UiTask ztlumí kmitočet na **šedou** (`touch_ui_set_freq(s, stale)`). **Dřívější SEQ-staleness heuristika ODSTRANĚNA** (falešně hlásila stale u nízkých kmitočtů, kde se reciproké okno legitimně protáhne) — teď se věří FPGA flagu.
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
- **TMP117** @ 0x49, 0x4A → `g_temp49`, `g_temp4A` (čteno v StartI2C4 každou 1 s).
- **ADS1115** @ 0x48 → `g_ads_mv[4]` (4 single-ended kanály AIN0–3, PGA ±4.096 V, 128 SPS, single-shot). Driver `ads1115_start`/`ads1115_read_raw`/`ads1115_raw_to_mv`. **AIN0/1 přímo; AIN2 = 12V větev přes odporový dělič (×13417/2814, kalibrace 13.417 V @ 2.814 V), AIN3 = 5V větev (×4978/2526, 4.978 V @ 2.526 V)** — přepočet ve StartI2C4 ukládá do `g_ads_mv[2]/[3]` skutečné napětí větve.
- **Si5356A** @ 0x70 (clock generator) → `si5356.c/h`, `si5356_init(&hi2c1)` v main.c USER CODE 2 (po `MX_I2C1_Init`, před schedulerem → bez mutexu). Aplikuje **ClockBuilder Pro register map** (`REGMAP[]`, oficiální CBPro „C Code Header" export) přes paging (reg 255 page0/page1) + SiLabs apply proceduru (OEB_ALL off → E2 pulse → SOFT_RESET 0xF6 → OEB_ALL on). Status reg 218 (0xDA): bit0 SYS_CAL, **bit2 LOS_CLKIN** (chybí vstupní ref. hodiny!), bit4 PLL_LOL. UART `si5356` = re-init + status.
- **⚠️ KONFIGURACE: 4× 100 MHz, fáze 0/90/180/270° (= 0/2,5/5/7,5 ns), Vstup 10 MHz → VCO 2,2 GHz (N=220) → /22.** Ty 4 fázově posunuté hodiny jsou **reference pro 4-fázový vernier TDC ve FPGA** (reciproký čítač, jemný krok 2,5 ns). **MUSÍ být 90°, NE 45°** — při 45° jemné kódy nesednou na 2,5 ns mřížku TDC → systematická chyba kmitočtu. Fáze v reg (LSB=Tvco/128=3,551 ps): CLK1 r111/112=704=2,5 ns, CLK2 r115/116=1408=5,0 ns, CLK3 r119/120=2112=7,5 ns. **Přesnost čítače = přesnost těch 100 MHz (= ppm 10 MHz vstupu Si5356).**
- **Při změně konfigurace: v CBPro nastav fáze (90° krok!), exportuj „C Code Header" a nahraď `REGMAP[]`** (formát {addr,val,mask} — adresy DECIMÁLNĚ pro 1:1 diff s exportem). **Vynech řádky s `mask==0x00`** (CBPro „do not write"); `mask<0xFF` → read-modify-write, `mask 0xFF` → přímý zápis (`wr_masked` to respektuje, `mask 0` přeskočí).
- **UART `scan1`** = I2C scan na I2C1 (s popisky zařízení). `scanner` zůstává pro I2C4.
- **Displej**: všechny 3 teploty (0x48/0x49/0x4A) malým fontem nahoře, 4 napětí ADS malým fontem nad kreslicí plochou; canvas zmenšen na **CANVAS_Y 242** (postupně 198→214→242, místo pro SPI status řádek scale 3 @ SPI_Y 178, touch status @ STATUS_Y 206, ADS @ SENS_ADS_Y 224).
- **NEPOVOLOVAT I2C1 v IOC** (init je ručně v USER CODE).

## UART TX
`_write` (main.c) chráněn `uartTxMutexHandle` (serializace printf z více tasků, jen za běhu
scheduleru) + timeout 100 ms (~1150 B/řádek, bez utínání). Pořád blokující HAL_UART_Transmit.

## Build / flash
STM32CubeIDE: vyber projekt **H757_LED_CM7** → Build (Ctrl+B) → Run (Ctrl+F11, config CM7).
Toolchain (arm-none-eabi) není v PATH tohoto prostředí — build/flash jen z IDE.
