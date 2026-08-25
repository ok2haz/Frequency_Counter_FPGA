# H757_LED — projektová poznámka

## Přehled
STM32H757 dual-core (CM4 + CM7) projekt generovaný v STM32CubeIDE.
- **CM7** = hlavní jádro, veškerá logika (FreeRTOS, displej, SDRAM, I2C, UART).
- **CM4** = konektivita (výhled: ETH/SCPI/web); dnes GPIO+timer (beep+LED) + **IPC konzument** + vlastní CPU% přes DWT. ✅ **CM4 BĚŽÍ (ověřeno na HW 2026-08-14)** — `CM4: alive (IPC heartbeat)`, header „4:xx%". ⚠️ **ALE JEN BEZ AKTIVNÍ SONDY** — viz „Dvoujádro / IPC".
- Veškerý vývoj displeje je v `CM7/Core/Src/`.
- **Dvoujádro CM7↔CM4** viz sekce „Dvoujádro / IPC" níže + `NAVRH_ARCHITEKTURA_CM7_CM4.md` §11 + `DUALCORE_BRINGUP_CHECKLIST.md`.

Hardware: STM32H757 → DSI (1 lane) → **TC358762** DSI-to-DPI bridge → Waveshare **4,3"** 800×480 IPS panel. ATTINY MCU @ I2C 0x45 (power/backlight/reset), TMP117 @ 0x48 (teplota), FT5x06 (touch).

> **📐 Fyzická velikost panelu je pro UI KLÍČOVÁ: 4,3" @ 800×480 → 8,54 px/mm (217 DPI), aktivní plocha ≈ 93,7 × 56,2 mm.**
> Při návrhu layoutu počítej v **milimetrech, ne pixelech**: dotykový cíl potřebuje **≥60 px (7 mm)**, pohodlně 77 px (9 mm);
> text má cap height ≈ 0,72 × `ascent` fontu, takže `mono_16` = 1,35 mm ≈ 9 úhlových minut na 50 cm (doporučeno 16–24′)
> → přístroj je čitelný z ~30–35 cm. Layout byl původně laděn pro 7" panel, kde je všechno o 39 % větší — proto se v UI
> pořád najdou prvky pod hranicí (viz TODO #11 ve `STATUS.md`). Přepočet: **1 mm = 8,54 px**.
> **Přehled ideálních vs. aktuálních velikostí všech prvků = `UI_SIZES.md` — po každé změně layoutu ho aktualizuj.**

**Footer RUN/STOP (2026-07-20): label = AKCE, ne stav.** Běží-li měření, tlačítko nabízí **červené „STOP"**
(`UI_BUTTON_STOP` — nová varianta v libui + `btn_stop_*` v obou paletách); při zastaveném **zelené „RUN"**.
Dřív to bylo obráceně (label = stav), což mátlo. Stav navíc nese **podbarvení velkého kmitočtu**:
při STOP se zóna čísla překryje `UI_COLOR_FREQ_STOP_BG` (poloprůhledná červená, ~15 %) → na první pohled
je vidět, že měření stojí. ⚠️ Alfa < 0xFF vyřazuje DMA2D fast-path (CPU fill), ale **při STOP neběží 20Hz
`tick_freq`**, takže se kreslí jen při plném renderu a při stisku → CPU dopad ~0. Přepnutí RUN/STOP proto
MUSÍ volat `screen_main_redraw_freq_area()` (per-segment dirty cesta by podklad nepřekreslila).

**Hlavní obrazovka má DVĚ rozložení, přepínatelná v okně DISPLEJ** (`render_body_grid` → `_hybrid` / `_classic`, persist v syscfg; vráceno 2026-08-23 na přání uživatele poté, co byla A/B větev 2026-08-22 odstraněna — TODO #14 — a starý `s_layout_old`/`*_v1` smazán). ⚠️ **Přepínač je v okně DISPLEJ, NE ve footeru**: původně přebíjel footer slot 0, čímž se ztratil PERIOD/FREQ toggle; rozložení je vlastnost displeje, patří tedy vedle Vzhledu. ⚠️ **KLASICKÉ je zamrzlá větev** — nemá easing statistik ani trendu (tiky `screen_main_tick_stats_anim`/`_trend_anim` v něm hned vracejí 0) a záměrně se v něm už nedělají změny; každá další úprava hlavní obrazovky míří do hybridního. **KLASICKÉ**: Allan 53 % šířky, pravý sloupec stohovaný offset (v.54, mono_16) / trend / signál (v.43), všechny mezery `SCR_MAIN_CARD_SECTION_GAP`.
**HYBRIDNÍ (výchozí)**: **vlevo Allan graf 364×242** (přes CELOU výšku mřížky, plné osy — sdílený `allan_plot` má od #11(2b) popisky mono_16 v kartě i okně), **vpravo statistiky** Offset/σ@1s/Drift (3× 125×64, hodnoty mono_18 — proto `SCR_MAIN_GRID_LEFT_RATIO` snížen 53→47 %, jinak by se „+9,9×10⁻¹⁰" nevešlo), **pod nimi mini trend** (398×100) **a úplně dole RF signál bargraf** (398×54) — jen v pravé části. Tap na Allan → okno ALLAN (s_view=23), tap na trend → fullscreen trend (s_view=9). Horní hrana mřížky MUSÍ zůstat na `SCR_MAIN_GRID_Y` (166) — clear oblast velkého čísla (`redraw_freq`) končí přesně na ní. **Pilulky v headeru**: `UI_DIM_PILL_H=46` (5,4 mm, strop = 4 px před spodní linkou hlavičky) + hit-slop přes výšku headeru (`pt_in_pill`, efektivně ~52 px). ⚠️ **Řadu pilulek limituje `HDR_PILL_LIMIT` (590, sníženo z 640 kvůli dvouřádkovému CPU bloku CM7/CM4 mezi pilulkami a hodinami, x 592..642, `screen_main_redraw_cpu`) + fit-check `hdr_pill_fit`** — rozpočtem je levý okraj CPU bloku resp. clear zón sekundového redrawu času/data (x=644, `screen_main_redraw_time`); pilulka, která se nevejde, se vynechá (pořadí = důležitost: GNSS, SYS, SAT, HDOP, HOLD, **CAL**). **CAL = kompaktní „ribbon" chip** (LED + „CAL", bez hodnoty; `has_led`, `UI_PILL_NORMAL`) — placeholder kalibračního stavu, ~67 px místo dřívější „CAL 4 min" pilulky (~90 px), takže se za HOLD před CPU blok v typickém stavu vejde; je **poslední v pořadí**, takže při přetlaku vypadne jako první (HOLD = živý holdover stav zůstane). (Historie: 2026-07-26 dočasně úplně odstraněn, pak vrácen jako užší chip.)

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
| HSE | **25 MHz (bypass)** — od 2026-08-22 (v0.6.0), dřív 10 MHz; X1/TCXO sdílený s ETH PHY | |
| SYSCLK | 480 MHz | PLL1 **M=5, N=192**, P=2 (VCO 960 = stejné; vstup 25/5=5 MHz) |
| Pixel clock (LTDC) | 25 MHz | PLL3 **N=35, FRACN=0**, R=7 (VCO 175 = stejné) |
| DSI bit clock | 700 Mbps/lane | DSI-PLL **NDIV=28**, IDF=1, ODF=1 (VCO 1400 = stejné) |
| DSI byte clock | 87.5 MHz | bit/8 |
| DSI escape clock | 17.5 MHz | TXEscapeCkdiv=5 |

> **⚠️ Přechod 10 → 25 MHz HSE (2026-08-22, v0.6.0):** X1/TCXO byl vyměněn za 25 MHz,
> aby dostal ETH PHY LAN8742A svých 25 MHz (sdílený oscilátor, dřív 10 MHz → PHY nenaběhl,
> STATUS #24). **Princip přepočtu: všechna VCO zůstala identická, změnil se jen vstupní
> dělič** (`M` 1→5, vstup PLL 10→5 MHz) **+ DSI NDIV** (70→28) — takže **všechny výstupní
> frekvence jsou beze změny** (SYSCLK 480, LTDC 25, DSI 700, SPI123/FMC z PLL2 **M=5 N=40**,
> ADC3 25). `HSE_VALUE`=25000000 (oba `hal_conf.h`). ⚠️ **`HSE_VALUE` je KRITICKÉ pro
> `fpga_freq_init`** (počítá SPI prescaler přes `HAL_RCCEx_GetPeriphCLKFreq`). ⚠️ OCXO 10 MHz
> → Si5356 → FPGA čítač je **JINÝ, oddělený oscilátor** (na FPGA desce) → přesnost měření beze
> změny. ⚠️ **Tento FW nenaběhne na desce s 10 MHz HSE** (PLL by chtěl VCO 2400 MHz → nezamkne
> se → `Error_Handler`).

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
- **Dirty-rect copy-forward:** po flipu se do nového back zkopírují **jen změněné oblasti** (ne 768 KB) — levné. Sledování v DMA2D backendu: každý fill/blit zaznamená svůj obdélník (`mark_dirty`). ⚠️ **Každý partial redraw MUSÍ začít fill/blit (clear)**, jinak se ta oblast nezkopíruje dopředu (problikávání). Triple → nový back je 2 snímky starý → kopíruje se sjednocení dirty z posledních 2 snímků. **Dedup (`copy_forward_dedup`, 2026-08-06):** prev+cur se často shodují/překrývají (freq rect a stat karty jsou v obou seznamech) → před kopírováním se vyhodí **shodné a plně obsažené** obdélníky (`rect_covers`) → žádný redundantní DMA2D blit. ⚠️ Kopírovaná množina zůstává **přesně sjednocením** (keep drží jen původní obdélníky, nikdy větší) → žádné riziko problikávání; **žádný bbox-merge** (ten by kopíroval mimo dirty).
  - **⚠️ `mark_dirty` se volá JEN z `d2d_fill`/`d2d_blit_ex`** (DMA2D cesta) — cokoli kreslené přes `prim_internal_blend_px` (per-pixel alpha blend: `aa_corner` u zaoblených rohů `prim_fill_rect_rounded`, `prim_draw_arc`, `glow`, AA hrany tvarů) **zapisuje přímo do framebufferu a `mark_dirty` OBCHÁZÍ**. Funguje to jen proto, že takový obsah je vždy uvnitř dřívějšího REPLACE clear/blit (jeho dirty rect ho „poveze s sebou" — stejný princip jako u textu, viz DMA2D glyph blend výše). **Past:** tlačítko/pilulka, které mění VARIANTU (a tedy barvu) při partial redrawu **bez** předchozího `blit_bg_region`/`fill_rect(..., REPLACE)`, nechá v rozích (mimo poloměr zaoblení) 2 snímky staré „duchy" — objevené 2026-07-19 u `cas_upd_mode` (okno Čas, tlačítko AUTO CET/CEST, jediné místo v appce měnící variantu bez clearu; opraveno přidáním `prim_fill_rect(rect, BG_CARD, REPLACE)` před `ui_button_render`). Ostatní partial-redraw tlačítka (MUTE/AUTODIM/LANG) mají stejnou mezeru, ale je neviditelná, protože drží stále stejnou variantu → identická barva „ducha" a aktuálního stavu.
- **`present` jen při změně:** `draw_diag_values`/`screen_main_redraw_time` vracejí, zda kreslily; volající flipne jen pak (jinak zbytečný flip).
- Volá `app_gpsdo` po každém vykreslení; UiTask LTDC adresu neřídí.
- **⚠️ Cache koherence:** DMA2D obchází CPU D-cache → po každém fill/blit se zneplatní cílová oblast (`SCB_InvalidateDCache_by_Addr`); WT → bez dirty řádků. Bez toho AA hrany textu čtou stará data („px šum").
- **⚠️ `testRED`/`test` UART příkazy** píšou natvrdo do FB0 — při page-flipu nemusí být vidět (bring-up reziduum).
- ⚠️ **Freeze NEBYL bufferem** — byl to `printf` v SensorsTasku (malý stack); buffer má CPU dopad ~0 % (dirty-rect). Off-screen canvas API odstraněno (nepoužité).

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
| defaultTask | Normal | 2560 B (GPS drain + rtc_app_tick snprintf + syscfg persist + alarm_tick + watchdog_supervise + **ipc_publish/ipc_service + CM4 stall detekce** + USB pump) |
| UartTask | Normal | 4096 B |
| I2C4Task | Low | 1536 B |
| UiTask | BelowNormal | 8192 B |
| FpgaTask | Normal | 2048 B |
| UartRxQueue | — | 64 × 1 B |
| GpsRxQueue | — | 256 × 1 B |

⚠️ **Tabulka drž synchronní se skutečnými `.stack_size` v `freertos.c`** (audit 2026-08-17
odhalil rozjetí: dřív tu bylo 1536/2048 B, realita 2560/4096 B — defaultTask rostl kvůli
`rtc_app_tick` snprintf, UartTask kvůli TODO #9 bumpu 512→1024 words). Součet stacků
18 432 B ze 32 768 B heapu = 14 336 B rezerva na malloc/haldu.

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

### Metrologická vrstva (2026-08-18) — co dělá z čítače přístroj

**Overlapping ADEV + MDEV + HDEV.** Původní `adev_stage` byl **non-overlapping**:
při τ = m·τ0 rozdělil ring na M/m disjunktních bloků a zahodil většinu dostupných
dvojic. Overlapping varianta (Riley, NIST SP1065) vytěží M−2m+1 překrývajících se
členů z **týchž dat** — na dlouhých τ, kde je vzorků nejméně, je to rozdíl mezi
„pár párů" a „řádově víc". Konfidenční pás (`ns`) to zohledňuje a je proto užší.
**MDEV** rozliší bílý a blikavý fázový šum (v ADEV mají stejný sklon), **HDEV**
je díky druhým diferencím imunní vůči lineárnímu driftu. ⚠️ **TDEV je nově
EXAKTNÍ** — definice je τ·MDEV/√3; dosud se počítala z ADEV, což platí jen když
MDEV ≈ ADEV, tedy právě ne u fázového šumu. ⚠️ **MTIE zůstává odhad** a popisek to
přiznává: přesný MTIE potřebuje uloženou fázi (⬅ #36). Přepínač v okně ALLAN má
5 segmentů = 60 px = **přesně projektové minimum** dotykového cíle, šestý se
už nevejde.

**Okno ANALÝZA (s_view=41).** Kam se to vešlo: nikam — footer MĚŘENÍ je plný
a karta má 9 řádků. Řešení: **třetí sourozenec v existující rotaci**
ČÍTAČ → MĚŘENÍ → ANALÝZA → ČÍTAČ; stávající tlačítko jen mění popisek, takže to
nestojí ani jeden nový pixel. Obsah: **rozpočet nejistoty** (rozlišení hradla
√2·tdc/gate + σy@τ + ppb reference, sčítané kvadraticky; zobrazuje se **rozklad**,
ne jen součet), **počet platných číslic** = log10(1/u), **drift Vc** a **tempco
Vc↔T**. ⚠️ U nejistoty je vždy uvedeno **k=2** — u vs U se liší dvojnásobně.
⚠️ U prokladů se vždy ukazuje **korelace r** a při |r| < 0,5 se výsledek označí
za neprůkazný; bez toho by uživatel četl proklad šumu jako měření.
⚠️ **Tempco funguje už dnes, bez FPGA** — teplota OCXO i Vc se logují od začátku.
Datalog se čte **jedním průchodem pro obě osy** + decimace na ~200 bodů (0,2 s
místo 9 s) a **jen při vstupu do okna**.

**Filtr měření (#5).** PRŮMĚR 8 sníží šum o √N, ale jediný výstřelek rozprostře
do N hodnot; MEDIÁN 9 (liché okno = ostrý) výstřelek zahodí, ale šum nesníží —
proto obojí. ⚠️ **Filtruje se POUZE zobrazovaná hodnota**; do Allan/histogramu/
datalogu jdou dál syrová měření, protože filtrovaná data by σy(τ) uměle vylepšila.

## Vývoj bez FPGA desky — co ho odblokovalo (2026-08-18)

**Emulátor FPGA rámců (`fpga_sim_*`, UART `fpgasim`).** Headline byl do teď náhodná
procházka v `screen_main.c`, která celou cestu z FPGA **obcházela** — netestovaná tak
zůstávala celá naše polovina kontraktu. `fpga_freq_poll()` má přitom ideální injekční bod:
`xfer()` je jediné místo, kde vzniká obsah `rx[]`. Emulátor složí syntetický 64B DATA rámec
**včetně správného CRC** a všechno za tím (MAGIC, CRC, `parse_data`, latch, VALID/FRESH/SEQ,
výběr odbočky) běží beze změny. Odemyká #1 a dovoluje vyrobit, co na stole nezařídíš:
SIGNAL_LOST, skok přes práh 380 MHz, poškozené CRC, díru v `phase_status`.
⚠️ **Nevaliduje** drátovou vrstvu, časování ani logiku FPGA. ⚠️ **Pojistky:** výchozí vypnuto,
nepersistuje se, `DATALOG_F_SIM` (bit 6) zůstane v logu navždy, info řádek začíná `SIM `,
`status` to hlásí. Tempo: nová SEQ jen ~4×/s (reálná FPGA má gate 0,25 s), i když FpgaTask
polluje 20×/s — jinak by emulace vyráběla 20 měření/s a zkreslila vše, co se opírá o tempo.

**Disciplinace LSE podle GPS (`rtc_lse_*`, UART `rtc cal`).** `rtc_try_sync()` každých 10 min
přepsal čas a odchylku **zahodil** — přitom právě ona je měření driftu vlastního krystalu.
Fáze se snímá **na hraně GPS sekundy** (GPS dává jen celé sekundy) přes sub-sekundový registr
RTC (1/256 s = 3,9 ms); drift = Δfáze/Δčas. ⚠️ Latence NMEA je přibližně konstantní, takže se
v **rozdílu** dvou fází vyruší — proto se měří rozdíl, ne absolutní fáze. ⚠️ Vzorkovač běží
**před** 1Hz throttlem `rtc_app_tick` (jinak by se s 1 Hz GPS aliasovalo). Okno 8 min → ~12 ppm
na okno; běžící průměr ~2 ppm po 6 h, ~1 ppm po dni. Korekce jde do **`RTC_CALR`** (smooth
calibration, krok 0,954 ppm) až od 12 oken a nejvýš 1×/h; skládá se (CALR už nějakou drží).
`RTC_CALR` žije v backup doméně → přežije reset bez zvláštní persistence.

**Flight recorder (`flightrec.c`, UART `flightrec`).** Crash black-box říká *co* se stalo,
ne *co se dělo předtím* — proto TODO #18 visel. Kruhový buffer 60 s (CPU, heap, **nejmenší
volný stack**, teploty, I2C chybovost) žije v RAM; do W25Q se sype až při poruše (detekovaný
stall, hook přetečení stacku/malloc, `flightrec test`). ⚠️ **Cílový sektor je předem smazaný** —
při poruše zbývá do IWDG ~1,5 s a erase trvá 50–400 ms, takže by se často nestihl.
⚠️ **Nezapisuje se z HardFault handleru** (`w25q wait_ready` ustupuje scheduleru → v exception
kontextu by zatuhlo).

**Rekonstrukce dlouhých τ ADEV z datalogu.** Restart dosud vynuloval pyramidu. ⚠️ Vzorek z logu
se vkládá od **stage 1**, ne 0: stage 1 má τ = 10 s = přesně kadenci logu, takže převod je
exaktní. Sypat log do stage 0 (τ0 = 1 s) by dalo σy(τ) špatně **o celý řád** a přitom věrohodně.
⚠️ **Trend pyramida se záměrně nerekonstruuje** (decimuje po 4, 10 s na žádnou stage nesedne).
Běží po dávkách 20 záznamů/tik (~2 min na pozadí); přeskakuje `freq == 0` a `DATALOG_F_SIM`.

**SCPI nad IPC snapshotem (`ipc_scpi_src_from_snap`, UART `scpi ipc <cmd>`).** Největší riziko
TCP poloviny #25 není socket, ale jestli snapshot nese vše, co SCPI potřebuje. `scpi ipc X`
pustí tentýž parser nad snapshotem a porovná s `scpi X` → **SHODA / ROZDIL**, ověřitelné bez ETH
i bez flashe bank 2. ⚠️ Zbývá přeložit `scpi.c` do CM4 obrazu (linked resource v `CM4/.project`)
+ řetězcový kanál ve sdílené struktuře.

## UART příkazy (StartUartTask)
`led on/off`, `ram write/read`, `sdram write/read`, `temperature`, `sensors`, `adcraw`, `scanner`, `testDSI`,
`testRED`, `test` (RGB565 sanity), `touch`, `touchloop`, `scan1`, `si5356`, `freq`, `gps`, `gpsraw`, `rtc`, `fpgaraw`,
`fpgaloop`, `stats`, `status`, `ui`, `qspiid`/`qspitest`/`qspispeed`/`storetest` (W25Q),
**`screenshot [sd]`** (export obrazovky do BMP; `screenshot.c`): **`screenshot sd`** = doporučená cesta — snímek se nejdřív zkopíruje do SDRAM scratche (anti-tearing, UiTask jinak během zápisu flipne) a FatFs zapíše celý `SHOTnnn.BMP` na kartu. Holé **`screenshot`** posílá 1,15 MB přes USB CDC (~sekundy, ⚠️ best-effort tok + snímá živý FB → u animované obrazovky pruhy ze dvou framů),
**`autocal`** (self-check referencí/napájení VREF/12V/5V/VBAT → PASS/WARN/FAIL; staged kroky ADC/timebase/RF; `autocal.c`, ROZPRACOVÁNO),
**`membench`** (benchmark všech pamětí — rychlost zápisu/čtení + hledání chybných bitů; ⚠️ destruktivní **jen** pro vyhrazený SDRAM a W25Q scratch, interní FLASH se jen čte — viz sekce „Benchmark pamětí"),
**`datalog [on|off|erase|dump]`** (záznam stability, viz „Datalog"), **`stacktest yes`** (⚠️ záměrně
přeteče stack UartTasku → IWDG reset; ověření řetězce detekce, viz TODO #10),
**`beep`/`beep test`** (testovací pípnutí, mute platí i pro test — odpověď na to upozorní),
**`beep on`/`beep off`** (globální mute, persist BKP_DR2), **`selftest`** (čistě-logické unit testy
za běhu: CRC16 vektor, hystereze /4↔/16 přes `fpga_freq_select_core` (bezstavové jádro), GPS parser
helpery (`gps_selftest`), fmt_frac+hist_h vektory (`screen_main_selftest`), Maidenhead
(`app_gpsdo_selftest`), kalendář+DST (`rtc_selftest`), datalog záznam+CRC+čas (`datalog_selftest`)
— žádný HW, žádný sdílený
stav; destruktivní testy zvlášť: `qspitest`/`storetest`), **`enc`** (encoder #29: TIM1 v encoder mode na PA8/PA9 + tlačítko PC13, 10 s živého výpisu
kroků a stisků; piny si nastavuje sám, **nic v `.ioc`**. ⚠️ Jen HW vrstva — model fokusu v UI
neexistuje a je to návrhové rozhodnutí, ne kód), **`eth`/`eth clk`** (ETH bring-up F0: bit-bang SMI z CM7 — reset PHY, sken adres 0–31, ID/BMCR/BMSR LAN8742A; `clk` změří REF_CLK na PA1 přes TIM2. Piny si nastavuje sám, **nic v `.ioc`** — viz `ETH_BRINGUP_CHECKLIST.md` §2. Bring-up reziduum jako `fpgaraw`), **`ping`/`screen main`/`clear`/`version`/`help`**.
`rtc` = RTC čas (`g_rtc_text`) + zda je synchronizovaný z GPS (viz „RTC").
**`status`** = od 2026-07-20 plná diagnostika (dřív jen „RUNNING"): verze + uptime, **příčina resetu
+ crash black-box** (`stall:UiTask`, `stack:UartTask`, …), RSR, heap free/min, CPU %, **volný stack
všech 5 tasků**, **stav CM4** (`CM4: ABSENT/alive/SILENT, stall x<n>` — viz sekce Dvoujádro), **FPGA link
+ počet CRC chyb + jak dávno byla poslední** (`fpga_freq_crc_count/last_age_s`)
a stav datalogu. První místo, kam sáhnout při náhodném restartu. (Reset sám nese „uptime od poslední" =
aktuální uptime — čas od posledního resetu; počet resetů se napříč power-cyklem nedrží, persistence
záměrně nezvolena.)
`temperature` = TMP117 0x48 + příznak `(STALE)` při chybě čtení. `sensors` = dump všech 10 senzorů
(`last/min/max/avg`, stav OK/ERR, `err_total/streak/`**`last`**`/samples`; **`last=<s>`** = uptime od poslední
chyby čtení daného senzoru, `err_last_ms` v `sensor_stat_t`, „-" bez chyby) — viz `g_sensors[]`/`sensor_stat.h`.
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
- **⚠️ HEADLINE = REÁLNÁ DATA (#1, 2026-08-25, KÓD HOTOVÝ, NEOVĚŘENO NA HW).** Velké číslo + **všechny statistiky** (Allan, drift, offset, Math, spektrogram, trend) čtou jediný hinge `s_freq_n` v `screen_main.c`, který nově žene **reálný/emulovaný kmitočet z FpgaTasku** (`g_freq_x100000`/`g_freq_seq`/`g_freq_valid`, plní je FpgaTask vedle `g_freq_text`). `freq_advance()` volí zdroj: platné měření → REAL; bez měření (mrtvý link, `fpgasim` off) → **SIM fallback** `freq_step()` s viditelným **„SIM" markerem** u čísla. **Emulovaná data (`fpgasim`) jdou reálnou cestou driveru** → berou se jako REAL (bez markeru) — proto se celý řetězec dá testovat bez FPGA desky. **Dynamický formát Hz–GHz**: `num_build_for()`/`num_layout()` staví segmenty/separátory/geometrii podle magnitudy měření (rebuild jen při změně počtu celých číslic; guard `FREQ_MAX_W`=720 px, frac 5→0 podle šířky). **Kadence statistiky = seq-driven** (`app_gpsdo_tick_stats_sample` vzorkuje jen na nové `g_freq_seq`; jinak by 1Hz tik nafoukl σy — jako web); **přechod REAL↔SIM i změna magnitudy resetují Allan/trend pyramidu** (nemíchat nekompatibilní vzorky). ⚠️ **`s_freq_center`/`s_freq_nominal_hz` už NEjsou fixně 10 MHz** — `screen_main_freq_hz()` = `s_freq_n / 10^frac` (nezávislé na centru). ⚠️ Nejistotní styl (SIGMA/FLOOR) na číslicích je zatím **CERTAIN všude** — reálná nejistota až #51. ⚠️ **τ0 pyramidy pořád předpokládá ~1 s** (plně správný τ0=skutečný rozestup = MathTask #27). Test: `fpgasim on <hz>` → headline; `fpgasim on 32768`/`1400000000` → přeformátování; `fpgasim fault lost` → šedá; `fpgasim off` → SIM marker.
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
- **BKP registry:** **DR0** = RTC sync magic; **DR1** = UI config (mode/chan/gate/run, magic `RTC_UICFG_MAGIC`, save `rtc_save_uicfg_if_dirty`); **DR2** = systémové nastavení (bity7:0 jas, bit8 mute, bit9 auto-dim en, bity10:15 auto-dim prodleva /15 s; magic `RTC_SYSCFG_MAGIC`); **DR3–DR5** = crash black-box (`RTC_CRASH_MAGIC`, hook → kind+jméno tasku, po přečtení smazáno); **DR6** = nastavení 2 (**bit0** = barevné schéma nejnižší bit + **bity9:10** = vyšší dva bity → `g_theme_idx` 0..4 = TMAVÉ/SVĚTLÉ/STŘEDNÍ/OBRYS/KONTRAST; staré záznamy mají bity9:10=0 → 0/1 = tmavé/světlé, zpětně kompat.; bit1 english, **bity2:6 časová zóna** kódovaná tz+13 → 1..27 = −12..+14 h, 0 = legacy záznam → UTC; bit7 AUTO CET/CEST, bit8 animace; `RTC_SYSCFG2_MAGIC`). Save `rtc_save_syscfg_if_dirty` (DR2+DR6). Načtení všech v `MX_RTC_Init` (před schedulerem), zápis výhradně defaultTask (kromě crash hooků).
- **Vlákno:** VEŠKERÝ přístup k RTC registrům je **výhradně z defaultTask** (`rtc_app_tick`). UART/UI čtou jen sdílené `g_rtc_text` ("YYYY-MM-DD HH:MM:SS") / `g_rtc_synced` (1=sync z GPS) — žádná cross-task HAL_RTC kolize. `g_rtc_text`/`g_rtc_synced` definované ve `freertos.c`, extern ve `freertos_shared.h`.
- **Zobrazení:** UART příkaz **`rtc`** (čas + sync stav). **Hlavní obrazovka** (header, `screen_main_redraw_time` + `render_header` date) čte RTC přes helper `rtc_time_date()` — **hodiny tikají plynule 1×/s i při ztrátě fixu** (dřív GPS-direct → mezi RMC stály a bez fixu zamrzly). Před prvním GPS syncem `--:--:--` / `no GPS`; v GPS okně nesynchronizovaný čas **ztlumený** (`UI_COLOR_INK_3`). GNSS/SAT pilulky zůstávají z GPS (odráží fix). **Diagnostika** (karta „System / RTOS / RTC") ukazuje RTC čas `HH:MM:SS` (ztlumený `no GPS` dokud nesrovnán).
- **Časová zóna (okno „Cas", s_view=22, dlaždice v Menu):** RTC běží VŽDY v UTC; zóna je jen zobrazovací posun. Dva režimy: **AUTO CET/CEST** (`g_tz_auto`, EU pravidlo letního času — CEST od poslední neděle března 01:00 UTC do poslední neděle října 01:00 UTC; `rtc_cest_active` = čistá funkce v rtc.c, Sakamoto den v týdnu) nebo **ruční posun** `g_tz_offset_h` −12..+14 h. `rtc_app_tick` z UTC odvozuje **`g_rtc_text_local`** (vč. přehoupnutí data, `rtc_apply_tz` + `rtc_month_days`) a **`g_tz_label`** („UTC"/„UTC+2"/„CET"/„CEST"). Okno Cas: živý UTC + lokální čas (~2×/s), tlačítko AUTO↔RUČNÍ, −/+ (v AUTO režimu první stisk −/+ přepne na ruční naseto z právě platného CET/CEST). **Lokální čas zobrazuje: hlavní obrazovka** (`rtc_time_date` čte local; label zóny na řádku data — change-key složený datum+label, clear fixní šířkou 150 px kvůli zkrácení labelu) **a screensaver**. **UTC zůstává: GPS okno, diagnostika, UART `rtc`.** Persist DR6: bity2:6 ruční posun, bit7 AUTO (viz výše). Kalendářní matematika + DST hranice kryté selftestem **`rtc_selftest`** (6. test).
- ⚠️ **defaultTask stack 256→384 words** kvůli `snprintf` v `rtc_app_tick` (historie: formátování v malém tasku už jednou přeteklo stack).
- ⚠️ **Při čtení RTC vždy `HAL_RTC_GetTime` PŘED `HAL_RTC_GetDate`** (čtení TR odemkne shadow registry, jinak se DR zasekne). **NEpovoluj RTC NVIC** (Alarm/WakeUp) v IOC — jen kalendář.

## Beeper (beeper.c/h) — PH9, 800 Hz + alarm vrstva (alarm.c/h)
Pasivní beeper na **PH9** (pin95). Tón **800 Hz** generuje **TIM7** přerušením @1600 Hz
(`HAL_TIM_PeriodElapsedCallback` v main.c → `beeper_isr_toggle()` přepíná PH9). `beeper_init`
(GPIO+TIM7+NVIC) voláno v main.c USER CODE 2. TIM7_IRQHandler v stm32h7xx_it.c USER CODE.
**Nepoužívej TIM7 jinde / nepovoluj v IOC.**
- **`beeper_tone(freq_hz)`** = libovolný tón (přepíše TIM7 ARR = 1 MHz/(2·freq)); `beeper_set(true)` resetuje ARR na 800 Hz (alarm). **`beeper_boot_melody()`** = vzestupný C-dur arpeggio G5→C6→E6→G6 („power-on" jingle ~0,5 s, blokující `osDelay`); volá UiTask jednou při startu, **jen když není mute** (respektuje `g_sound_muted` z BKP). Watchdog grace (8 s) blokující melodii kryje.
- **`alarm.c/h` = jediný volající `beeper_set()`.** `alarm_tick()` (defaultTask ~100 Hz) hlídá
  **hrany** tří stavů: FPGA `g_freq_stale` (SIGNAL_LOST/mrtvý link), GPS lock (`gps_get`:
  valid ∨ fix_mode≥2) a **limit pass/fail** (`g_meas_verdict`, #44 — jen když limity i alarm
  zapnuté). Ztráta signálu = 3 pípnutí, ztráta GPS locku = 2, **limit FAIL = 4**, obnovení = 1;
  počítadlo `g_alarm_limit_fail`. **Start tichý** (guardy `s_*_ever` — první link-up/první fix
  nepípne, bench bez antény taky ne; limit se armuje jen skutečným PASS, takže zapnutí limitu na
  už špatné hodnotě nepípne).
  Pattern neblokující (HAL_GetTick fáze), vyhodnocení stavů jen 5×/s (gps_get kopíruje ~200 B
  v kritické sekci), časování pípnutí 100×/s. **Mute** = `g_sound_muted` (okno Nastavení /
  UART `beep off`) umlčí okamžitě i test; prev-stavy se při mute dál aktualizují (po odmutení
  žádné pípnutí na starou hranu). UART: `beep`/`beep test`, `beep on`, `beep off`.
  **Od 2026-08-17 navíc `mon_eval()` = prahový monitor** (VBAT / OCXO pásmo / σy@1s — viz sekce
  níže): pattern **3× 200 ms** (pomalejší a delší než FPGA 80 ms a GPS 120 ms — „něco se pomalu
  kazí", ne „právě se ztratil signál"), počítadla `g_alarm_vbat/ocxo/adev`.

## Boot POST diagnostika (bootled.c/h) — LED_1 (PG3) + pípání (PH9)
Každý sledovaný init při startu si zapíše pořadové číslo (`bootled_step`); při zaseknutí
v `Error_Handler()` (nebo při selhání bring-upu panelu) **LED_1 blikne N× a SOUČASNĚ N× pípne**
(800 Hz, 150 ms svit+tón / 150 ms tma+ticho). Happy path = jen zápis do proměnné, žádné zdržení.
- **⚠️ Pípání NEPOUŽÍVÁ `beeper.c`.** Ten generuje tón přes **TIM7 IRQ**, jenže `bootled_fail()` běží
  z `Error_Handler()`, kde jsou **přerušení vypnutá** (přesně proto tenhle modul používá i **DWT**
  místo `HAL_Delay`) → TIM7 by nikdy netikl. Tón se proto **bit-banguje přímo na PH9** stejným DWT
  časováním (`BOOT_BEEP_HALF_US` 625 µs = 800 Hz, shodně s `beeper.c`). GPIO init je idempotentní,
  nezávislý na tom, jestli `beeper_init`/`MX_GPIO_Init` už proběhly.
- **Mute (`g_sound_muted`) se ZÁMĚRNĚ neuplatňuje** — není to UX zvuk, ale hlášení poruchy; při raném
  `Error_Handler` navíc nemusí být BKP s nastavením ještě načtená.
- Důvod: když selže panel, LED je jediný výstup — a v krabičce nemusí být vidět. Pípání projde i zavřeným přístrojem.
- ⚠️ Při selhání *před* `SystemClock_Config` je `SystemCoreClock` ještě default → délky (a tedy i výška
  tónu) budou mimo; vzor blikání/pípání zůstává čitelný. Platí to i pro původní blikání.

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
- **⚠️ Diagnostika „kdo se zasekl" (2026-07-20):** `watchdog_supervise` při odmítnutí refreshe zapíše
  do crash black-boxu (BKP_DR3..5, **kind 3**) jméno tasku se starým heartbeatem → po restartu
  `g_crash_text` = **`stall:UiTask` / `stall:FpgaTask` / `stall:BOTH`**. Bez toho byl prostý IWDG reset
  němý (RSR řekl jen „watchdog", ne který task). Zapisuje se **jen jednou za běh** (`s_stall_logged`).
- **⚠️ Blokující operace v taskech s heartbeatem ukrajují z 2,5 s limitu.** Nejhorší třída jsou
  **QSPI erase/write**: `w25q.c wait_ready()` čekal až 1000 ms (sector erase 50–400 ms) v **čistém
  spinu**. Volá ho defaultTask (syscfg auto-save, datalog) i UiTask (`calib_save`) — a protože
  defaultTask je Normal a UiTask BelowNormal, erase v defaultTasku UiTask **úplně vyhladověl**
  (heartbeat stárl) a defaultTask se sám nedostal na `watchdog_supervise`. Od 2026-07-20 `wait_ready`
  **ustupuje scheduleru** (`osDelay(1)` mezi dotazy, jen když scheduler běží). **Pravidlo: žádný spin
  delší než ~10 ms v defaultTask/UiTask/FpgaTask** — buď `osDelay`, nebo to přesuň do UartTasku
  (ten se nemonitoruje).
- **⚠️ IWDG2 = nezávislý watchdog CM4** (`CM4/Core/Src/iwdg2.c`, zrcadlo IWDG1): stejné parametry
  (~4 s, registrová sekvence, `HAL_IWDG` vypnutý). CM4 je bare-metal → **žádný heartbeat, prostý
  `iwdg2_kick()`** v každé iteraci hlavní smyčky; `iwdg2_init()` **až po beep melodii** (blokuje
  ~1,2 s), aby startup nespotřeboval timeout. Zaseknutá CM4 smyčka → IWDG2 reset CM4. DEBUG freeze =
  `__HAL_DBGMCU_FREEZE2_IWDG2()` (APB4FZ2). **🔴 ZMĚŘENO 2026-08-13: reset scope IWDG2 je SYSTEM-WIDE,
  ne per-core → IWDG2 je VYPNUTÝ.** Ověřeno: dočasná CM4, která přestala krmit watchdog, shodila
  **celý přístroj** (uptime CM7 cykloval po ~24 s). Zapnutý IWDG2 by tedy nechal zaseknutou CM4 (dnes
  dělá téměř nic) shodit i displej a měření — horší než CM4 samotná. **Zaseknutí se neztratí:** CM7 ho
  vidí přes heartbeat, loguje `stall:CM4` + `g_cm4_stall_count` (UART `status`). Až na CM4 poběží
  ETH/SCPI, dá se přidat cílený restart CM4 z CM7 (to už není nezávislý watchdog).

## Dvoujádro / IPC (CM7 ↔ CM4) — `ipc_shared.h`, `ipc.c`, `CM4/…/ipc_cm4.c`
✅ **CM4 BĚŽÍ A IPC ROUND-TRIP FUNGUJE — ověřeno na HW 2026-08-14** (`status` → `CM4: alive (IPC heartbeat), stall x0`;
header „4:xx%"). Obousměrný link je tím HW-ověřený: CM4 přijal snapshot (ověřil magic+verzi, jinak by mlčel)
**a** publikuje heartbeat zpět. Option bytes byly správně už z výroby (`BCM4=1`, `BOOT_CM4_ADD0=0x810`),
bank2 flashnutá.
- 🔴🔴 **PODMÍNKA: ŽÁDNÁ AKTIVNÍ LADICÍ SONDA. Testuj CM4 VÝHRADNĚ po čistém power-cyklu bez debug session.**
  Připojený debugger rozbíjí boot handshake CM7↔CM4 (HSEM/`D2CKRDY`) → boot gate CM7 vyprší →
  `g_cm4_absent=1` → „4:off" (vypadá to jako „CM4 nebootuje", ale CM4 je v pořádku). Táž sonda dělá
  **falešné HardFaulty** (`HF@24000000`, `HFSR=0x80000000` = DEBUGEVT: leftover flash breakpoint přes
  FPB). **Postup po každém flashi: Terminate debug session → Remove All Breakpoints → úplný power-cycle**
  (ne NRST). Teprve pak je `status` vypovídající. Platí i pro měření CPU (viz „CPU zátěž NEMĚŘ přes sondu").
- Návrh + revize `NAVRH_ARCHITEKTURA_CM7_CM4.md` §11; bring-up postup `DUALCORE_BRINGUP_CHECKLIST.md`
  (SRAM4 clock, .ioc regen pozor). ⚠️ Ta část o „nutnosti nastavit option bytes" je pro tuhle desku
  **bezpředmětná** — jsou správně (ověřeno čtením přes CubeProgrammer CLI).
- **Sdílená paměť = SRAM4 / D3 `0x38000000`, 64 KB** (`ipc_shared.h`, `g_ipc = *(volatile ipc_shared_t*)IPC_BASE` —
  **shodná adresa pro obě jádra**). Magic „IPC1" + verze + size (CM4 ověří po bootu). ⚠️ **MPU region 2 na
  CM7 = NON-CACHEABLE + SHAREABLE** (`main.c MPU_Config`); bez toho by CM7 cache viděla stará data. CM4 nemá
  D-cache ani MPU pro SRAM4 → ordering visí **jen na `__DMB()`** (shareability se neshoduje — viz bring-up §3).
- **Snapshot CM7→CM4** (seqlock, `seq` liché = zápis): `ipc.c` `ipc_publish` (**event-driven: na každé nové
  měření `seq_meas` NEBO ≥2 Hz heartbeat** — 2 Hz fixní by podvzorkoval FPGA ~4 měření/s; audit §11.9), **JEN reálná
  data** z `fpga_freq_get_last`/`gps_get`/`g_sensors`/`g_calib`/health; ⚠️ statistika sigma/offset/drift ZÁMĚRNĚ
  neplněná dokud #2 = simulace headline). **v2 (2026-08-09): PLNÁ sada instrument-state** — všechny teploty
  (OCXO/deska/MCU), napětí (12V/5V/VREF/VBAT/Vc), RF mV + **AD8307 kalibrace**, Si5356 stav, kanál →
  CM4 obslouží SCPI/web (`MEAS:VOLT?`/`SYST:TEMP?`/`MEAS:POW?`) **bez přístupu ke `g_sensors`/`g_calib`**
  (na CM4 nejsou). **v3 přidal Math/limit cfg mirror** (viz cmd ring níže). ⚠️ Rozšíření layoutu = **`IPC_VERSION`
  →6** (v2 senzory+kalibrace, v3 Math cfg, v4 `sens_valid` + `t_fpga_c100`, **v5 (F1, 2026-08-22) stav ETH
  linky/IP v `ipc_cm4_status_t`** — `net_ip`/`net_link`/`net_speed_mbps`/`net_duplex`, publikuje `ipc_cm4_set_net()`
  z CM4, čte `ipc_cm4_net()` na CM7 → `g_cm4_net_up`; CM4 dnes hlásí natvrdo
  down, reálné až s lwIP ve F5; **v6 (F3, 2026-08-22) `eth_phy_id` + `eth_init_ok`** — publikuje
  `ipc_cm4_set_eth()` z CM4 po `MX_ETH_Init`, čte `ipc_cm4_eth()` → `g_cm4_eth_ok`/`g_cm4_phy_id`;
  CM4 ověří magic+verzi+size
  po bootu, nesouhlas → IPC off; **v7** `scpi_selftest_ok`, **v8** `web_ctrl_en`, **v9** `httpd_selftest_ok`,
  **v10** `web_user`/`web_pass` (Basic Auth), **v11 (2026-08-23) `ui_cfg`** = NASTAVENÍ měření
  (brána/kanál/RUN) — bez něj neměl CM4 z čeho odpovědět na `SENS:FREQ:GATE?`/`CHAN?`, viz „slepý
  readback" níže; **v12 (2026-08-24) rozšíření webu** (viz „Web rozšíření v12" níže): snapshot
  přibyl o **alarmy/prahy/selftest** (#4), **GPS družice `gps_sats[24]`** (sky plot, #5) a přibyl
  **datalog transfer kanál** `ipc_datalog_xfer_t` (dlouhá historie 24h/7d/30d + CSV, #6).
  **⚠️ v12 MĚNÍ layout PŘED `cm4` blokem** (snapshot vzrostl) → gracefull detekce nesouladu bank
  (`cm4_ipc_version`) u tohoto bumpu NEFUNGUJE. **KÓD HOTOVÝ, NEOVĚŘENO NA HW.**).
  **⚠️⚠️ Při změně `IPC_VERSION` se MUSÍ přeflashnout OBĚ banky.**
  - **⚠️ Nesoulad bank je prakticky NEVIDITELNÝ — `4:--` to NENÍ.** CM4 při neshodě jen přestane
    přijímat snapshot (`s_ready=0`), ale **heartbeat volá dál a bez podmínky** → `ipc_cm4_alive()`
    (magic + růst heartbeatu) hlásí živou CM4 a header svítí `4:xx%`, jako by bylo vše v pořádku
    (jediný příznak: LED_2 nereaguje na GPS fix). **`cm4_ipc_version`** (razítkováno v každém
    heartbeatu, přežije samostatný reset CM7 + `memset` v `ipc_init`) to zviditelní: System Health
    `CM4:IPCv<x>!=<y>` červeně + UART `status` → `⚠ IPC NESOULAD`. ⚠️ **Detekce funguje jen dokud se
    nemění layout PŘED `cm4` blokem** (`snap`/`cmd`/`resp`) — pak si jádra přestanou rozumět už
    v adrese (týká se to bumpů, kde snapshot roste, jako v12 → nutno flashnout obě banky naslepo).
  - **Předletová pojistka:** `scripts/build.sh` varuje, když je některý obraz **starší než
    `ipc_shared.h`** (typicky „přeložil jsem jen jedno jádro") — ověřeno, že varování skutečně padne.
  - **System Health řádek CM4 (v6):** do boxu 156 px se při mono_16 vejde **15 znaků**, takže
    „CM4:OK NET:down" je přesně na doraz a **nic dalšího připojit nelze**. Ukazuje se proto vždy ten
    údaj, který v daném stavu něco říká: dokud link nejede (tj. do lwIP ve F5) je „NET:down" konstanta
    bez informace, kdežto **`CM4:OK PHY:C131`** je živý důkaz, že CM4 mluví s LAN8742A přes MDIO
    (= kritérium F3). Jakmile link naběhne, přebírá `NET:UP`; `ETH:--` = init na CM4 neprošel (amber).
    Totéž podrobněji v UART `status` → `ETH(CM4): init OK, PHY ID 0x0007C131 (LAN8742A)`.
- **`sens_valid` (v4, 2026-08-13):** maska platnosti hodnot ve snapshotu, **bitové pozice ZÁMĚRNĚ shodné
  se `SCPI_V_*`** → CM4 backend udělá `src->valid = snap.sens_valid` a chová se bit za bit jako CM7 na USB.
  Shodu hlídá 14 `_Static_assert` v `ipc.c` (+ 8 dalších pro `SCPI_CFG_*` vs `IPC_CFG_*`) — ty dvě hlavičky
  se jinak nepotkají v jedné translation unit, takže rozejití by nikdo nechytil. Do v3 se **neplatná napětí
  publikovala jako 0** (nerozeznatelné od skutečné nuly) a **neplatné teploty jako poslední dobrá hodnota**
  bez příznaku → `MEAS:VOLT?` by přes USB vrátilo `9.91E37` a přes TCP nulu = dvě různé pravdy o tomtéž
  přístroji. Hodnota se teď publikuje vždy (poslední dobrá se hodí pro trendy), ale **bez bitu se nesmí
  servírovat jako měření**. `t_fpga_c100` do v3 ve snapshotu vůbec nebylo → `SYST:TEMP? FPGA` nešlo na CM4 zodpovědět.
  Seqlock/ring helpery jsou **parametrizované ukazatelem** (`ipc_snap_wr/rd_*`,
  `ipc_ring_cmd/resp_*`) + `g_ipc`-vázané zkratky → `ipc_selftest` běží nad **lokální** instancí (žádný race s publisherem).
- **CM4 konzument** (`CM4/Core/Src/ipc_cm4.c`): `ipc_cm4_read` (seqlock, **bounded retry ≤8** — CM4 se nesmí
  zaseknout, + **per-read kontrola magicu** kvůli neseqlocknutému memsetu v `ipc_init` při bootu),
  **`ipc_cm4_cm7_alive(now_ms)`** (sleduje růst snapshot `seq` → **CM4 nedůvěřuje starým datům při zamrzlém CM7**,
  §11.4/§11.9 — jinak by servíroval stará data jako živá), `ipc_cm4_heartbeat` (živost → CM7). Zapojeno v CM4
  `main.c` smyčce; **LED_2 svítí při GPS fixu ze snapshotu** (jen když CM7 žije) = viditelný důkaz round-tripu.
  ⚠️ **`ipc_shared.h` sdílen RELATIVNÍM include** `"../../../CM7/Core/Inc/ipc_shared.h"` (CM4/CM7 sourozenci → regen-safe,
  žádná změna include path).
- **CM7 čte CM4 heartbeat:** defaultTask `ipc_cm4_alive()` → `g_cm4_alive` + `ipc_cm4_cpu_pct()` →
  `g_cm4_cpu_pct` → **CPU blok headeru** (2026-08-14): **`4:xx%`** = živý, s **reálnou zátěží CM4**
  (CM4 si ji měří sám přes DWT — busy/total cykly za ~1 s okno, clock-agnosticky, publikuje v heartbeatu;
  dnes ~0 %, protože CM4 skoro nic nedělá — naskočí s ETH/SCPI) / `4:--` (D2 ready, IPC ticho) /
  `4:off` (nenabootoval). **stall:CM4:** hrana alive→dead →
  log `stall:CM4` + `g_cm4_stall_count` (UART `status`). ⚠️ **CM7 se kvůli mrtvému CM4 NERESETUJE** (§11.4) a
  **NESAHÁ na crash black-box** (ten = příčina resetu CM7); CM4 se zotaví vlastním IWDG2.
- **Boot gate** (`main.c` Boot_Mode_Sequence_1/2, HSEM + `RCC_FLAG_D2CKRDY`): timeout → `g_cm4_absent=1`,
  **NEspadne do Error_Handler** (degradovaný běh, UART `[BOOT] CM4 nenabehl`). VTOR z auto-remapu
  (`USER_VECT_TAB_ADDRESS` zakomentovaný) → boot řídí **option bytes**.
- **D2 SRAM split** (linkery, regen-safe): **SRAM1 128K → CM7** (`RAM_D2 @0x30000000/128K`; CM7 do D2 nic
  nelinkuje, jen diagnostický `ram write/read`), **SRAM2 128K → CM4** (`RAM @0x10020000/128K`, CM4-alias)
  a **SRAM3 32K → ETH DMA** (`ETH_D2 @0x30040000/32K`, sekce `.eth_dma`).
  ⚠️ Kolize „obě jádra celý D2" byla latentní do ETH; teď disjunktní.
  - ⚠️ **CM4 `RAM` zkrácena 160K → 128K (2026-08-22, ETH F3).** SRAM3 se vyčlenila deskriptorům; kdyby
    `RAM` dál sahala přes SRAM3, linker by tam umístil `.bss`/`.data` a **tiše přepsal ETH deskriptory**
    (stejná fyzická paměť, jen jiná adresa → o kolizi neví). CM4 zabírá ~15 KB flash / ~4 KB RAM.
  - ⚠️ **`ETH_D2` je SYSTÉMOVÁ adresa `0x30040000`, ne CM4 alias `0x10040000`.** ETH DMA je AHB master
    a D2 SRAM vidí na `0x30xxxxxx`; na CM4-only alias nedosáhne. CM4 CPU na systémovou adresu dosáhne
    taky, takže stačí jedna adresa pro obě strany. Bez vlastní sekce skončily `.RxDescripSection`/
    `.TxDescripSection` z generovaného `eth.c` jako **orphan v `.data` na `0x10020010`** (a ještě se
    tahaly z flash). Kontrola: `nm H757_LED_CM4.elf | grep DscrTab` → musí být **`30040000 B`**.
    CM4 nemá D-cache → žádná cache maintenance kolem deskriptorů (výhoda oproti ETH na CM7).
- **Ethernet + lwIP na CM4 (F5, 2026-08-22 — KÓD HOTOVÝ, NEOVĚŘENO NA HW).** `NO_SYS=1`
  (bare-metal raw API, žádný RTOS na CM4) + **DHCP klient**. lwIP zaveden **ručně** z
  `STM32Cube_FW_H7_V1.13.0` (v `.ioc` LWIP **není**): `Middlewares/Third_Party/LwIP`
  (`src/core`, `core/ipv4`, `netif/ethernet.c`) + `Drivers/BSP/Components/lan8742`; glue
  `CM4/LWIP/Target/ethernetif.c` (adaptovaný ST `LwIP_HTTP_Server_Raw`) a
  `CM4/LWIP/App/lwip_app.c` (`lwip_app_init/process`). ⚠️ Jméno `lwip_app.c` (ne `lwip.c`)
  je zvolené tak, aby budoucí CubeMX „Generate Code" s LWIP nekolidoval.
  - 🔴🔴 **`LWIP_RAM_HEAP_POINTER` MUSÍ ZŮSTAT NEDEFINOVANÝ — nevracet pevnou adresu.**
    ST příklady mají `#define LWIP_RAM_HEAP_POINTER (0x30004000)`, protože na **jednojádrovém**
    H7 je D2 SRAM1 volná. Tady SRAM1 podle rozdělení D2 patří **CM7**, takže halda lwIP ležela
    v cizí paměti a cokoli, co do SRAM1 zapsalo (`membench` cíl „SRAM1 D2", UART `ram write`),
    **shodilo CM4 natrvalo** (IWDG2 je vypnutý). Bez toho define si lwIP alokuje `ram_heap`
    jako statické pole v `.bss` CM4 → SRAM2, tedy do vlastní paměti. Kontrola:
    `nm H757_LED_CM4.elf | grep ram_heap` → musí být **`1002xxxx`**, ne `3000xxxx`.
    Cena: `.bss` CM4 +14 kB (MEM_SIZE) → ~65 kB ze 128 kB, pořád velká rezerva.
  - ⚠️ **`ethernetif.c` NESMÍ duplikovat generovaný `eth.c`:** deskriptory, `EthHandle`
    i `HAL_ETH_MspInit` z ST příkladu jsou odstraněné, používá se `heth` a `low_level_init`
    ETH **znovu neinicializuje** (jen čte `heth.gState`) → `eth.c` zůstává netknutý regenerací.
  - ⚠️ **MAC se bere z `heth.Init.MACAddr`, ne z `ETH_MAC_ADDR*`** — HW filtr programuje
    `MX_ETH_Init` (00:80:E1:…), zatímco makra v `hal_conf` nesou 02:00:…; při rozejití by
    ARP odpovídal špatnou adresou a spojení by tiše nefungovalo.
  - ⚠️ **`ETH_RX_BUFFER_SIZE` musí = `heth.Init.RxBuffLen` (1536)** — ST příklad má 1000,
    DMA by psala za konec bufferu.
  - ⚠️ **Žádná cache maintenance** (`SCB_InvalidateDCache_by_Addr` odstraněno): CM4 nemá
    D-cache a CMSIS ji pro něj ani nedefinuje. Přesně proto ETH patří na CM4.
  - ⚠️ **Smyčka CM4 je rozdělená na rychlou a pomalou část.** Dřív končila `HAL_Delay(800)`
    → lwIP obsloužen 1×/800 ms, RX ring (4 deskriptory) by přetekl a ping měl RTT ~1 s.
    Teď: `lwip_app_process()` + `iwdg2_kick()` **každou iteraci (~1 ms)**, IPC snapshot +
    heartbeat + ETH stav na **5 Hz**, LED_2 stavovým automatem místo blokujících delayů.
  - **DHCP startuje/zastavuje link callback**, ne natvrdo — jinak by klient posílal DISCOVER
    do odpojeného kabelu a po zapojení čekal na svůj backoff (lwIP ho zvedá na desítky s).
  - **Paměť:** RAM 30 KB/128 KB (SRAM2), `.eth_dma` 12,7 KB/32 KB (SRAM3: deskriptory +
    zero-copy RX pool 8×1568 B). CM4 obraz 12,9 → **84,6 KB**.
  - ⚠️ **Statická IP zatím NEJDE** — okno SÍŤ (s_view=35) ji ukládá do syscfg, ale **IPC
    snapshot ta pole nenese**, takže se k nim CM4 nedostane. Vždy jede DHCP.
- **cmd ring (CM4→CM7) + config sync (v3):** `IPC_CMD_CFG_SET` (key `IPC_CFG_*` + `arg`/`double argd`) → CM7
  `ipc_service` aplikuje Math/limity na `g_meas_cfg` (`ipc_cfg_apply`; commit jen při reálné změně, kritická
  sekce). Čtení zpět = **cfg mirror ve snapshotu** (`math_m/b/null_ref/lim_lo/hi` + flagy) → CM4 obslouží `CALC:`
  readbacky + `CALC:DATA?/LIM?` bez `g_meas_cfg`. NULL:ACQ používá reálný FPGA kmitočet.
  ⚠️ `ipc_cmd_t` rozšířen o `key`+`double argd` (aby `lo/hi/m/b` nesly plný rozsah, ne jen uint32) → `IPC_VERSION` 3.
  Ostatní příkazy (GATE/RUN/CHAN/LOG) dozrají se SCPI/web na CM4 (dnes status=1).
- **SCPI je DATA-SOURCE nezávislé** (`scpi.c/h`, 2026-08-10): parser+handlery čtou z `scpi_src_t` (instrument-state
  + validity bity `SCPI_V_*` + akce `set_cfg`/`read_log`), NE z globálů. **CM7 backend** `scpi_src_load_cm7`
  (`#if CORE_CM7`) plní z `g_sensors`/`gps_get`/`fpga_freq`/`g_calib`/`g_meas_cfg`/datalog; `scpi_process` =
  wrapper (USB volající beze změny).
  - **SCPI/web na CM4 (W2–W5, plán W0–W5 dokončen; historie v git + `WEB_UI_PLAN.md`).**
    `scpi.c` + `meas_math.c` (fyzicky v `CM7/Core/Src/`) se linkují i do CM4 obrazu (explicitní
    `subdir.mk` v `CM4/Debug/SCPI/`, `-I CM7/Core/Inc`); běží tam SKUTEČNĚ, ne jen se překládají —
    CM4 pustí `scpi_selftest()`/`httpd_min_selftest()` za bootu a výsledek hlásí přes IPC (v7/v9)
    do UART `status` (CM4 nemá konzoli → IPC je jediný kanál na ověření, jako PHY ID u F3).
    ⚠️ **Na CM4 vrací `DISPlay:*` a `SYST:DATE/TIME` SCPI-99 `-241 "Hardware missing"`** (`#else`
    větev mimo `#if CORE_CM7`) — displej i RTC registry jsou fyzicky jen na CM7 a IPC cestu nemají
    ani mít nemají (vzhled/jas nejsou „přístroj", `WEB_UI_PLAN.md` 1.6). Bez té větve build CM4 spadl.
    - **Sdílený `ipc_scpi.c` (OBĚ jádra):** `ipc_scpi_src_from_snap()` (snapshot → `scpi_src_t`, čistá)
      + `ipc_scpi_set_cfg()` (SET → `IPC_CMD_CFG_SET` do cmd ringu). ⚠️ **Žádné `osDelay`/`HAL_Delay`
      v tomto souboru** (jádrově neutrální — nečeká na odpověď).
    - **TCP 5025 (`scpi_tcp.c`)** raw lwIP, pool 4 spojení (bez mallocu). ⚠️ `scpi_src_t` se plní
      **živě pro každý příkaz** (klient pošle druhý o minuty později), ne jednou při připojení.
    - **HTTP port 80 (`httpd_min.c`)** vlastní HTTP/1.1 (ne vendorovaný lwIP `httpd`/`fs.c` —
      `makefsdata` by přidal build krok, stejný důvod jako hand-rolled SCPI). ⚠️ **`GET /api/state`
      staví JSON přes `scpi_src_t`, NE přímo ze snapshotu** → validita (`SCPI_V_*`→`null`) je táž
      logika jako SCPI, ne druhá kopie. Čísla bez `%f` přes `fmt_scpi_hz_d` (sdíleno se `scpi.c`).
    - **W0 vypínač ovládání = `web_ctrl_en` (IPC v8):** `src.set_cfg = web_ctrl_en ? ipc_scpi_set_cfg : NULL`
      → zakázané ovládání spadne do **existující** NULL-guard ochrany parseru (`-230`), žádná nová cesta.
    - **Basic Auth (IPC v10, `web_user`/`web_pass`):** `POST /api/scpi` vyžaduje `web_ctrl_en` **A** platné
      jméno/heslo (`check_auth`, bajtové porovnání, prázdné heslo nikdy neprojde). ⚠️ **TCP 5025 Auth nemá**
      (VISA raw socket nezná HTTP hlavičky) → spoléhá jen na `web_ctrl_en`.
    - 🔴 **SPA (`SPA_HTML[]` v `.rodata`, `GET /`) — pravidla pro editaci** (aby zůstal C řetězcový literál
      bez build kroku): **žádná dvojitá uvozovka ani zpětné lomítko uvnitř** → v JS **žádné regexy**
      (oddělovač tisíců/`trim` ručně); atributy z `innerHTML` **bez uvozovek** (`class=cell`) → víchodnotový
      stav přes `data-` atributy (`data-st=bad`) + CSS selektor; interaktivita `addEventListener`+`data-`,
      ne `onclick=`. ⚠️ **Barvy křivek = CSS proměnné `--c0..--c3` nastavené TŘÍDOU** (`class='ln s0'`),
      ne literální `stroke=` (jinak se graf při změně palety nepřebarví). ⚠️ **`spec(kind)` = jeden zdroj
      pravdy** grafu (série+měřítko+formát) pro náhled i detail. 🔴 **Asynchronní odesílání** (`pump_send`
      + `tcp_sent`): tělo JSON/SCPI **vždy v connection-owned `c->bodybuf`**, NE ve sdíleném scratchi
      (souběžná spojení by si přepsala tělo); SPA stránka je `.rodata` konstanta (sdílet bezpečné).
    - **Grafy staví klient z vlastního pollingu** (buffer max 3600 vzorků = 1 h @1 Hz): kmitočet jako
      odchylka od průměru okna, napájení jako % od nominálu (jinak 12 V zploští zbytek). ⚠️ **Historie
      byla jen v prohlížeči** (F5 ji zahodí) — dlouhá historie z datalogu přibyla ve v12 (#6, níže).
    - ⚠️ **SVG:** `viewBox='0 0 100 100'` + `preserveAspectRatio='none'` (souřadnice = %), popisky os
      jako HTML overlay (ne `<text>`, roztáhl by se), body přes `setAttribute('points',…)`.
    - **`rf_dbm`, ALLAN, DRIFT počítá klient z REÁLNÝCH měření.** 🔴 `sigma_tau`/`offset`/`drift` ze
      snapshotu se **záměrně nepoužívají** (CM7 je neplní — zdroj je simulace headline, `ipc.c`);
      servírovat je jako měření by porušilo zlaté pravidlo. Vzorky se berou **podle `seq_meas`** (jinak
      by 1 Hz poll započítal tentýž výsledek víckrát → σy nesmyslně NÍZKÁ) a buffer se **zahodí při změně
      brány** (Allan chce rovnoměrné τ0). Overlapping ADEV z fází `x[k+1]=x[k]+y[k]·τ0`, τ0 = skutečný
      rozestup měření; drift = lineární proklad s korelací r (|r|<0,5 → neprůkazný, nekreslí se). ⚠️ **Pozor
      na rozdíl proti displeji:** headline na displeji je pořád simulace (STATUS #2), web servíruje reálná
      FPGA data → bez FPGA desky displej ukazuje kmitočet a **web správně `null`** (ne chyba webu).
      Když kmitočet chybí, SPA **vypíše důvod** (STOP / SPI DOWN / ztráta signálu) místo prázdna.
  - **Web rozšíření v12 (2026-08-24) — KÓD HOTOVÝ, NEOVĚŘENO NA HW; kompiluje se
    `-Wall -Wextra` bez varování na obou jádrech.** Šest bodů (#1–#6):
    - **#1 Ovládání MATH/LIMITY/NULL/CAS ze SPA** — nová karta pošle SCPI (`CALC:MATH:M/B/STAT`,
      `CALC:NULL:ACQ`, `CALC:LIM:LOW/UPP/STAT`, `SYST:DATE/TIME`) **týmž `POST /api/scpi`**, co
      konzole. **Žádný nový server kód** — reuse existujícího `set_cfg` (podmínka `web_ctrl_en`
      + Basic Auth). Tlačítka se zamknou při zakázaném ovládání jako segmenty.
    - **#2 mDNS `gpsdo.local`** — **ručně psaný responder v `lwip_app.c`** (vendorovaný lwIP
      `mdns` modul chybí, jen hlavičky). `LWIP_IGMP=1` (lwipopts), `NETIF_FLAG_IGMP` v
      `ethernetif.c`, MAC `PassAllMulticast` + `igmp_joingroup_netif(224.0.0.251)` při link UP,
      UDP 5353 → odpovídá na A-dotaz. ⚠️ **BEST-EFFORT** (jako `gps glonass`): závisí na tom, že
      MAC přijme multicast a IGMP join projde; selže-li cokoli, CM4 běží dál bez mDNS.
    - **#3 SSE push** (`GET /api/stream`) místo 1 Hz pollingu — **držené spojení**, `httpd_min_poll()`
      (z hlavní smyčky CM4, throttle ~20 Hz) posílá `data: <state json>` při novém `seq_meas`.
      ⚠️ **SPA má automatický fallback na 1 Hz poll** (EventSource `onerror`/timeout) → i kdyby
      SSE selhalo, dashboard jede. `HTTPD_MAX_CONN` 3→**5** kvůli drženým spojením.
    - **#4 Alarmy/prahy/selftest** ve `/api/state` (nová karta STAV) — počítadla `g_alarm_*`,
      stav prahů `g_mon_*_bad`, `g_selftest_res`, `sys_level`. Čteno **přímo ze snapshotu**
      (`scpi_src_t` je nemá).
    - **#5 GPS sky plot** (`GET /api/sats`, SPA fetch ~3 s) — polární graf z `snap.gps_sats[24]`.
      ⚠️ `ipc_sat_t` (v `ipc_shared.h`, bez `gps.h`) drží **shodný layout s `gps_sat_t`** — hlídá
      **7 `_Static_assert`** (`offsetof` každého pole + velikost + `IPC_GPS_MAX_SATS==GPS_MAX_SATS`)
      v `ipc.c`, publikace je pak `memcpy`.
    - **#6 Dlouhá historie 24h/7d/30d + CSV** (`GET /api/log?win=…&n=48`) — **datalog vlastní jen
      CM7** (W25Q), takže data tečou přes **nový IPC kanál `ipc_datalog_xfer_t`** (SRAM4, mimo
      snapshot): CM4 zapíše požadavek (from/count/step, decimace) + zvedne `req_gen`; CM7
      `ipc_datalog_service()` (defaultTask, blokující `datalog_read_back`) naplní `rec[]` a nastaví
      `resp_gen`. HTTP odpověď je **ODLOŽENÁ** — dokončí ji `httpd_min_poll` (mode `HCONN_LOG`,
      timeout 2 s). Jen **jeden transfer souběžně** (503 jinak). SPA přepíná graf mezi živým
      oknem (buffer prohlížeče) a datalogem přes `src()`; **CSV export** (tlačítko) uloží
      zobrazená data (bez zpětného lomítka v SPA → `String.fromCharCode(10)`). ⚠️ Datalog
      neukládá 12V/5V/VREF ani MCU/FPGA teploty → v dlouhých oknech ty řady chybí.
    ⚠️ **Rozsah snapshotu vzrostl** (alarmy + 144 B družic) → `HTTPD_BODYBUF_MAX` 1536→**4096**
    (JSON historie ~2,7 kB). Test: doplnit do `HW_OVERENI_PRUCHOD.md`.
- **Rozšíření 2026-08-13 (jen nad poli, která `scpi_src_t` UŽ má → CM7 i budoucí CM4 se chovají
  IDENTICKY, bez bumpu `IPC_VERSION`):** `SYST:CAP?`, **`SYST:ERR:ALL?`** (vyprázdní celou frontu
  jedním dotazem; ⚠️ po výpisu chyb **nepřipojuje** koncovou `0,"No error"` — ta se vrací jen
  u prázdné fronty), **`STAT:PRES`** (fakticky no-op, protože OPER/QUES *enable* registry nemáme —
  ale MUSÍ se přijmout, jinak inicializační `*RST;*CLS;STAT:PRES` z VISA/IVI ovladače skončí
  chybou a zaplní frontu), `SENS:FUNC?` → `"FREQ"`, `SENS:ROSC:SOUR?` → `INT`, **`SENS:ROSC:LOCK?`**
  (⚠️ hodnotí jen LOS_CLKIN bit3 + PLL_LOL bit4 — **bit2 LOS_XTAL je na této desce trvale 1**),
  **`MEAS:PER?`** (perioda = 1/f), `MEAS:FREQ:STAL?` (1 = měření nedůvěryhodné),
  **`SYST:TEMP:ALL?`** + **`MEAS:VOLT:ALL?`** (agregáty = 1 round-trip místo 4/5 — na TCP to bude znát).
  ⚠️ **Perioda má 15 des. míst (femtosekundy):** při 1,4 GHz je perioda 714 ps, takže i pikosekundový
  krok by byl 0,14 % — pro čítač nepoužitelné. Zlomek se tiskne **po dvou 32bitových půlkách**
  (`%07lu%08lu`), protože 15 cifer se do `unsigned long` nevejde a newlib-nano neumí `%llu`.
- **SET příkazy (2026-08-15) — SCPI přestalo být read-only.** Nově `SENS:FREQ:GATE <s>`
  (presety 0,1/1/10/100 s, tolerance 1 %), `SENS:FREQ:CHAN <0|1>`, **`INIT`/`INIT:IMM`**
  (RUN), **`ABOR`** (STOP), **`READ?`** (= INIT + FETCh, jednořádkové měření pro VISA/IVI)
  a `INIT:CONT?` readback. ⚠️ **Vláknový most:** stav měření (`st` v `screen_main.c`) vlastní
  UiTask, SCPI běží v UartTasku → SET zapíše jen **požadavek** (`g_ui_cfg_req` +
  `g_ui_cfg_req_pend`, kódování jako `g_ui_cfg`) a UiTask ho aplikuje v `app_gpsdo_tick_clock`
  přes `screen_main_apply_cfg_req()` (překreslí footer + zónu čísla, persist do BKP).
  Aplikace běží **před** `s_view` guardem, aby příkaz nezmizel, když je uživatel v jiném okně.
  ⚠️ `SENS:FREQ:GATE?`/`CHAN?` vracejí **nastavenou** hodnotu (SCPI set/readback kontrakt);
  skutečně změřené okno z FPGA rámce je nově na **`SENS:FREQ:GATE:ACTual?`**.
- **`SYST:DATE`/`SYST:TIME` (2026-08-15):** dotaz čte `g_rtc_text`, SET jde přes **request-most**
  (`g_rtc_set_*` + `g_rtc_set_pend`) — RTC registry vlastní výhradně defaultTask, který požadavek
  aplikuje v `rtc_app_tick` **před** `rtc_try_sync()`, takže při GPS fixu má poslední slovo GPS.
  ⚠️ Ručně zadaný čas **nenastavuje** `s_synced` ani BKP magic → UI dál správně hlásí „no GPS".
  Smysl to má jen bez antény.
  Klíče `SCPI_CFG_GATE/CHAN/RUN` mají protějšky `IPC_CFG_*` (hlídají `_Static_assert`);
  rozšíření výčtu nemění layout → `IPC_VERSION` beze změny.
  - ✅ **W1 HOTOVO (2026-08-23), ověřeno na HW přes web:** `ipc_ui_cfg_apply()` (v `ipc.c`, oddělená
    od čistého `ipc_cfg_apply` pro Math/limity) napojuje `IPC_CFG_GATE/CHAN/RUN` na **tentýž most**
    `g_ui_cfg_req`+`g_ui_cfg_req_pend` → `screen_main_apply_cfg_req()` v UiTasku, jaký používá SCPI
    přes USB — včetně identického bitového balení. RUN/STOP, brána i kanál z CM4 tedy fungují
    (`IPC_CMD_LOG` → `datalog_set_enabled`).
  - 🔴 **⚠️ PAST, na kterou se přišlo až HW testem přes web (2026-08-23) — SLEPÝ READBACK.**
    Zápis fungoval, ale **`SENS:FREQ:GATE?` / `CHAN?` přes TCP/HTTP vracely pořád `0.1` a `0`**,
    takže to navenek vypadalo jako „brána a vstup nejdou nastavit". Příčina: snapshot nesl jen
    `channel_id`/`gate_ns` = **co ohlásil FPGA rámec** (a při mrtvém SPI linku jsou nulové), ale
    **nenesl NASTAVENÍ** (`g_ui_cfg`). `ipc_scpi_src_from_snap` proto `set_gate_idx` neplnil vůbec
    (zůstal 0 z `memset`) a `set_chan` bral z `channel_id`. USB cesta byla přitom správně
    (`scpi_src_load_cm7` dekóduje tytéž bity z `g_ui_cfg`) → **dvě různé pravdy o tomtéž přístroji**,
    přesně to, čemu má `sens_valid` + `_Static_assert` disciplína bránit. Opraveno v **IPC v11**:
    snapshot má `ui_cfg` (bývalý `_pad_s` → velikost beze změny) a obě jádra ho dekódují stejně.
    **Poučení: readback musí číst NASTAVENÍ, ne poslední naměřenou hodnotu** — jinak se chyba
    projeví teprve tehdy, když měření neběží, a vypadá jako porucha zápisu.
- ⚠️ **`scpi_selftest` hlásí ŘÁDEK prvního neúspěšného assertu** (`scpi_selftest_fail_line()`, vypisuje
  ho `run_selftests` při FAILu). Je to **101 kontrol slitých do jedné návratové hodnoty** a na hostiteli
  se test spustit NEDÁ (v tomhle prostředí není nativní C kompilátor, jen arm-none-eabi, ani WSL).
  ⚠️ **`meas_math_capture_null()` referenci nejen zachytí, ale rovnou zapne relativní režim**
  (`null_en = 1`), takže po `CALC:NULL:ACQuire` Y klesne na nulu (ne na neposunutou hodnotu).

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
  `s_quad` z quad-enable; fallback Fast Read 0x0C 1-line); zápis/erase/registry 1-line.
- **UART:** `qspiid` (JEDEC ID, čeká EF4020), `qspitest` (init + destruktivní self-test sektoru 0),
  `qspispeed` (64 KB timed read + verify → KB/s), `storetest` (blob store self-test na CALIB regionu).

### Region mapa + storage vrstva (w25q_map.h, w25q_store.c/h) — HOTOVO/ověřeno
Deska je **generická** (použitelná i jinam) → regiony obecné, ne GPSDO-specifické. Zarovnané na 64 KB:
| Offset | Velikost | Region | Účel |
|---|---|---|---|
| `0x000000` | 64 KB | **CONFIG** | runtime nastavení (časté změny), wear-leveled store |
| `0x010000` | 64 KB | **CALIB** | kalibrace + zařízení param (zřídka), wear-leveled store |
| `0x020000` | 64 KB | **SETUP** | uložené sestavy (setup profily, `setup.c`, s_view=33), wear-leveled store — **přidáno 2026-08-01** |
| `0x030000` | ~63,8 MB | **DATA** | generický bulk / datalog (⚠️ base posunut z `0x020000` kvůli SETUP → datalog se jednou založí znovu) |

- **`w25q_store`** = generický **kruhový wear-leveled blob store** nad regionem: 1 blob (max **4080 B** = 1 sektor)
  na region, každý zápis jde do dalšího sektoru (round-robin → N× endurance), nejnovější platný `seq` vyhrává.
  **Power-safe:** payload se zapíše první, hlavička (magic+seq+CRC16) NAPOSLED → výpadek uprostřed zápisu =
  magic chybí = záznam neplatný, starý zůstává. API `w25q_store_init/read/write`. Nezná app obsah (jen bajty).
  Ověřeno `storetest` (write/read/CRC + rotace sektorů OK).
- **⚠️ Cap 4080 B/blob** (1 sektor). Stačí na kalib params; velký LUT → multi-sektor rozšíření nebo DATA region (viz [[revize-2026-07-03]]).
- **Nastavení = HYBRID BKP + W25Q flash (`syscfg.c/h`, CONFIG store) — persist i přes power-cycle.**
  BKP (DR1/DR2/DR6) přežije **jen warm reset** (bez VBAT ne power-cycle) → přidána druhá vrstva do
  **W25Q CONFIG store** (blob `{magic, brightness, mute, autodim_en/sec, theme, lang, tz_offset, tz_auto, ui_cfg,
  datalog_en, anim_en, fx_en, + Math/limity: meas_math/null/limit/alarm_en, m/b/null_ref/lo/hi doubles}`,
  survey_valid/n/lat/lon/alt/spread (výsledek self-survey), síť, prahový monitor, okno MĚŘENÍ,
  vzdálený přístup (`web_ctrl_en`/`web_user`/`web_pass`) a **rozložení hlavní obrazovky**
  (`layout_classic`, přidáno 2026-08-23) — **aktuální magic „SCFE"** (dřív SCF8/SCFD; každá změna
  layoutu blobu = nový magic → první boot po ní načte výchozí a při první změně uloží nově;
  bez BKP zálohy jsou fx/anim/Math/survey/rozložení aplikované vždy, ne jen při studeném startu).
  **`syscfg_load()`** (app_gpsdo_init, PŘED `ui_theme_select`+`screen_main_init` kvůli
  tématu/jasu při 1. renderu): při **studeném startu** (BKP smazána → `g_syscfg_bkp_valid=0`) je **flash autoritativní**;
  při **warm resetu** (`g_syscfg_bkp_valid=1`, nastaví MX_RTC_Init dle DR2 magicu) má přednost BKP a flash se jen
  inicializuje. **`syscfg_flash_tick()`** (defaultTask ~100 Hz): **debounced shadow-diff** — po ~1,5 s klidu od
  poslední změny jeden flash zápis (rychlé +/- tapy se sloučí, wear + blokování kritických tasků minimalizované).
  BKP zápisy (`rtc_save_syscfg_if_dirty`) zůstávají = instant cache pro warm reset. ⚠️ **Okno ztráty ≤1,5 s**:
  změna + power-cycle do 1,5 s (flash ještě nezapsán) se ztratí (BKP by ji měl, ale power-cycle ho smaže). ⚠️ **Dva
  QSPI writeři** teď za běhu (defaultTask syscfg auto-save + UiTask `calib_save` na ULOZIT) — prakticky se nepřekrývají
  (různá okna, debounce), mutex je TODO při přidání bulk/log QSPI.
- **AD8307 + ADS 12V/5V kalibrace → CALIB store HOTOVO** (`calib.c/h`, `g_calib`, okno Kalibrace):
  blob `{magic, ad8307_slope, ad8307_intercept, gain_12v, gain_5v}` (20 B, verzováno magicem
  `0x43414C31` „CAL1"), `calib_load()` volá `app_gpsdo_init()` (UiTask, jednou při startu),
  `calib_save()` jen na tlačítko ULOZIT (blokující erase+write, needěje se to periodicky).
  Prázdný/nevalidní záznam → vychozí (datasheet) hodnoty. ⚠️ Krátké okno při bootu (SensorsTask
  běží dřív než `app_gpsdo_init()`) čte 12V/5V default gain, ne uloženou kalibraci — kosmetické.
### Datalog (datalog.c/h, datalog_sd.c) — DATA region, HOTOVO 2026-07-20 (TODO #6)
**Append-only kruhový log stability** do W25Q **DATA** regionu. Záznam **32 B / 10 s** → ~270 kB/den
(8640 zázn./den). ⚠️ **Datalog ZÁMĚRNĚ využívá jen ~1/3 DATA regionu** (`W25Q_DATALOG_SIZE` ~22,3 MB
= 696 960 záznamů → **~80 dní** kruhu; pak se přepisuje nejstarší — log nikdy „nedojde"). Zbylé 2/3
DATA regionu zůstávají volné pro další bulk použití (fonty XIP, rekonstrukce Allan pyramidy). (Plný
region = ~242 dní; dřív tu chybně stálo „~600".)
- **Záznam (LE, ruční serializace — NE memcpy struktury, aby byl formát nezávislý na kompilátoru):**
  `seq(0)`, `t_unix(4)`, `freq_x100000(8)`, `t_ocxo_c100(16)`, `t_board_c100(18)`, `ocxo_vc_mv(20)`,
  `rf_mv(22)`, `flags(24)`, `sats(25)`, `hdop10(26)`, **`vbat(27)`**, **CRC16(28)** (CCITT-FALSE přes byte 0..27).
  - ⚠️ **`rf_mv(22)` jsou SYROVÉ mV, ne dBm** (přejmenováno z `rf_dbm10` 2026-08-18). Uložení
    v mV je záměr — kalibrace `g_calib.ad8307_*` se může změnit, syrová hodnota ne. Jenže staré
    jméno pole neodpovídalo obsahu a **oba konzumenti to vzali doslova**: CSV export i SCPI
    `MMEM:DATA?` dělily deseti a servírovaly výsledek jako dBm → 571 mV vyšlo jako **„57,1 dBm"**
    místo −61,2 dBm (nesmysl: 57 dBm = 500 W na vstupu čítače). Odhaleno testem přes UART
    2026-08-18. Opraveno: SCPI převádí přes kalibraci stejným vzorcem jako `MEAS:POW?`,
    CSV sloupec se jmenuje `rf_mV` a nese syrovou hodnotu.
  - **`vbat(27)` — záložní CR2032, přidáno 2026-08-17.** Prahový monitor (alarm.c) umí křiknout,
    až je baterie vybitá, ale degradace CR2032 trvá měsíce a bez záznamu nejde odhadnout, **kdy**
    ji vyměnit. Vešlo se do **jediného volného bajtu 27** (dřív `spare`, uvnitř CRC), takže formát
    zůstal 32 B a **žádná migrace nebyla potřeba**: kód `(mV−2000)/8` → 1..255 = 2008..4040 mV
    s krokem 8 mV (na baterii bohatě — sleduje se trend přes měsíce, ne mV; chyba ≤4 mV).
    ⚠️ **Kód 0 je vyhrazen jako „nezaznamenáno"** — díky tomu se starší záznamy (kde byl bajt 27
    vždy 0) nepřečtou jako mrtvá baterie 2,00 V, ale jako `DATALOG_INVALID16`. Kryje to selftest
    (round-trip vč. kvantizace, starý záznam s přepočteným CRC, neplatné čtení senzoru).
    Konzumenti: CSV export (sloupec `vbat_mV`, prázdná buňka u starých záznamů), `MMEM:DATA?`
    (SCPI NaN `9.91E37`), UART `datalog dump` (`Vbat=`) a **okno GRAFY** — dolní graf se **tapem
    přepíná OCXO Vc ↔ VBAT** (`TAP: Vc/VBAT`). Záměrně přepínač, ne dvě série v jedné ose: Vc je
    kolem 1,9 V a VBAT kolem 2,9 V, takže společný autoscale by obě křivky zmáčkl do ~30 % výšky.
  `DATALOG_F_*` = GPS_VALID/FIX_3D/FPGA_LINK/SIGNAL_LOST/DIV16/HOLDOVER.
  ⚠️ **RF se ukládá SYROVĚ v mV**, ne v dBm — kalibrace (`g_calib`) se může změnit, syrová hodnota ne.
- **Pozice zápisu se po bootu ODVODÍ ze `seq`** (`find_head`: blok s nejvyšším seq v 1. záznamu → v něm
  první volný slot; smazaná flash = `0xFFFFFFFF` = volno). **Žádná hlavička/metadata** → log přežije
  reset i výpadek napájení. Na hranici erase bloku se blok nejdřív smaže (= zahodí 128 nejstarších).
- **Vlákno:** `datalog_tick()` volá **výhradně defaultTask** (vedle `syscfg_flash_tick`), throttle 10 s
  uvnitř. QSPI přes `qspiMutexHandle` s **krátkým timeoutem (10 ms)** — při obsazené flash vzorek zahodí
  (`write_errors++`) a jede dál; defaultTask krmí watchdog a nesmí čekat.
- **⚠️ Úložiště je za abstrakcí `datalog_backend_t`** (probe/read/write/erase + `erase_size`/`capacity`).
  `datalog_init` zkusí **SD** a spadne na **W25Q** → až bude SD osazená, log se přepne bez zásahu volajících.
  Rozdíl NOR vs SD = jen `erase_size` (SD = 0 → `erase` smí být NULL). **512B RMW layer HOTOVÝ 2026-08-08**
  (generický `blk_io_t` + `blk_read`/`blk_write` = překlad byte-offsetu na 512B bloky vč. spanningu a
  read-modify-write částečných bloků). RMW je kryté `datalog_sd_selftest` (RAM fake blok, bez HW).

### SD karta (`datalog_sd.c`, SDMMC1) — SW HOTOVÝ 2026-08-11 (#28), ale **záměrně VYPNUTÝ**
**SDMMC1 je v `.ioc`** (4-bit, CM7): PC8–11 = D0–D3, PC12 = CK, PD2 = CMD (AF12), `ClockDiv=2`
→ **SDMMC_CK 16 MHz**. Detaily + past při regeneraci = `CUBEMX_CHECKLIST.md` sekce SDMMC1.
- 🔴🔴 **`HardwareFlowControl` MUSÍ být ENABLE** (v `.ioc` chyběl). Bez něj má `CLKCR` bit17
  `HWFC_EN=0` a na H7 SDMMC **datová cesta vůbec nejede**: příkazy (init/CID/CSD → `TRANSFER`) projdou,
  ale **blokový přenos nedostane ani bajt** (`DPSMACT` visí, `STA=0x1000`, `HAL_SD_ReadBlocks` SW
  timeout) — vypadá to jako HW vada DAT0, ale HW je v pořádku (ref. projekt `H757_SDcard_01/` HWFC má).
  **Fix regen-safe v `sd_export.c`:** `sd_apply_init_config()` nastaví `hsd1.Init.HardwareFlowControl`
  + `SDMMC_Init(SDMMC1, hsd1.Init)` po `HAL_SD_InitCard` **ručně zapíše CLKCR** — nutné, protože init
  skládáme sami a vynecháváme `HAL_SD_ConfigWideBusOperation` (ta má 49denní SCR smyčku → IWDG), kde by
  HAL transfer takt+HWFC jinak aplikoval. `HAL_SD_InitCard` sám nechá CLKCR na init hodinách (400 kHz).
  ⚠️ Doplnit HWFC i do `.ioc` přes CubeMX (viz CUBEMX_CHECKLIST). **`sd init` má krokovou diagnostiku
  `[a]..[e]`** (probe datové cesty přes CMDTRANS i DPSM_ENABLE) — první místo pro budoucí SD bring-up.
- **4-bit sběrnice (2026-08-14):** po identifikaci `sd_try_4bit()` = **CMD55 + ACMD6** (bezdatové příkazy)
  řekne kartě 4-bit, pak `SDMMC_Init` nastaví host `WIDBUS=4B`. ⚠️ **NE `HAL_SD_ConfigWideBusOperation`**
  (ta čte SCR neohraničenou datovou smyčkou → dřív IWDG). Fallback při chybě = zůstat 1-bit. **Rychlost
  zápisu** dál táhne hlavně program-time karty; pomáhá **`f_expand` předalokace** (`_USE_EXPAND=1` v ffconf
  — MUSÍ zůstat) a **32 KB bloky** (doporučená velikost, `SD_SPEED_BUF`). Vyšší takt/CK odpor/bulk kondík
  = HW TODO ve STATUS #69.
- **`sd_hal_rd`/`sd_hal_wr` hotové**, za `#ifdef HAL_SD_MODULE_ENABLED` (aktivují se samy se SDMMC1).
- **Card-detect = `PE3`** (`datalog_sd_card_present()`, debounced). Socket J13 má mechanický
  spínač DET_A(GND)–DET_B + 47k pull-up → **karta vložena = LOW**. Pin si `datalog_sd.c`
  konfiguruje **sám** (idempotentně, jako CS ve `fpga_freq_init`) → funguje i bez `.ioc`.
  Je **mimo** `#ifdef HAL_SD_MODULE_ENABLED` (čisté GPIO), takže UI hlásí přítomnost karty
  i při vypnutém SD backendu. **Bez karty se `HAL_SD_Init` ani nezkouší** — to dělá „běh
  bez karty" zadarmo. **Hot-removal:** `sd_still_there()` kontroluje pin před každým blokem,
  při vytažení shodí `s_sd_ok` + `HAL_SD_DeInit` → další zápisy selžou hned místo ~200 ms
  timeoutu (defaultTask nesmí čekat). Vytržení uprostřed zápisu je bezpečné — každý 32B
  záznam má CRC16, takže se poškozený při čtení přeskočí.

### Benchmark pamětí (`membench.c/h`, okno PAMETI s_view=43, UART `membench`) — 2026-08-23
Rychlost zápisu/čtení **a hlavně hledání chybných bitů** napříč všemi paměťmi. Vstup:
dlaždice **PAMETI >** v Nastavení (obsadila poslední volnou buňku mřížky 3×4) → tlačítko
**BENCHMARK**; nebo UART `membench` (vypíše tabulku).

⚠️ **Sloupec „testovano" je `testovaný blok / kapacita čipu`, ne kapacita.** Testovat jde
jen to, co nikdo nepoužívá, a ten rozdíl je často řádový — u SDRAM **4 MB z 32 MB** (zbytek
drží framebuffery a linker sekce `.sdram`), u W25Q **32 kB z 64 MB**. Původně sloupec ukazoval
jen testovaný blok pod hlavičkou „velikost" a četlo se to jako kapacita paměti (nahlášeno
při HW testu 2026-08-23) — proto se od té doby zobrazují **obě** čísla.

**Co se testuje a proč právě to** (6 cílů): **DTCM** `0x20000000` 64 kB ze 128 kB (linker sem
nic neumisťuje — ověřeno v mapfile; TCM se z principu necachuje), **AXI SRAM** vlastní statický
32 kB buffer z 512 kB (jediný cíl bez volné oblasti — je tam `.bss`/haldy), **SRAM1 D2**
`0x30001000` 64 kB ze 128 kB (CM7 do D2 nic nelinkuje; CM4 sedí v SRAM2 `0x30020000` a SRAM3
`0x30040000` — ověřeno v CM4 mapfile, žádný překryv), **SDRAM** `0xC0400000` **512 kB z 32 MB**
(scratch v MPU region 1, sdílený s UART `sdram write/read` a se `screenshot`), **interní FLASH
bank1** `0x08000000` 256 kB z 1 MB **jen čtení**, **W25Q QSPI** `W25Q_BENCH_BASE` 32 kB ze 64 MB.
- **SRAM4 / D3 se netestuje.** Do 2026-08-23 se testovala její „volná" horní půlka
  (`0x38008000`, nad `sizeof(ipc_shared_t)`). Odebrána proto, že SRAM4 je paměť, ve které **žije
  mezijaderné spojení**; hnát do ní sekundy provozu je proti pravidlu modulu („testuje se
  výhradně paměť, kterou nikdo jiný nepoužívá") — argument „horní půlka je volná" platí
  o **adresách**, ne o sběrnici. Přínos 16 kB je proti riziku nulový.
- 🔴🔴 **SRAM1 „volná" NEBYLA — poučení pro přidání dalšího cíle (vyřešeno HW 2026-08-23).**
  `membench` shazoval CM4, protože halda lwIP ležela na pevné adrese `0x30004000` (SRAM1 =
  CM7 podle rozdělení D2), zděděné z `LWIP_RAM_HEAP_POINTER` v `lwipopts.h`. Benchmark ji
  přepsal → CM4 spadla a s vypnutým IWDG2 už nenaběhla. Opraveno smazáním toho define
  (lwIP `ram_heap` teď v `.bss` CM4 = SRAM2). **Pravidlo: „linker sem nic neumisťuje" NENÍ důkaz
  volné paměti** — pevně zadrátovaná adresa v middlewaru se v mapfile neprojeví; při přidání cíle
  grepni i **absolutní adresy ve zdrojích obou jader**. ⚠️ Týž problém má i UART `ram write`
  (80 kB od `0x30001000`). ⚠️ **Přiřazení viníka:** `g_cm4_alive` má ~3 s okno → smrt CM4 spadne
  na zrovna běžící (poslední/nejdelší) cíl → mylně obviní W25Q. Viníka určuj čtením **syrového
  `g_ipc.cm4.heartbeat`** (5×/s) a označením **jen prvního** postiženého cíle.
- **`__HAL_RCC_C1_D2SRAM1_CLK_ENABLE()`, ne společná varianta** — na dvoujádrovém H7 má každé
  jádro vlastní sadu povolovacích bitů (`RCC_C1->AHB2ENR` vs `RCC->AHB2ENR`) a nás zajímá jen
  přidělení pro CM7. Sahat na společný registr, když zároveň řešíme padání CM4, je zbytečné
  riziko.
- ⚠️ **Během SDRAM fáze může displej krátce trhat** — LTDC čte framebuffer z téhož čipu přes
  tentýž FMC. Při 4 MB se obraz **rozsypal** (~46 MB/s potřebuje LTDC, test mu bral pásmo
  několik sekund) → blok zmenšen na 512 kB (~0,4 s) a ustupuje se scheduleru po 32 kB.
- ⚠️ **Interní FLASH se ZÁMĚRNĚ nikdy nezapisuje** — erase/write do banky, ze které se
  zároveň vykonává kód, zastaví sběrnici; druhá banka patří CM4. Místo bitových chyb se
  obraz přečte **dvakrát s invalidovanou cache** a součty se porovnají → to odhalí
  **nestabilní čtení**, ne trvale špatný bit (ten by dal pokaždé stejný chybný součet;
  trvalou vadu na H7 hlásí ECC flash sama).
- **Vzory** (`pat_word`, čistá funkce indexu — verify si hodnotu **dopočítá znovu**, takže
  se nikde nedrží kopie očekávaných dat v druhé, možná taky vadné paměti): `0x00`/`0xFF`
  (stuck-at bit), `55/AA` (zkrat mezi sousedními datovými linkami), **adresa v adrese**
  (chyba ADRESNÍCH linek — bez něj by se dva různě adresované, ale shodně zapsané bloky
  tvářily jako OK), PRNG (data-závislý crosstalk).
- 🔴 **Cache maintenance je tu otázka SPRÁVNOSTI, ne rychlosti.** Bez `clean` po zápisu by
  data zůstala v D-cache a do paměti se vůbec nedostala; bez `invalidate` před čtením by
  verify přečetl zpátky právě tu cache. **Vada paměti by se pak NIKDY neprojevila a
  benchmark by hlásil OK na rozbité RAM.** Dělá se jen u cacheable cílů (AXI/SRAM1/SDRAM);
  TCM a MPU non-cacheable D3 ji nepotřebují.
- **Maska chybných bitů (`err_bitmask`) je ta informace, kvůli které se to dělá** — ukáže
  rovnou na konkrétní datovou linku (např. samý bit 7 → jedna vadná dráha), spolu s adresou
  první neshody. UART výpis ji tiskne jen když nějaká chyba padne.
- 🔴 **Rozlišovací diagnostika (přidána 2026-08-23 po prvním HW běhu).** Souhrn „N chybných
  bitů" řekne, ŽE je něco špatně, ale ne CO — první běh vrátil 8,2 M chybných bitů v SDRAM
  a nešlo z toho poznat příčinu. Doplněno proto:
  - **`pat_err[]` = chyby po jednotlivých vzorech.** Selže-li **jen** vzor „adresa", jsou vadné
    ADRESNÍ linky, ne buňky; selžou-li všechny, jsou vadná data.
  - **`first_err_got`/`want` = skutečně přečteno vs. čekáno.** Náhodné bity = rozpad obsahu;
    cizí *platná* hodnota = překryv adres.
  - **`alias_off` = test adresních linek** (unikátní hodnota na každou mocninu dvou). Vzory dat
    vadnou adresaci **neodhalí** — dvě adresy mířící do téže buňky se tváří jako správný zápis.
  - **`retain_err` = retenční test, JEN pro SDRAM** (jediná DRAM v systému): zapiš, **počkej 1 s**,
    teprve pak ověř. Běžný „zapiš a hned ověř" test příliš pomalý refresh **nikdy neodhalí**,
    protože data se rozpadají až po čase. Clean cache PŘED čekáním (jinak by se měřila retence
    cache) a invalidate až PO něm.
- 🔴🔴 **NEUZAVŘENÝ NÁLEZ: ADRESY V SDRAM SE MOHOU OPAKOVAT PO 2 MB — intermitentní.** Zápis na
  `0xC0400000 + 2 MB` občas skončí na `0xC0400000` (dvě adresy = tatáž buňka). **Objevuje se a mizí
  mezi běhy** (vyloučeno, že jde o collateral padající CM4 — vrátilo se i při zdravé CM4) → ukazuje
  na **marginální/studený spoj**. Odpovídá nefunkční adresní lince **`FMC_A9` = `PF15`** (řádkový
  bit 9 ↔ `HADDR[21]`); firmware je v pořádku (`PF15` v `.ioc` i `HAL_GPIO_Init`), takže podezření
  míří na **HW (pájka `PF15`↔A9)**. **Prozvonit `PF15`**, až bude deska otevřená (neproměřeno,
  nepotvrzeno). Retence po 1 s = 0 chyb → refresh OK (⚠️ `REFRESH_COUNT` 1835 vs výpočet 371 pořád
  nesedí, `fmc.c`, ověřit zvlášť).
  - 🔴 **Nejdražší důsledek — `membench` ho PŘÍMO TESTUJE.** `FB2` (`0xC0200000`) se od `FB0`
    (`0xC0000000`) liší **právě jen v `HADDR[21]`** (ten podezřelý bit); totéž canvas pool vs `FB1`.
    Kdyby sdílely paměť, triple buffering by byl fakticky double (blikání/trhání). `fb_alias`
    (reverzibilní sonda, jen když je alias detekován) hlásí `!! FB0 … a FB2 … SDILEJI PAMET` nebo
    uklidňující `(framebuffery se navzajem NEprekryvaji)`. **Než sáhneš na cokoli v zobrazovacím
    řetězci, přečti si tenhle řádek.**
- 🔴 **Bezpečnostní pojistka (`sdram_safety_check`) — proto, že překryv existuje.** Když se adresy
  opakují, „testuju jen vyhrazenou oblast" **přestává platit** a zápis do scratche může skončit
  ve framebufferu. Staticky to zajistit nejde (závisí to na HW), takže se to **před každým během
  změří**: jednoslovnou **reverzibilní** sondou se ověří, že testovaný blok nesdílí buňku s FB0/
  FB1/FB2/canvas/`.sdram`; při kolizi se test **přeskočí** (`kolize s 0x…!`). Změřená vzdálenost
  překryvu blok zároveň **zkrátí**, aby nehlásil falešné chyby z překryvu sám se sebou.
- ⚠️ **Velikost SDRAM testu 4 MB → 512 kB:** při 4 MB se **rozbíjelo zobrazení**. Není to přepisem
  framebufferu (ten hlídá pojistka výše), ale **propustností FMC** — LTDC musí z téže SDRAM
  nepřetržitě číst ~46 MB/s a několikasekundový test mu bere pásmo → podtečení FIFO a rozsypaný
  obraz. 512 kB = ~0,4 s provozu místo ~3 s; navíc se ustupuje scheduleru po 32 kB.
- ⚠️ **Vlákna:** celý běh trvá jednotky sekund → smí běžet **jen v UartTasku** (jediný
  nehlídaný watchdogem; stejné omezení jako `sd_export_run`). UI tlačítko proto jen nastaví
  `g_membench_req` a `membench_service()` v UartTasku to vykoná; mezi vzory a cíli se
  ustupuje scheduleru (`osDelay(1)`), aby nevyhladověl UiTask (BelowNormal).
- ⚠️ **D2/D3 SRAM mají vlastní hodinový signál** (`RCC_AHB2ENR`), který CubeMX pro CM7 nikde
  nezapíná — `membench_run` volá `__HAL_RCC_D2SRAM1_CLK_ENABLE()` (idempotentní). Bez toho
  by celý region četl samé nuly a benchmark by hlásil „vadnou paměť" na zdravé desce.
- **W25Q scratch** (`W25Q_BENCH_BASE`, 8 sektorů = 32 kB) leží **za flight recorderem**, tedy
  ve volných 2/3 DATA regionu — nekoliduje s datalogem ani crash dumpem. Jen **2 vzory**
  (adresa + 55/AA): každý stojí celý erase cyklus (8× 50–400 ms), pět vzorů jako u RAM by
  test protáhlo na desítky sekund. ⚠️ NOR flash umí jen 1→0, takže **před každým vzorem musí
  být erase** — bez něj by druhý zápis dal součin obou vzorů a test by hlásil neexistující chyby.
- 🔴 **Past při návrhu okna (nalezeno HW testem 2026-08-23): karta si kreslí VLASTNÍ hlavičku
  na baseline `rect.y + UI_DIM_CARD_PAD_Y + 16` = `rect.y + 25`, přes celou šířku.** První
  verze měla záhlaví sloupců na baseline 86 při kartě od y=62 (hlavička tedy na 87) → popisek
  karty ležel **přesně přes** „pamet / velikost / zapis" a text se přepisoval. **Pravidlo: první
  vlastní řádek obsahu musí začínat aspoň ~30 px pod `rect.y` karty s hlavičkou**, nebo se
  hlavička nesmí použít. Svislý rozpočet okna PAMETI je proto rozepsaný v komentáři u
  `MEMB_ROW0` (karta 58..404 | hlavička karty 83 | záhlaví sloupců 116 | 7 řádků 146..326 |
  stavový řádek 356 | poznámka 384).
- ⚠️ **Řetězce ze snapshotu se čtou s `%.Ns`, ne holým `%s`** — `phase`/`msg` přepisuje UartTask
  přesně ve chvíli, kdy je UiTask kreslí; kdyby čtení trefilo okamžik mezi přepsáním starého
  terminátoru a zapsáním nového, holé `%s` by četlo za konec pole. Stejný důvod jako `strncpy`
  u `g_rtc_text`.
- ⚠️ **Jak číst naměřené rychlosti** (Debug/`-O0`; pro porovnání s katalogem stavěj Release `-Os`):
  pořadí zápis DTCM > AXI > SRAM1 D2, čtení stejné pořadí (DTCM zero-wait-state = nejrychlejší).
  Zápis > čtení je normální (store buffer je „fire-and-forget", load musí počkat). ⚠️ **Kdyby všechny
  vyšly shodně nebo DTCM pomaleji než AXI, měří se režie smyčky, ne paměť** (to byl stav před
  rozbalením). **`SPEED_UNROLL` (po 8) + jeden součet na čtveřici** (ne 8× samostatné `acc +=` — v
  `-O0` je každý příkaz round-trip na stack; **neměnit bez změření**). ⚠️ **SDRAM měř nejlepší
  z `SPEED_PASSES`=3** — sdílí FMC s LTDC (~46 MB/s), takže jednotlivý běh kolísá; rušení jen
  zpomaluje → minimum je blíž pravdě. ✅ Sanity: W25Q čtení ~4,46 MB/s sedí na strop pollovaného QSPI.
- **Rychlost se měří zvlášť od ověřování**: zápis + samostatný čtecí průchod (součet do
  `volatile`, jinak by GCC smyčku vyhodil jako mrtvý kód a „rychlost čtení" vyšla nesmyslně
  vysoká); porovnávání běží až potom, neměřené — jinak by číslo neslo rychlost porovnávací
  smyčky, ne paměti. RAM se měří přes **DWT CYCCNT**, QSPI přes `HAL_GetTick` (erase ve
  stovkách ms by CYCCNT při 480 MHz přetekl — wrap za ~8,9 s).

### SD export (`sd_export.c/h`) — mount/unmount + CSV
- **Dělba podle blokování (kritické):** `sd_export_tick()` je **levný** (čtení GPIO + případný
  rychlý `f_mount(NULL)`) → volá ho **defaultTask** ~2 Hz. `sd_export_mount()` a
  `sd_export_run()` **BLOKUJÍ** (`HAL_SD_Init` desítky–stovky ms, zápis souboru sekundy) →
  **VÝHRADNĚ z UartTasku**, který není hlídaný watchdogem.
- **Auto-UNmount ano, auto-mount NE.** Odmountování při vytažení je instantní (jen zahodí
  ukazatel), takže se vejde do defaultTasku. Mount je blokující → dělá se explicitně.
- **UART: `sd` / `sd mount` / `sd unmount` / `sd export [N]`** → `GPSDO.CSV` (oddělovač **`;`**
  kvůli českému Excelu; čas jako **unix sekundy**; kmitočet bez `%f` — celé Hz + 5 desetin).
  Zapisuje se **chronologicky** (nejstarší první), `datalog_read_back(0)` je nejnovější.
- ⚠️ **Tlačítko v UI zatím není** — běželo by v UiTasku (watchdog) a potřebovalo by worker;
  stejné omezení jako `calib_save()`.
- **FatFs zapnutý 2026-08-11** (CubeMX: FATFS → SD Card, `Detect_SDIO = PE3`). `BSP_SD_Init()`
  sám kontroluje přítomnost karty a pak volá `HAL_SD_Init` + 4-bit → karta se inicializuje
  **líně přes FatFs**, proto je `MX_SDMMC1_SD_Init()` vyřazená správně.
  ⚠️ **`FIL`/`DIR`/`FILINFO` NIKDY na stack tasku** — při `_FS_TINY=0` nese `FIL` vlastní 512B
  sektorový buffer (~560 B). Na stacku dělal ze `selftest_body` 1176 B a z `export_body` 920 B,
  takže po SD operacích klesla rezerva UartTasku na ~660 B (změřeno `status` 2026-08-15).
  Řešení = `static FIL` (funkce běží výhradně z UartTasku a nejsou vnořené) → `sd_export_selftest`
  spadl na **56 B**. RAM_D1 má ~386 KB volných, takže je to zadarmo.
  `_USE_LFN=0` → jen 8.3 jména (`GPSDO.CSV` vyhovuje); `_FS_TINY=0` → `FIL` má vlastní 512B
  buffer → **`sd_export_run()` má 808 B rámec** (UartTask 4 kB, základ ~1,3 kB → ~1,8 kB rezerva).
- 🔴🔴 **`sd_diskio.c`: `ENABLE_SD_DMA_CACHE_MAINTENANCE` = 0 a `ENABLE_SCRATCH_BUFFER` VYPNUTÝ**
  (oba v USER CODE, přežijí regen). Blokující `HAL_SD_ReadBlocks/WriteBlocks` na H7 **NEpoužívají
  IDMA** — přehazují data procesorem přes FIFO (`SDMMC_ReadFIFO()` ve smyčce). A protože
  `BSP_SD_ReadBlocks_DMA`/`WriteBlocks_DMA` jsou **přepsané na tuhle blokující variantu**
  (`sd_export.c`, `__weak` v `bsp_driver_sd.c`), neteče přes IDMA vůbec nic.
  - **Cache maintenance je pak u čtení PŘÍMO ŠKODLIVÁ**: data píše CPU (dirty v D-cache),
    `SCB_InvalidateDCache_by_Addr` je bez zápisu zpět zahodí → přečtou se nuly. Přesně proto
    `sd fs` hlásil „karta není naformátovaná" u karty, kterou `f_mount` v pořádku namountoval.
  - **Scratch („slow path", bere se pro každý buffer nezarovnaný na 32 B — což `FIL` na zásobníku
    je) má v ZÁPISOVÉ větvi dvě chyby ST**: `Invalidate` se volá *před* `memcpy` a `Clean` chybí
    úplně (na kartu jde starý obsah RAM), a čeká se na `READ_CPLT_MSG`, přestože zápis posílá
    `WRITE_CPLT_MSG`. Projev: `f_write`+`f_close` projdou, ale adresářová položka se nezapíše →
    `f_open(FA_READ)` vrátí **FR_NO_FILE**. Bez scratche jde všechno fast-path a chyby jsou mimo hru.
  - ⚠️ **Kdyby se někdy vracelo ke skutečnému IDMA, musí se zapnout OBOJE — a napřed opravit ty
    dvě chyby.** Pravidlo: **cache maintenance patří VÝHRADNĚ k `_DMA`/`_IT` variantám.**
- **`BSP_SD_GetCardState()` je přepsaná na „polite polling"** (`osDelay(1)`, když karta není ready).
  `sd_diskio.c` na ni čeká v několika **těsných smyčkách bez yieldu** s `SD_TIMEOUT` = **30 s**;
  bez toho by zaseklá karta na 30 s vyhladověla UiTask → mrtvý dotyk + IWDG. `SD_TIMEOUT` je mimo
  USER CODE (regen ho vrátí), tahle funkce je `__weak` → jde přepsat bezpečně.
- 🔴🔴 **`SDMMC1_IRQHandler` MUSÍ existovat a NVIC být povolený** (`stm32h7xx_it.c` USER CODE 1 +
  `HAL_NVIC_SetPriority(SDMMC1_IRQn, 5, 0)` v `BSP_SD_Init`). `sd_diskio.c` dělá **každý** přenos
  přes `BSP_SD_ReadBlocks_DMA` a čeká na zprávu z `SDQueueID`, kterou posílá **jedině** ta obsluha.
  Bez ní čekal každý `f_mount`/`f_read` celých `SD_TIMEOUT` = **30 s** v těsné smyčce
  `BSP_SD_GetCardState()` → UartTask vyhladověl UiTask (BelowNormal) → `watchdog_kick_ui()` se
  nevolal → **IWDG reset**. Navenek „`sd fs` zamrzne konzoli a restartuje desku" (2026-08-13).
  Priorita **5**, obsluha volá FreeRTOS API. Blokující cesta `datalog_sd.c` (FIFO polling) žádné
  přerušení nepovoluje, takže se s tím nebije.
- **⚠️ PŘEVZATO Z POSTUPU FRANTIŠKA (`C:\Claude_obecne\SD_franta.md`, rozchodil SD na TÉMŽE HW):**
  - **`BSP_SD_Init()` je přepsaný** v `sd_export.c` (generovaná verze je `__weak` → regen-safe).
    Důvod: `HAL_SD_Init()` na konci volá `HAL_SD_ConfigWideBusOperation(Init.BusWide)`, takže
    s `Init.BusWide = 4B` proběhne přepnutí **uvnitř identifikace** — a když selže, **spadne celá
    inicializace**, přestože karta v 1-bit režimu funguje. Proto identifikace běží **vždy 1-bit**
    a na 4 bity se přepíná až potom, s fallbackem. ⚠️ V `.ioc` musí `SD_4_bits_Wide_bus` ZŮSTAT —
    jen díky němu `HAL_SD_MspInit` nakonfiguruje piny D1–D3; liší se pouze runtime `Init.BusWide`.
  - **`hsd1.ErrorCode` je „sticky"** (HAL ho přiřazuje přes `|=`) a `ConfigWideBusOperation` na konci
    kontroluje jeho celý obsah → **před každým přepnutím se musí vynulovat**, jinak i úspěšný
    fallback vrátí zděděnou chybu.
  - **`disk.is_initialized[0] = 0` při unmountu** (`ff_gen_drv.c` si pamatuje, že `disk_initialize()`
    už proběhl, a podruhé ho nezavolá) — jinak mount bez karty skončí `FR_DISK_ERR` místo čistého
    `FR_NOT_READY`, tedy zavádějící diagnostikou.
  - **Deskový erratum:** externí pull-up, který měl být na **CMD**, je omylem na **CLK**. CMD (PD2)
    proto **musí mít interní pull-up** — v našem `.ioc` už `GPIO_PULLUP` je ✅. Není to open-drain,
    ale tři-stav při obratu směru: v mezeře `N_CR` linku nedrží nikdo a bez pull-upu plave.
  - ⚠️ **Nepoužívat `-fsyntax-only`** na rychlou kontrolu — zastaví překlad před middle-endem, takže
    celá třída varování (`-Wformat-truncation`, `-Wmaybe-uninitialized`, `-Wstringop-*`) vůbec nevznikne.
- ⚠️ **Dvě nezávislé detekce téhož pinu (PE3):** `datalog_sd_card_present()` (debounced, app API)
  a `BSP_SD_IsDetected()` (raw, používá ho FatFs uvnitř). Nekolidují — jen čtení, stejný pin.
- **✅ ROZHODNUTO 2026-08-11: W25Q je autoritativní úložiště, SD je JEN EXPORT** (`sd_export.c/h`).
  W25Q uveze ~600 dní, takže o data se nikdy nepřijde; SD slouží k tomu, k čemu má — vytáhnout
  a přečíst na PC. Tím odpadá **kontinuita `seq`** (log by se vytažením karty roztrhl mezi dvě
  média), **přepis MBR** i **ztráta dat při vyjmutí** (přeruší se jen export).
- **⚠️⚠️ `DATALOG_SD_RAW_OK` = 0 a takové zůstane** → `probe()` vrací false, SD backend je inertní.
  RAW blokový zápis by šel od offsetu 0 = **LBA 0 = MBR karty** a první zápis by ji pro PC
  zničil. Define zůstává jen jako nouzová varianta — **nezapínat**.
- **🔴 Boot bez karty NESMÍ zabít přístroj:** `MX_SDMMC1_SD_Init()` má na selhání `Error_Handler()`
  (= `bootled_fail()`, mrtvý přístroj), a `HAL_SD_Init` selže vždy bez vložené karty. Proto se
  v USER CODE **vyplní handle a hned `return`** — přeskočí se jen `HAL_SD_Init`, které pak dělá
  `BSP_SD_Init()` při mountu. Stejná filozofie jako `goto display_skip` u panelu a `g_cm4_absent` u CM4.
  ⚠️⚠️ **Holý `return;` NESTAČÍ:** `HAL_SD_MspInit()` začíná `if (sdHandle->Instance == SDMMC1)`,
  takže s `Instance == NULL` se **nezapnou hodiny SDMMC1 ani nenakonfigurují piny** (nutno vyplnit
  handle před returnem).
- **✅ SD hardware ověřen na HW (2026-08-12, přes GDB):** s obejitou detekcí vrátí `BSP_SD_Init()`
  `MSD_OK`, karta se ohlásí jako **SDHC 14,5 GB**, `CardState = TRANSFER`, `CLKCR = 0x4002`
  (4-bit, 16 MHz). **Tím padá podezření na STATUS #69** — SDMMC1 komunikuje spolehlivě.
- **✅ Card-detect PE3 FUNGUJE, polarita LOW = karta zasunuta** (vyřešeno mechanicky 2026-08-13 —
  chyba byla v kontaktech slotu, ne v detekci). **Zásada: když dva nezávislé zdroje (detekce PE3 +
  odezva karty `CMD_RSP_TIMEOUT`) říkají totéž, neobcházej je — ověř hardware.** `sd force on` /
  `sd det invert on` zůstávají jen jako nouzový únik.
- **UART příkazy SD:** `sd diag` (stavová diagnostika — rozliší selhání HW vrstvy vs. souborového
  systému), `sd test` (**zápis 8 kB + zpětné čtení a porovnání obsahu** → prověří multi-blokový
  přenos i cache maintenance), `sd det [invert on|off]`, `sd force [on|off]`, `sd mount`/`unmount`,
  `sd export [N]`.
- **Okno SD KARTA (`s_view=37`)** — dlaždice „SD KARTA >" v Nastavení (poslední, dřív volná buňka).
  Živý stav (karta/stav/FS/volné místo/**rychlost z posledního testu**/výsledek poslední akce) +
  tlačítka **PŘIPOJIT↔ODPOJIT** (label = AKCE, ne stav), **TEST**, **EXPORT CSV**, **FORMAT**.
  **TEST** = verify integrity (8 kB bit po bitu) **+ měření propustnosti** (512 kB zápis/čtení →
  KB/s, do `sd_ui_info_t.test_w_kbs/r_kbs` a řádku „Rychlost:"). **FORMAT = DVOJÍ potvrzení**
  (`s_sd_fmt_stage` 0→1→2, tlačítko „FORMAT"→„POTVRDIT 1/2"→„SMAZAT! 2/2" červeně; auto-zrušení
  po `SD_FMT_TIMEOUT_S`=6 s nebo stiskem jiného tlačítka) → `SD_REQ_FORMAT` → `f_mkfs("",FM_FAT32,…)`
  v `sd_export_service` (blokující, UartTask). ⚠️ **`_USE_MKFS=1` musí zůstat.** UART ekvivalent:
  `sd format yes yes` (taky dvojí potvrzení).
  ⚠️ **Tlačítka jen nastaví `g_sd_req`** — blokující práci dělá UartTask v `sd_export_service()`.
  Mount trvá stovky ms, test a export sekundy; v UiTasku by to shodilo watchdog. Kapacitu počítá
  taky UartTask (`f_getfree` umí u FAT16 projít celou FAT), UI čte hotový snapshot
  `sd_export_ui_info()`. Stejná filozofie jako `g_rtc_text` vs. přímé `HAL_RTC`.
- **`get_fattime()`** (`CM7/FATFS/App/fatfs.c` USER CODE) čte **`g_rtc_text_local`** → exportované
  soubory mají správné datum. ⚠️ **Nevolá `HAL_RTC`** — běží z UartTasku a RTC registry patří
  výhradně defaultTasku.
- ⚠️ **Souběh:** `sd_export_tick()` (defaultTask) při vytažení karty odmountovává, ale export/test
  běží v UartTasku → příznak **`s_busy`** auto-unmount po dobu blokující operace přeskočí.
  `_FS_REENTRANT=1` chrání operace nad svazkem, **deregistraci svazku ne**.
  **Příznak se nastavuje VÝHRADNĚ ve wrapperu** (`sd_export_selftest`/`sd_export_run`), tělo je
  vyčleněné do `selftest_body`/`export_body` → `s_busy = true; r = body(); s_busy = false;`.
  Důvod: první verze ho v `selftest` vůbec nenastavila a v `run` neuklidila na 3 chybových cestách
  (jeden neúspěšný export = **natrvalo vypnutý auto-unmount**). Přibývající `return` uvnitř těla
  o příznaku nic neví, takže se ta chyba nemůže vrátit. ⚠️ **Nový `return` patří do těla, ne do wrapperu.**
- **⚠️ IDMA + D-cache (nejspíš příčina STATUS #69 „init OK, karta se vidí, DMA nejede"):** IDMA
  **nedosáhne na DTCM** a AXI SRAM je **cacheable WB**. Proto `sd_hal_*` používá **statický bounce
  buffer v `.bss` (RAM_D1) zarovnaný na 32 B** + clean/invalidate, ne buffer volajícího —
  `blk_read`/`blk_write` mají `uint8_t blk[512]` na **nezarovnaném stacku** a cache maintenance
  nad ním by zasáhla sousední linky a poškodila okolní data.
- **Zap/vyp** = `datalog_set_enabled` (okno Datalog tlačítko ZAPNOUT/VYPNOUT, UART `datalog on|off`),
  **persist v syscfg blobu** (W25Q CONFIG). ⚠️ Kvůli novému poli se **magic zvedl `"SCFG"` → `"SCF2"`**
  → první boot po této změně nastavení nenačte (neznámý magic) a vrátí výchozí; při první změně se uloží nově.
- **Okno Datalog (s_view=17) je ŽIVÉ** (v `app_gpsdo_tick`): stav/backend, záznamů/kapacita, seq, chyby zápisu.
  Používá `kv_row_live` (na rozdíl od `kv_row` si **čistí box hodnoty** — jinak by kratší hodnota nechala ocas té delší).
- **UART `datalog [on|off|erase|dump]`** (`erase` = destruktivní smazání celého logu, `dump` = posledních 10 záznamů).
- **Selftest** (`datalog_selftest` = 7. z 13): round-trip pack/unpack, detekce poškozeného bajtu přes CRC,
  prázdný slot, kalendář→unix vektory. (8. = `meas_math_selftest`; 9. = `setup_selftest` = sanitizace
  slotu sestavy `slot_sanitize`; 10. = `autocal_selftest` = verdikt pásem `ac_verdict`.)
- **Plánované dál:** rekonstrukce Allanovy pyramidy z logu po bootu; export přes USB CDC; memory-mapped XIP.

## I2C1 — senzory na FPGA desce (i2c.c MX_I2C1_Init, ads1115.c/h)
Druhá I2C sběrnice **I2C1**: SCL=**PB8**, SDA=**PB9** (AF4, ~100 kHz, Timing 0x70303AEE jako I2C4).
`MX_I2C1_Init` je **self-contained v i2c.c USER CODE 1** (GPIO+clock tam, regen-safe) — voláno v main.c USER CODE 2 před schedulerem. Mutex `i2c1MutexHandle`.
- **TMP117** @ 0x49, 0x4A (čteno v SensorsTask 2×/s). **⚠️ 0x4A NENÍ osazený** → vrací NACK (rychlá chyba `sensor_fail`, červený `!` na diagu) — to je očekávané, NEodstraňovat (ať se připojí, až bude). 0x49 osazený.
- **ADS1115** @ 0x48 (4 single-ended kanály AIN0–3, 128 SPS, single-shot). Driver `ads1115_start(hi2c,ch,pga)`/`ads1115_read_raw`/`ads1115_raw_to_mv(raw,pga)` — **PGA je per kanál** (tabulka `k_ads_pga[4]` v `freertos_task_sensors.c`; zatím vše ±4.096 V pod `ADS1115_HW_DIVIDERS_REV2=0`, cílové ±2.048 V po úpravě děličů — viz `BOARD_V20_CHECKLIST.md §7`). **AIN0=OCXO_VC_Sense, AIN1=RF_Level (AD8307), AIN2 = 12V/VBUS větev přes dělič (×13417/2814), AIN3 = 5V větev (×4978/2526)** — přepočet v SensorsTask ukládá skutečné napětí větve.
- **⚠️ Stav senzorů = `g_sensors[SENS_COUNT]` (`sensor_stat.h`), NE staré skalární `g_temp*`/`g_ads_mv`.** Jednotná struktura pro 3× TMP117 + 4× ADS + **3× ADC3 (MCU jádro teplota, VDDA, VBAT)** = 10 senzorů: `last` (poslední hodnota, °C nebo mV), `min`/`max`/`mean` (statistika z platných vzorků, running mean), `samples`, `valid` (1=poslední čtení OK), `err_total`/`err_streak` (čítače chyb). Index = `sensor_id_t` (`SENS_T48/T49/T4A/ADS0..3/CORE_T/VDDA/VBAT`). **Zápis VÝHRADNĚ SensorsTask** přes `sensor_update(id,val)` (platný vzorek) / `sensor_fail(id)` (chyba I2C → `valid=0`, `last` drží poslední dobrou → matematika/log ignorují podle `valid`). **Bez zámku** (mění se ~2×/s, roztržené čtení tolerováno; přesná matematika patří dovnitř SensorsTask). `sensor_stat.h` je čistý C header (smí ho includovat i app/ vrstva). SensorsTask čte senzory **2×/s** (`vTaskDelayUntil` 500 ms); touch NENÍ v tomto tasku (viz UI vrstva).
- **⚠️ ADC3 = interní MCU senzory (teplota jádra / VREF / VBAT), `SENS_CORE_T/VDDA/VBAT`.** Čte SensorsTask (`adc3_read_chan`), zobrazení v diag „MCU" kartě + okně SENZORY + UART `sensors`/`adcraw`. **Rozchození bylo HW-tricky — 5 vrstev problémů (interní kanály railovaly na `0xFFFF`):**
  0. **🔑🔑 VREF+ není spojen s VDDA (HW desky) → reference budí vnitřní `VREFBUF`.** Pin 43 má jen `C15` 100n + dodaný **1 µF** (nutný pro stabilitu VREFBUF), s VDDA NEspojen (záměr — kvůli SMPS šumu). Bez reference VREF+ visel (~0,5 V) → VREFINT (1,22 V) i teplota saturovaly na `0xFFFF`. **Zapnutí VREFBUF v `main.c` USER CODE 2** (`HAL_SYSCFG_VREFBUF_VoltageScalingConfig(SCALE0 ≈ 2,5 V)` + `HighImpedanceConfig(DISABLE)` + `HAL_SYSCFG_EnableVREFBUF`). **⚠️🔑 KRITICKÉ: VREFBUF má VLASTNÍ clock `RCC_APB4ENR.VREFEN` (NE přes SYSCFG!) — bez `__HAL_RCC_VREF_CLK_ENABLE()` je celý `VREFBUF->CSR` MRTVÝ** (zápisy ignorovány, čtení vrací 0, ENVR „nedrží"). Tohle byla nejskrytější příčina — VREFBUF vypadal jako HW vada reference, ale chyběl jen ten clock enable. Ověření: `adcraw` → `VREFBUF CSR=0x09` (ENVR+VRR), `CCR≠0` (trim), `vref≈32000`. **Reference je ~2,5 V, NE VDDA 3,3 V** → label senzoru „VREF".
  1. **`PCSEL=0`** — na H7 musí mít každý kanál bit v `ADC3->PCSEL`, jinak je analogový vstup **odpojený** → railuje. `HAL_ADC_ConfigChannel` ho na této verzi NEnastavil → zapínáme ručně (`ADC3->PCSEL |= (1<<kanál)`, kanál z `SQR1` rank1; ADEN musí být 0).
  2. **`ClockPrescaler` CubeMX vynechal** z `MX_ADC3_Init` (i když `.ioc` má DIV8) → ADC běžel na 25 MHz + špatný BOOST. SensorsTask init nastaví `ADC_CLOCK_ASYNC_DIV8` (→3,125 MHz) + `HAL_ADC_Init`.
  3. **Kalibrace jsou 16-bit** (ne 12-bit jak tvrdí HAL komentář; VREFINT_CAL=24291). LL makra `__LL_ADC_CALC_*` u VREFINT dělí 12-bit → špatně. Počítáme ručně 16-bit: **`vref(+) = VREFINT_CAL×3300/vref_data`** (reference-agnostické → vrací skutečné VREF+, tj. ~2500 z VREFBUF), `Temp = (ts×vref/3300 − TS_CAL1)×80/(TS_CAL2−TS_CAL1)+30` (**korekce `×vref/3300`** protože TS_CAL je @ 3,3 V ale VREF+ je 2,5 V), `VBAT = vbat×vref/65535×4` (vnitřní dělič /4). **„VDDA" senzor teď měří VREF+ (~2,5 V), label = „VREF".**
  4. **Single-channel režim** (ScanConvMode=DISABLE, NbrOf=1, čteno po jednom) — scan polling 3 kanálů byl nespolehlivý. VBAT = pin 8 = záložní CR2032 (BT1) → VBAT senzor monitoruje tu baterii.
  Vše regen-safe v `freertos_task_sensors.c` + `main.c` USER CODE 2 (ne v generovaném `adc.c`). Diagnostika: UART `adcraw` (raw + spočítané). Detail viz `CUBEMX_CHECKLIST.md`.
- **Ošetření chyb senzorů:** při selhání I2C čtení (HAL chyba / mutex nezískán) → `sensor_fail`: `valid=0`, hodnota se NEpřepisuje (žádný sentinel, neotráví průměry). **Log** přes UART jen na PŘECHODU stavu (první chyba po OK / obnovení) — žádný 1 Hz spam. **Displej** (diagnostika): neplatná hodnota se kreslí ztlumeně (`UI_COLOR_INK_3`) + malý červený `!` vlevo. **I2C4 MÁ recovery** (`i2c4_recover` v `freertos_task_ui.c`, od 2026-07-12, commit 16df3f8): při ≥8 po sobě jdoucích HAL selháních čtení touch (FT5x06) udělá 9 SCL pulzů (PH11) + **re-init BEZ `MX_I2C4_Init`** (jen `HAL_I2C_Init` inline) — stejný bezpečný vzor jako `i2c1_recover` níže, takže se **NEvolá** ta nebezpečná cesta popsaná v incidentu výše (ta byla o špatné 400 kHz timing hodnotě v `MX_I2C4_Init`, ne o téhle recovery technice). Rate-limit max 1×/5 s, pod `i2c4MutexHandle`. Log `touch: I2C4 nereaguje (N chyb) -> bus recovery` = normální/očekávané chování při zaseknuté sběrnici (ATTINY drží SDA po nešťastné transakci), ne chyba. **I2C1 MÁ recovery** (`i2c1_recover` v SensorsTask): při „wedge" chybě (BERR/ARLO/TIMEOUT — slave drží SDA) udělá 9 SCL pulzů (PB8) + **re-init BEZ `MX_I2C1_Init`** → jeden vadný/zaseknutý čip neshodí zbytek (ADS). Spouští se max 2×/cyklus, JEN na wedge, NE na NACK (absentní 0x4A re-init nezpůsobí). I2C1 nemá ATTINY → reset bezpečný. **⚠️ Recovery NESMÍ volat `MX_I2C1_Init`** — ta má `Error_Handler()` (nekonečná smyčka) při selhání `HAL_I2C_Init`; při ODPOJENÉM/plovoucím busu (pull-upy jsou na FPGA desce!) re-init selže → **zamrzl by CELÝ program** (zjištěno 2026-06-25). Recovery proto dělá GPIO AF + `HAL_I2C_Init` inline a selhání ignoruje (zkusí příští cyklus). **Recovery běží pod `i2c1MutexHandle`** (nekoliduje s UART `si5356`/`scan1`). **Back-off (`i2c1_backoff_ms`):** při trvalém selhání celé I2C1 (mrtvý bus) se polling zpomaluje **3×@500 ms → 3×@1 s → 2×@2 s → @10 s** (šetří CPU); jakékoli HAL_OK → reset na normál (NACK absentního 0x4A se nepočítá → při připojeném kabelu back-off nevznikne). TMP117 0x48 (I2C4) čte dál 2×/s, gate je jen na I2C1.
- **Si5356A** @ 0x70 (clock generator) → `si5356.c/h`, `si5356_init(&hi2c1)` v main.c USER CODE 2 (po `MX_I2C1_Init`, před schedulerem → bez mutexu). Aplikuje **ClockBuilder Pro register map** (`REGMAP[]`, oficiální CBPro „C Code Header" export) přes paging (reg 255 page0/page1) + SiLabs apply proceduru (OEB_ALL off → E2 pulse → SOFT_RESET 0xF6 → OEB_ALL on). Status reg 218 (0xDA), **bitová mapa OVĚŘENA z AN565** (dřívější „bit2=LOS_CLKIN" byla chyba): bit0 SYS_CAL, **bit2 LOS_XTAL** (krystal XA/XB — na desce NEOSAZEN, piny uzemněné → bit TRVALE 1, benigní, ignoruje se), **bit3 LOS_CLKIN** (skutečná ztráta 10 MHz na pinu 4), bit4 PLL_LOL. ⚠️ **PLL_LOL se při fyzické ztrátě vstupu NEasertuje** (AN565: LOL = rozdíl >5000 ppm na PFD, ne odpojený vstup) → ztrátu reference hlásí právě bit3. UART `si5356` = re-init + status.
- **⚠️ KONFIGURACE: 4× 100 MHz, fáze 0/90/180/270° (= 0/2,5/5/7,5 ns), Vstup 10 MHz → VCO 2,2 GHz (N=220) → /22.** Ty 4 fázově posunuté hodiny jsou **reference pro 4-fázový vernier TDC ve FPGA** (reciproký čítač, jemný krok 2,5 ns). **MUSÍ být 90°, NE 45°** — při 45° jemné kódy nesednou na 2,5 ns mřížku TDC → systematická chyba kmitočtu. Fáze v reg (LSB=Tvco/128=3,551 ps): CLK1 r111/112=704=2,5 ns, CLK2 r115/116=1408=5,0 ns, CLK3 r119/120=2112=7,5 ns. **Přesnost čítače = přesnost těch 100 MHz (= ppm 10 MHz vstupu Si5356).**
- **Při změně konfigurace: v CBPro nastav fáze (90° krok!), exportuj „C Code Header" a nahraď `REGMAP[]`** (formát {addr,val,mask} — adresy DECIMÁLNĚ pro 1:1 diff s exportem). **Vynech řádky s `mask==0x00`** (CBPro „do not write"); `mask<0xFF` → read-modify-write, `mask 0xFF` → přímý zápis (`wr_masked` to respektuje, `mask 0` přeskočí).
- **UART `scan1`** = I2C scan na I2C1 (s popisky zařízení). `scanner` zůstává pro I2C4.
- **Diagnostická obrazovka** (`app_gpsdo_render_diag`, tlačítko MENU → ZPĚT): **dvousloupcový layout** (`DG_*` makra v `app_gpsdo.c`). **Levý sloupec:** Teploty — **labely dle umístění, ne adres** (pořadí: „STM board" = TMP117 0x48, „MCU jadro" = ADC3 CORE_T, „OCXO" = 0x49, „FPGA board" = 0x4A), všechny `last` + `min/max`; Napětí — „OCXO_VC" (AIN0, ladicí napětí), „RF_Level" (AIN1), AIN2 (12V), AIN3 (5V), VREF + VBAT. **Pravý sloupec:** *Komunikace + měření FPGA* (`g_spi_text` zeleně/červeně + `g_freq_info` = gate/PH/SEQ), *Reference Si5356* (lock z `g_si5356_status`: LOCK OK / LOS CLKIN! / PLL UNLOCK! / CALIB…), *System / RTOS / RTC* (heap free/min, CPU %, uptime, **RTC čas HH:MM:SS** z `g_rtc_text` — ztlumený + `no GPS` dokud nesrovnán z GPS). Neplatný senzor = ztlumeně + červený `!`. Refresh ~2×/s z `app_gpsdo_tick` (UiTask), tearing-free přes `prim_stm32_present`.
  - ⚠️ **Šířka `dval`/`dtext` boxu MUSÍ nechat rezervu za sousedním textem** (min. ~20 px) — box se před vykreslením čistí (`fill_rect` na pozadí karty), takže když box zasahuje do oblasti sousedního labelu, KAŽDÝ živý redraw kus labelu smaže (viz historie 2026-07-18: min/max box teplot začínal na `DG_LLBL+96`, což bylo uvnitř labelů „STM board"/„FPGA board" — text se při každé aktualizaci ořezával). Boxy v teplotním řádku teď: label do ~140 px, min/max `DG_LLBL+140` šířka 100, hodnota `DG_LVAL` šířka 100 — žádný box nezasahuje do souseda.
  - **Datové zdroje:** Si5356 status čte SensorsTask (`si5356_read_status`, reg 218) do `g_si5356_status`/`g_si5356_ok`; RTOS zdraví počítá UiTask (`xPortGetFreeHeapSize`, idle-delta CPU %) do `g_rtos_*`/`g_uptime_s`.
  - **Diagnostika = technický hub.** Footer: **DIAGRAM** (blokové schéma, s_view=21) | **PAMET** (s_view=5) | **SELFTEST** (s_view=20) | ZPĚT. Do Diagnostiky se dá i z **System Health** (tlačítko DIAGNOSTIKA) a z Menu dlaždice.
  - **Tlačítko DIAGRAM** → **Komunikace: blokové schéma** (`app_gpsdo_render_commdiag`, s_view=21). Uzly: GPS NEO-7M, SENZORY, STM32H757, Si5356A, FPGA GW1NR-9. **Grafika (přepracováno 2026-07-18):** uzel = zaoblený rámeček, výplň BG_0, **obrys 2 px v barvě stavu + stavová „LED" tečka** vpravo nahoře (stav čitelný bez čtení popisků); STM32 má akcentní barvu (= „my", ne stavový). **Čárkovaný skupinový rámeček „FPGA deska"** (`cd_group`, fieldset styl — popisek přerušuje rámeček) kolem Si5356+FPGA. Popisky spojů na **„pilulce"** (`cd_label_chip`) — **šířka se přizpůsobí textu** (`prim_text_width`, žádný prázdný blok), výplň BG_0 překryje čáru pod textem → popisek sedí přímo NA spoji a zůstává čitelný. Spoje = pravoúhlé trasy (`cd_path`) barvené stavem: GPS→STM32 (UART/1PPS), SENZORY↔STM32 (I2C1/I2C4, `i2c_health()`), STM32→FPGA (SPI2, `g_spi_ok`), OCXO→Si5356 (CLKIN, `SI5356_LOS_CLKIN`), Si5356→FPGA (4×100 MHz), RF vstup→FPGA (`g_freq_stale`). Celý diagram se překresluje najednou při změně `cd_state_key()`.
    ⚠️ **Šířka uzlů = text (mono_16, 10 px/znak) + ~18 px padding/stranu** (historie: uzly měly 90-130 px prázdné rezervy). **Postranní popisky OCXO/RF** mají šířku ověřenou proti reálné šířce textu — „OCXO 10MHz" @ sans_16 potřebuje ~104 px, dřívější box 90 px ho **ořezával**.
- **Okna** (`s_view`: 0=main, 1=diag, 2=gps, 3=health, 4=senzory, 5=pamět, 6=histogram, 7=nastavení, 8=screensaver, 9=trend-fullscreen, 10=o-přístroji, 11=boot-splash, 12=menu-rozcestník, 13=confirm-restart, 14=reference, 15=kalibrace, **16=holdover, 17=datalog, 18=alarmy, 19=čítač, 20=selftest, 21=komunikace (blokové schéma), 22=čas (zóna), 23=allan-fullscreen, 24=animace/demo, 25=příklady animací (smyčka), 26=spektrogram Δf, 27=EFEKTY (přepínače), 28=status ribbon demo, 29=grafy (časový průběh senzorů), 30=přehled kanálů (horizontální bargrafy), 31=math/limity, 32=self-survey, 33=sestavy (uložit/načíst), 34=měření (prezentace: perioda/jednotky/statistika/TFOM, #67), 35=síť/ETH, 36=displej (jas+auto-dim+vzhled), 37=SD karta, 38=kvalita GPS (historie sats/HDOP z datalogu), 39=prahy (meze monitoru), 40=průvodce kalibrací, 41=analýza (nejistota/drift/tempco), 42=přístup (jméno/heslo pro web+SCPI), 43=paměti (benchmark RAM/FLASH)**).

### Prahový monitor reálných veličin (alarm.c, okno PRAHY s_view=39) — 2026-08-17
Do 2026-08-17 hlídal `alarm.c` jen **události** (FPGA link, GPS lock, limit pass/fail) — z deseti
senzorů, které měří 24/7 reálná data, **ani jeden**. Doplněny tři prahové podmínky nad skutečně
měřenými veličinami: **VBAT pod mezí** (záložní CR2032 pro RTC/BKP — degradace, o které se jinak
nedozvíš, dokud tiše nepřijdeš o čas a nastavení při odpojení napájení), **OCXO mimo teplotní pásmo**
(rozladěná pec) a **σy@1s nad prahem**.
- **Konfigurace `mon_cfg_t`** (`g_mon_cfg` v alarm.h) + persist v syscfg blobu (magic **„SCFA"→„SCFB"**,
  tedy jeden boot s výchozím nastavením). Výchozí: **VBAT 2,80 V** ZAP (CR2032 s **3,3 V nominálem** na
  této desce; syscfg magic „SCFE"→„SCFF" 2026-08-24, ✅ ověřeno HW 2026-08-25), OCXO 45–55 °C ZAP.
  Displejové nominály VBAT (GRAFY vert. bar + PŘEHLED KANÁLŮ + web) = 3,3 V.
- ⚠️ **`adev_en` je výchozí VYPNUTÝ** — σy@1s se dnes počítá ze **simulace** headline (~1e-8), takže
  jakýkoli realistický práh by pípal na šum. Mechanika je hotová a správná; zapnout až po #2.
- **Hystereze je povinná** (`band_eval`): VBAT ±30 mV, OCXO ±0,5 °C, ADEV ±10 %. Bez ní by veličina
  sedící přesně na prahu překlápěla stav při každém vyhodnocení (5×/s) a pípala donekonečna.
- **Start tichý** (`s_*_ever`): alarm se ozve až po jednom dobrém čtení, takže trvale vybitá baterie
  nepípá při každém bootu — na displeji ji vidíš (SYS pilulka + okno PRAHY) tak jako tak.
- **Vyhodnocení běží i při mute** (`mon_eval` je před mute větví): `g_mon_*_bad` čte SYS pilulka a okna,
  takže musí odrážet skutečnost; mute umlčuje **jen pípnutí**.
- **σy@1s se předává přes globál `g_adev_1s`** (plní `app_gpsdo_tick_stats_sample` ze
  `screen_main_adev_1s()`) — Core vrstva nesmí sahat do `app/screens/`, stejný most jako `g_meas_verdict`.
- **SYS pilulka**: všechny tři jsou **AMBER** (degradace). RED zůstává vyhrazená ztrátě reference
  a selftest FAILu, tedy stavům, kdy přístroj buď neměří, nebo měří špatně a neví o tom.

### Okno KVALITA GPS (s_view=38) — historie příjmu z datalogu
Vstup tlačítkem **KVALITA** v okně GPS. Odpovídá na otázku, kterou živý pohled na družice nezodpoví:
*„je moje anténa dobře umístěná?"* — graf **počtu družic** a **HDOP** v čase + souhrn (% času s 3D fixem,
průměr/min družic, průměr/max HDOP). Zdroj = datalog (`sats`, `hdop10`, `flags`), tedy **reálná data**
(kmitočet v logu je zatím 0 ⬅ #2, ale GPS pole jsou plná od začátku → okno je užitečné hned).
Presety 1 h / 6 h / 1 den / 7 dní. ⚠️ **Renderuje se jen při vstupu a při změně presetu** — jeden render
dělá stovky blokujících `datalog_read_back`; periodické obnovování by v UiTasku porušilo pravidlo
„žádný spin > 10 ms" (stejný vzor jako okno GRAFY). `hdop10 == 255` je sentinel „neplatné" a nesmí
se dostat do statistiky jako HDOP 25,5.

### Průvodce kalibrací napětí (s_view=40)
Vstup tlačítkem **PRŮVODCE** v okně Kalibrace. Řeší to, co ruční krokování gainu neřeší: *„přístroj
ukazuje 4,827 V — je to skutečné napětí, nebo je vedle dělič?"* Uživatel vybere větev (12V/5V), navolí
hodnotu změřenou multimetrem a firmware dopočítá gain. **POUŽÍT** ho nastaví živě (řádek „Přístroj
měření" se hned srovná), **ULOŽIT** persistuje do W25Q CALIB.
- Matematika: `sensor_update` ukládá už **přenásobenou** hodnotu, takže syrovou hodnotu ADS znát
  nepotřebujeme: **`nový_gain = starý_gain × (skutečné / zobrazené)`**. Převod je tím nezávislý na
  aktuálně nastaveném gainu.
- Guardy: nic se neměří (< 100 mV) → nedělíme; výsledný gain mimo `KALIB_ROWS` rozsah → nenabídne se.

### ⚠️ Oprava navigace (2026-08-17)
`goto_view` neznal `case 2` / `15` / `18`, přestože `nav_push(2)` se používal už dřív (GPS → SURVEY).
Spadlo to do `default` → **ZPĚT ze Self-survey vedlo na hlavní obrazovku místo zpátky do GPS okna**.
Doplněno; nová okna 38/39/40 by tu chybu jinak zdědila.
- **Okno GRAFY (s_view=29, STATUS.md #31)** — časový průběh teplot + napájení. Vstup: tlačítko **GRAFY** ve **footeru System Health** (footer proto přeskládán na 4 tlačítka: SENZORY/DIAGNOSTIKA/NASTAVENI/GRAFY nestejných šířek + BACK). Layout: vlevo dva grafy (nahoře **Teploty** — 4 série STM/MCU/OCXO/FPGA se *sdílenou* osou + legenda s aktuální hodnotou; dole **OCXO Vc** — jedna série, autoscale + overlay rozsah/okno), vpravo 5 **vertikálních bargrafů** aktuálních napájecích větví (12V/5V/REF/BAT/Vc) s **nominálním markerem** (#32). **Kombinovaný zdroj:** krátká okna (≤1 h) z **RAM decimační pyramidy** (`sensor_hist.c/h` — per-senzor, idiom `trend_feed`, plněná ze SensorsTasku 2Hz plného sweepu přes `sensor_hist_feed`, base 2 s, 4 stage ×4 → ~3,4 h, ~15 kB RAM_D1), dlouhá okna (6 h/1 den/7 dní) z **datalogu** (W25Q — jen veličiny, které datalog ukládá: OCXO+deska teplota, OCXO Vc; ostatní série se u dlouhých oken v grafu vynechají, legenda je ztlumí). Footer presety −/+ (3 min…7 dní). ⚠️ **Datalog okna se renderují JEN při vstupu/změně presetu** (klíč stabilní per preset), ne periodicky — jeden render dělá stovky blokujících `datalog_read_back` a periodické obnovování by v UiTasku porušilo pravidlo „žádný spin >10 ms" (dlouhá historie je stejně minuty/hodiny stará). RAM okna se hýbou 1×/s (`g_uptime_s`). ⚠️ Headline/statistiky jsou pořád simulace, ale tohle okno kreslí **reálné senzory** (teploty/napájení jsou skutečná HW data).
- **Okno PREHLED KANALU (s_view=30, STATUS.md #47)** — **sesterské** okno ke GRAFY (přepínač ve footeru obou, tlačítka `PREHLED >` / `< GRAFY`, přepíná **bez `nav_push`** → BACK z obou vede tam, odkud byla dvojice otevřena = System Health). Zatímco GRAFY ukazuje **časový průběh**, tohle je **aktuální stav všech kanálů jako horizontální bargrafy** s nominálním markerem + číselnou hodnotou vpravo → odchylka od očekávané hodnoty na první pohled. Dvě karty: **Teploty** (STM/MCU/OCXO/FPGA board, rozsah 0–70/90 °C, bez nominálu) + **Napájení + RF** (12V/5V/REF/VBAT/OCXO Vc s nominálem a ok/warn barvením dle ±15 % pásma, + **RF v dBm** přes `g_calib` AD8307 slope/intercept). Data **reálná** (`g_sensors[]`), refresh z `app_gpsdo_tick` s **per-řádek change-detect** (pct + **min/max** + text; flip jen při změně řádku). Bary kreslí lokální `hbar_row_draw` (ne `ui_bargraph`) — **segmentovaný** track (`HB_SEGS=45` × ~10 px, `hb_seg`/`hb_marker_idx`) s **barevnými markery** (2026-08-06): **AKT** = vyplněný úsek (zelená/amber dle ±15 % pásma) = aktuální hodnota, **REF** = accent segment na nominálu, **MIN** = violet segment na `s->min`, **MAX** = červený segment na `s->max` (peak-hold, z `g_sensors[].min/max`). Markery se kreslí PŘES výplň (viditelné i uvnitř zelené). Legenda barev `hbar_legend()` vpravo od nadpisu (kreslí se jednou). Přepočet syrové hodnoty na jednotku sdílí `hbar_disp`/`hbar_pct_disp` (RF přes `g_calib`). Popis řádků v `HBAR[]`. ⚠️ Jako u GRAFY: senzory jsou skutečná HW data, headline zůstává simulace; geometrie počítaná (tabulky fontů) — vzhled ověřit na displeji.
- **Okno MATH / LIMITY (s_view=31, STATUS.md #43/#44)** — dlaždice **„Math/Limity"** v Menu (dřív Placeholder 2). **#43 Math Mx+B + NULL/relativní:** transformace `Y = M·X + B`, pak (při NULL) `Y −= null_ref` (relativní režim, „null then measure"). **#44 limit pass/fail:** meze `lo/hi` nad výslednou Y → verdikt PASS / FAIL LO / FAIL HI. Pure-logic jádro `meas_math.c/h` (Core, `g_meas_cfg` + `g_meas_verdict`, selftest 8/8). UI: karta A (X měřeno, Y výsledek, tlačítka MATH/M-cyklus/B±/NULL), karta B (verdikt badge, Lo/Hi, PÁSMO ±, LIMITY/ALARM přepínače). **Limity se nastavují jako pásmo kolem aktuální Y** (`math_recenter_limits`: lo/hi = Y ± preset pásmo; recenter při změně math/null/pásma). **⚠️ Průběžné vyhodnocení běží i mimo okno** (`app_gpsdo_tick_stats_sample` → `g_meas_verdict`), takže **alarm hlídá limity na pozadí**: `alarm.c` beepne na hranu PASS→FAIL (4 pípnutí) / FAIL→PASS (1), armuje se jen skutečným PASS (zapnutí limitu na už špatné hodnotě nepípne), respektuje mute; počítadlo `g_alarm_limit_fail`. ⚠️ **Aplikuje se na `screen_main_freq_hz()` = DNES SIMULACE headline** → plný smysl po reálném SPI linku (#2). **Persist v syscfg flash blobu** (`g_meas_cfg` = math/null/limit/alarm flagy + m/b/null_ref/lo/hi jako doubles; magic `"SCF6"`→`"SCF7"`) — NE v BKP → `syscfg_load` aplikuje vždy (jako fx/anim), UI preset indexy (M, pásmo) se dopočtou z cfg při otevření okna (`math_sync_idx`). Formátování Hz bez `%f` (`fmt_hz`, integer extrakce, nano.specs).
- **Okno SELF-SURVEY (s_view=32, STATUS.md #53)** — tlačítko **SURVEY** v GPS okně (footer pravý sloupec). Firmwarové **Welford průměrování** lat/lon/alt z platných fixů → **horizontální rozptyl [m]** = konvergence (klesá s N); START/STOP. `survey_accumulate` běží na pozadí v `app_gpsdo_tick` (i mimo okno). START pošle i **UBX-CFG-TMODE2** survey-in (`gps_survey_in_cmd`) — ⚠️ best-effort, účinné jen na timing-grade RX (NEO-7M nejspíš NAKne = neškodné); firmwarové jádro je HW-nezávislé. **Persist:** STOP uloží výsledek (mean lat/lon/alt + rozptyl + N) do `g_survey_*` → syscfg flash (magic „SCF8") → přežije power-cycle; okno při otevření ukáže poslední uloženou polohu (dokud se nespustí nový survey).
- **Okno SESTAVY (s_view=33, STATUS.md #54)** — tlačítko **SESTAVY >** ve footeru Nastavení. **8 číslovaných slotů** (uložit/načíst profil nastavení: jas/téma/jazyk/zvuk/auto-dim/zóna/UI cfg/efekty/animace + Math+limity) do W25Q **SETUP regionu** (`setup.c/h`). Všech 8 slotů = **JEDEN blob** přes `w25q_store` (wear-leveled, CRC, power-safe). Slot −/+, ULOŽIT/NAČÍST/SMAZAT; NAČÍST aplikuje i téma/jas (`ui_theme_select`+`screen_main_invalidate`+`init`). `setup_init()` v `app_gpsdo_init` (po `calib_load`).
- **Warm-up / stabilizace OCXO (STATUS.md #52)** — `warmup_ready()` (v okně Holdover): „hotovo" = uptime ≥ 300 s **A** tepelný sklon `|dT/dt| < 0,08 °C/min` (z RAM historie 0x49 přes `sensor_hist`). Nahradilo naivní `uptime<180` ve stavu **WARMUP**; OCXO řádek ukazuje `45.2 C +0.03/m` (fialově dokud náběh).
- **Autokalibrace / self-check (`autocal.c/h`, STATUS.md #68, ROZPRACOVÁNO)** — UART `autocal` + tlačítko **AUTO-CAL** v okně Kalibrace (výsledek do status řádku). Dnes **verifikace (guard-band)** z `g_sensors`: VREF ~2,5 V, 12V/5V ±5 %, VBAT → PASS/WARN/FAIL (nejhorší = celkový verdikt). **Staged** (jasně označené, zatím se NEprovádějí — nemění koeficienty): ADC3 HW self-cal (⬅ ADC3 vlastní SensorsTask → nutný koordinovaný reinit), timebase vs GPS (⬅ #2), RF slope/intercept (⬅ externí RF reference = manuální řádky Kalibrace). Bezpečné — **nic nezapisuje do HW**.
- **Bargraf — vertikální mód (`ui_bargraph.c/h`, #32):** `ui_bargraph_t` má nově `vertical` (0=horizontální default, 1=vertikální — segmenty zdola nahoru, jednotná barva `color`, text nekreslí — umísťuje volající), `segs` (0=default 20; jinak N = jemnější granularita) a `nominal_pct` (≤0=žádný; jinak segment na té hodnotě zvýrazněn accentem → odchylka od očekávané hodnoty vidět na první pohled). **Horizontální fast-path `ui_bargraph_update` (main screen RF bar, anim demo) beze změny** (fixních 20 segmentů).
- **Grafické efekty — sada 2026-07-24, řízená bitmaskou `g_fx_enabled` (`fx_flags.h`, bezzávislostní header sdílený firmware+app).** 6 vizuálních efektů (dřív 7 — `FX_HEAD_GLOW` odstraněn 2026-07-26 na přání uživatele, `FX_ALL`=0x3F; syscfg maskuje `& FX_ALL` při load i save → zbytkový bit ze starého blobu se sám ztratí, bez magic bumpu), každý samostatně ZAP/VYP přes okno **EFEKTY** (`s_view=27`, footer tlačítko „EFEKTY >" v okně Animace vedle „PŘÍKLADY"): tlačítka `UI_BUTTON_RUN`(zelená=ZAP)/`UI_BUTTON_STOP`(červená=VYP), tap přepne bit. **Persist JEN v syscfg flash blobu** (magic `"SCF4"`→`"SCF5"`) — NE v BKP (DR6 nemá pod magicem dost volných bitů), proto `syscfg_load` čte flash i při warm resetu a `g_fx_enabled` aplikuje vždy. Default vše ZAP. Bity: **`FX_WATERFALL`** (spektrogram, s_view=26), **`FX_ALLAN_CONF`** (konfidenční pás ADEV ~1/√(2·párů), `allan_band_fill`), **`FX_HOLD_CONE`** (holdover drift kužel, `holdover_cone_draw` — šířka/barva dle stavu LOCK/HOLDOVER/WARMUP/NO-LOCK), **`FX_OCXO_GAUGE`** (analogový budík Vc=AIN0 v Holdoveru, `ocxo_gauge_draw` půlkruh + barevné zóny), **`FX_SPARK_FILL`** (area-chart výplň pod trend sparkline, `spark_fill_below` v libui/sparkline.c), **`FX_SYS_XFADE`** (plynulé prolínání barvy SYS pilulky, `screen_main_tick_sys_xfade` @20 Hz — barvy přes `ui_pill_variant_colors` + `override_style` v ui_pill_t). ⚠️ Efekty přes `prim_internal_blend_px` (glow/arc/per-column fill) obcházejí `mark_dirty` → každý MUSÍ začít clear (fill/blit REPLACE) kvůli copy-forwardu. **Nová libui komponenta `ui_segmented_t`** (`segmented.h/.c`, přidána do `ui/ui.h`): N-segmentový přepínač, vybraný = accent pilulka, text `UI_COLOR_BG_0` (kontrast v obou tématech) — použit pro **TDEV/MTIE přepínač v okně ALLAN** (`ALLAN_METRIC_RECT`, `screen_main_set_allan_metric` 0=ADEV/1=TDEV/2=MTIE; TDEV=τ·ADEV/√3, MTIE≈√3·τ·ADEV = ODHAD bez uložené fáze; metrika mění křivku + Y osu s dynamickými popisky `allan_ylabel`, σy(τ) tabulka zůstává ADEV referencí). **Spektrogram Δf** (s_view=26): heat strip X=čas, barva=odchylka `screen_main_freq_dev_unit()`, wrap styl (1 sloupec/vzorek @2 Hz). **Od 2026-07-25 ZÁLOŽKA** (sdílený segmented `VIEW_TABS` ve footeru vlevo) vedle **ALLAN** (23) a **HISTOGRAM** (6) — sesterská trojice přepínaná BEZ nav_push; Menu dlaždice zrušena (zpět Placeholder 2), metrika ADEV/TDEV/MTIE + LOGY přesunuty do středu footeru. **Status ribbon demo** (s_view=28, dlaždice „Status ribbon" — dřív Placeholder 3): LED chipy GPS/FPGA/REF/SENS. **⚠️ Headline + statistiky jsou pořád SIMULACE** → spektrogram/kužel/konfidenční pás kreslí šum, ne realitu, dokud se nenapojí reálná FPGA data.
- **⚠️ Navigace = zásobník** (`s_nav_stack`/`nav_push`/`nav_back` v app_gpsdo.c). Každý forward přechod pushne aktuální s_view; **BACK (`nav_back`) se vrací k tomu, ODKUD bylo okno otevřeno** — takže okno otevřené z Menu → BACK → zpět do Menu; podpora vnoření (Menu→Nastavení→O přístroji→BACK→Nastavení→BACK→Menu, Diagnostika→DIAGRAM→BACK→Diagnostika). `app_gpsdo_render_main` resetuje zásobník (kořen). `goto_view` renderuje cíl (0/1/3/7/12 mohou být návratové = spawnují podokna).
- **⚠️ REORGANIZACE 2026-08-13 (2. iterace): Nastavení = ČISTÝ rozcestník.**
Všechny přímé ovládače se odstěhovaly do tematických podoken: **jas + auto-dim → nové
okno DISPLEJ (`s_view=36`)**, **zvuk (mute) → okno ALARMY** (patří k tomu, co umlčuje).
**Vzhled** se 2026-08-13 přesunul ještě dál — do okna **DISPLEJ** (je to vlastnost displeje,
ne obecné nastavení); mřížka se o buňku přeskládala, aby v ní nezůstala díra.
Nastavení je díky tomu mřížka **3×4 přes celou šířku** se **stejnou geometrií jako Menu**
(w=246, h=76, x=18/278/538, y=68/154/240/326 → dotykový cíl 8,9 mm): DISPLEJ · Vzhled ·
Jazyk · ALARMY · CAS · SIT · KALIBRACE · REFERENCE · ANIMACE · SESTAVY · O PRISTROJI ·
**PAMETI** · **SD KARTA** (mřížka je od 2026-08-23 plná — poslední volnou buňku, střední
sloupec 4. řádku, obsadila dlaždice PAMETI → okno benchmarku, `s_view=43`).
Vzhled a Jazyk jsou **přepínače** (label nese stav), zbytek naviguje.
⚠️ Eased jas bar tiká nově v `s_view==36`, ne v 7.
⚠️ **Okno DISPLEJ má od 2026-08-23 i přepínač rozložení hlavní obrazovky**
(HYBRIDNÍ ↔ KLASICKÉ, `LAYOUT_RECT`, karta symetricky pod Vzhledem) — viz úvod dokumentu.

**⚠️ REORGANIZACE 2026-08-13 (1. iterace): Nastavení = rozcestník konfigurace, Menu = nástroje.**
Do okna **Nastavení** se z Menu přesunuly **Čas, Alarmy, Kalibrace, Animace** a přibyla
**Síť** (`s_view=35`). Pravá polovina Nastavení je teď **mřížka 2×5** (`SETNAV_X0/X1=410/600`,
w=182, y=72/140/208/276/344, h=62 = 7,3 mm — těsně nad projektovým minimem): Vzhled ·
Jazyk · SIT · CAS · ALARMY · KALIBRACE · ANIMACE · REFERENCE · O PRISTROJI · SESTAVY.
Levá polovina zůstává přímé ovládání (zvuk/jas/auto-dim). Uvolněné 4 sloty v Menu jsou
**placeholdery** (`ACT_FREE`) — ⚠️ dotyk na ně **NEdělá `nav_push`**, jinak by se BACK
zanořoval do prázdna.

- **Okno SÍŤ (`s_view=35`)** — DHCP ZAP/VYP + statická IP/maska/brána (výběr pole a oktetu,
  `−/+` po jednotkách s wrapem 0..255, bez přetečení do sousedního oktetu). Persist v syscfg
  blobu (magic **„SCF8" → „SCF9"**).
  - **Karta „Stav linky" je od F5 (2026-08-22) ŽIVÁ** (`s_view==35` v `app_gpsdo_tick`,
    per-řádek change-detect): **Link** (UP + rychlost/duplex / DOWN / „ETH init selhal
    (REF_CLK?)" / „CM4 nenabehla"), **IP adresa** přidělená DHCP serverem (accent barvou;
    „ceka na DHCP..." dokud nepřijde) a **PHY** (`LAN8742A`). Hodnoty pochází z lwIP na CM4
    přes IPC (`ipc_cm4_net`) — CM7 na síti sám nedělá nic. Totéž podrobněji v UART `status`.
    ⚠️ Bez linky se **neukazuje stará IP** (jen `--`), aby okno nelhalo.
  - ⚠️ **Konfigurace (DHCP ZAP/VYP + statická adresa) se pořád JEN UKLÁDÁ** — reálně jede
    vždy DHCP. Chybí přenos: `g_net_dhcp`/statická adresa žijí v syscfg na CM7, ale
    **IPC snapshot ta pole nenese**, takže se k nim CM4 nedostane. Doplnit spolu
    s rozšířením snapshotu; pak `g_net_dhcp` → `dhcp_start()`, jinak `netif_set_addr()`.
    **DHCP klient se psát nebude — lwIP ho má** (`LWIP_DHCP`).

**MENU** (footer tlačítko na hlavní obrazovce, `b==4`) → **Menu rozcestník** (`app_gpsdo_render_menu`, s_view=12): **mřížka 3×4 = 12 dlaždic** (2026-07-19 rozšířeno z 3×3=9; h=76, gap 10, y=68/154/240/326 — zmenšeno z h=88/gap12, aby se 4. řádek vešel před footer; `MENU_ITEMS[MENU_N]` + `menu_activate()`, `ACT_*` enum — tabulka místo if-řetězce; **Restart NENÍ dlaždice — footer tlačítko vpravo vedle ZPĚT**, `MENU_RESTART_RECT` {460,417}). **Řádek 4 = „Animace" + „Math/Limity" + „Status ribbon"** (Animace = `ACT_ANIM` → s_view=24; Math/Limity = `ACT_MATH` → s_view=31, viz níže; Status ribbon = `ACT_RIBBON` → s_view=28). Poslední `ACT_PLACEHOLDER` slot byl 2026-08-01 obsazen Math/Limity; případný budoucí placeholder = no-op (`menu_activate` prázdný `case`, dotyk **nedělá `nav_push`**, protože nikam nenaviguje). Zbylých 9 dlaždic je jen SYSTEM/NÁSTROJE (kontextová okna GPS/Histogram/Trend NEJSOU v menu — dostupná přímo z hlavní obrazovky přes pilulku/tap): Diagnostika, Nastavení, System Health, **Čítač** (s_view=19, živý syrový detail měření FPGA: SPI status, obě odbočky /4 + /16 přes `fpga_freq_get_last()` — kopie latche posledního DATA rámce pod IRQ-off, bezpečné z UiTasku —, edge_count, gate ms, SEQ, dekódované error_flags, fázový status jako 4+4 indikátory present/fine; hlavní použití = SPI/měření bring-up), **Holdover** (s_view=16, živý stav disciplinace WARMUP/LOCK/HOLDOVER/NO-LOCK z GPS fixu + FPGA linku + timepulse + OCXO teploty 0x49), **Datalog** (s_view=17, vstupní bod pro logování do W25Q DATA regionu — zatím NEAKTIVNÍ, ukazuje base/kapacitu/JEDEC + plán ~32 B/10 s, 1/3 regionu → ~80 dní), **Alarmy** (s_view=18, přehled hlídaných podmínek + počitadla `g_alarm_fpga_lost`/`g_alarm_gps_lost` + živý mute stav — doplňuje Nastavení, které má jen globální mute), **Kalibrace** (s_view=15, **editovatelná**: AD8307 slope/intercept + ADS 12V/5V gain přes `-`/`+` na řádku — mění `g_calib` okamžitě živě, tlačítko ULOZIT persistuje do W25Q CALIB store přes `calib_save()`; ADC3 VREF + TDC krok zůstávají read-only HW konstanty), **Cas** (s_view=22, časová zóna AUTO CET/CEST / ruční — viz sekce RTC); **Restart** = footer tlačítko (ACTIVE variant) vedle ZPĚT. ⚠️ **NEJSOU dlaždice** (dostupné z kontextu, kam patří — reorganizace 2026-07-18): **Senzory** + **Diagnostika*** = tlačítka v System Health; **O přístroji** + **Reference** = tlačítka v Nastavení; **Paměť** + **Selftest** = tlačítka ve footeru Diagnostiky (technický hub). *Diagnostika zůstává i dlaždicí (časté použití). Restart → **potvrzovací okno** „Opravdu restartovat?" ANO/NE (s_view=13); ANO → `g_reboot_req=1` → defaultTask `NVIC_SystemReset`. Reference/Kalibrace/Holdover/Datalog/Alarmy/Čítač/Selftest = `app_gpsdo_render_reference/kalib/holdover/datalog/alarms/counter/selftest` (s_view=14/15/16/17/18/19/20); Holdover/Reference/Alarmy/Čítač jsou živé (v `app_gpsdo_tick`), Datalog/Kalibrace/Selftest statické (Selftest se překreslí po SPUSTIT).
- **Animace — sada 2026-07-21, řízená globálním přepínačem + `anim.h`.** `anim_t{cur,target}` + `anim_reset/anim_set/anim_step(a,k,snap)` (ease-out: `cur += (target−cur)·k`, `snap` = práh pro dorovnání a zastavení překreslování) je od tétorevize **sdílený header `CM7/app/anim.h`** (`static inline`, ne `static` v jednom `.c`) — používá ho jak `app_gpsdo.c`, tak `screens/screen_main.c` (dvě různé translation units). **Globální přepínač `g_anim_enabled`** (perzist `BKP_DR6` bit8 + syscfg blob pole `anim_en`, magic bump `"SCF2"`→`"SCF3"`) žije v **okně Animace** (Menu → dlaždice „Animace", `ANIM_TOGGLE_RECT`) — `anim_step` sám o sobě při VYP okamžitě skočí na cíl, takže VŠECHNY dále popsané animace se bez zásahu volajícího chovají jako dřív (žádná per-volání kontrola flagu). Plynulost všude táhne **vlastní rychlý tik `app_gpsdo_tick_anim` @ ~20 Hz** (vedle `tick_freq` ve `freertos_task_ui.c`), dispatchovaný podle `s_view` — NE 2 Hz `app_gpsdo_tick`. Konkrétní animace:
  - **Okno Animace/demo** (s_view=24, dlaždice „Animace" v Menu řádek 4): demonstrace `anim_t` na **RF bargrafu** (`ui_bargraph_update` = jen změněné segmenty + value text v clearnutém boxu). Cíl skáče (reálný RF z `g_sensors[SENS_ADS1]`, nebo bez signálu demo sekvence úrovní ~2 s/krok), aktuální ho plynule dojíždí. **Footer tlačítko „PRIKLADY ANIMACI >" (`ANIM_DEMO_RECT`) → subokno níže.**
  - **Subokno „PRIKLADY ANIMACI"** (s_view=25, `app_gpsdo_render_animdemo`/`tick_animdemo`, `nav_push(24)` → BACK zpět do Animace, proto `goto_view` má `case 24`): **6 dlaždic, každá jeden typ animace, vše běží NEPŘETRŽITĚ ve smyčce** z `app_gpsdo_tick_anim` (~20 Hz, vlastní frame counter). Dlaždice: 1 ease-out bar, 2 pulzující LED (radius+barva OK/WARN/BAD), 3 flash tlačítka (accent obrys periodicky), 4 eased číslo, 5 zvýraznění poslední číslice, 6 fade text. ⚠️ **Záměrně NEZÁVISÍ na `g_anim_enabled`** — je to ukázka, hýbe se i při vypnutých animacích (vlastní raw ease `ad_ease`, ne `anim_step`). Účel: ověřit každou animaci v izolaci (flash na reálném tlačítku trvá jen ~150 ms, těžko se trefit). Každý tik plný clear+redraw vnitřku každé dlaždice (BG_CARD REPLACE → mark_dirty → copy-forward OK).
  - **Flash tlačítek/pilulek při stisku = DVA mechanismy.** (a) **In-place footer RUN/GATE/CHAN** na hlavní obrazovce: `screen_main_button_flash_start/tick` (neblokující, obrys odezní přes 3 tiky). (b) **In-place přepínače v oknech** (NE navigační): `tap_flash(rect)` / `tap_flash_pill(rect)` v `app_gpsdo.c` (tenké obaly nad `tap_flash_r(rect, rad)`) — **2px accent OBRYS** přes prvek (`rad` = tvar dle prvku: `UI_DIM_BUTTON_RADIUS` vs `UI_DIM_PILL_RADIUS`). Obrys se kreslí PŘES už vykreslený prvek → **text zůstává** (nepřekresluje se), netřeba label ani re-render obsahu. **Volá se JEN u 4 in-place přepínačů, které po stisku ZŮSTANOU na obrazovce** (Animace `ANIM_TOGGLE`/`ANIM_DIGIT`, Čas `TZ_AUTO`, Trend −/+) — tam se dotčené tlačítko hned překreslí (`anim_toggle_redraw`/`cas_upd_mode`/`render_trend_scale_btns`), takže obrys (jde přes `prim_internal_blend_px`, mimo dirty-rect copy-forward) je pokrytý. **⚠️ U NAVIGAČNÍCH tlačítek + pilulek se flash ZÁMĚRNĚ NEvolá (2026-07-22):** problik PŘED navigací stojí vždy ≥1 panel-frame (~17 ms), protože musí být vidět dřív, než se flipne cílová obrazovka → jen zdržoval přepnutí. Odezva navigace = samotná okamžitá změna obrazovky. Vývoj vzhledu (než padlo „bez flashe u navigace"): `osDelay(70)` → 1px obrys → plný accent fill (překryl text) → ACTIVE-tint → high-contrast fill → 2px obrys. In-place RUN/GATE/CHAN na hlavní obrazovce mají vlastní `screen_main_button_flash` (kreslen ve stejném snímku jako akce → zdarma). Gate `g_anim_enabled` (u ANIM/DIGIT toggle při VYP→ZAP problik nesvítí — flag je při čtení ještě 0).
  - **Eased jas v Nastavení** (`settings_tick_jas`, s_view=7): HW backlight (`g_brightness`) se mění OKAMŽITĚ (žádný lag stmívání), jen vizuální bar na obrazovce plynule dojíždí k nové hodnotě (`settings_draw_jas_bar`, boxbased partial redraw).
  - **Eased Offset/σ@1s/Drift** (`screen_main_tick_stats_anim`): tři `anim_t` nad *raw* hodnotami (před `fmt_frac`), value-only partial redraw (`draw_stat_card_value`). `stats_anim_resync()` (voláno z `render_body_grid` PŘED plným renderem) zajistí, že návrat na hlavní obrazovku z podnabídky ukáže číslo okamžitě (ne zastaralé nedojeté).
  - **Eased trend sparkline** (`screen_main_tick_trend_anim`, jen v2): `s_spark_prev[]` → `s_spark[]` (cíl) interpolace přes `TREND_ANIM_STEPS=20` kroků, `trend_plot_draw()` sdílí kreslení sigma-band+polyline+regrese mezi plným 1Hz renderem a 20Hz tikem. `trend_anim_resync()` (stejný vzor jako stats) + reset při změně počtu bodů `n` (jinak by interpolace mezi různě dlouhými poli byla nesmysl).
  - **Micro-flash tlačítek na hlavní obrazovce** (`screen_main_button_flash_start/tick`, s_view=0): 2px accent obrys tlačítka na `BTN_FLASH_FRAMES=3` tiků po stisku (leží celý uvnitř dirty rectu, který už zavolal `screen_main_redraw_button`).
  - **Zvýraznění změněné číslice v headline — ODSTRANĚNO 2026-07-25** (na přání uživatele). Smazán celý freq-flash mechanismus (`screen_main_freq_flash_tick`, `freq_seg_draw_color`, `s_first_frac_seg`, `FREQ_FLASH_FRAMES`), samostatný přepínač `g_digit_anim_enabled` (+ globál, BKP_DR6 bit9, syscfg pole `digit_anim_en`, `ANIM_DIGIT_TOGGLE_RECT` tlačítko + touch) i persist (magic bump `"SCF5"`→`"SCF6"`). Číslice měřeného kmitočtu se překreslují bez jakéhokoli podbarvení. K dohledání v git historii.
  - **Pulsující stavová LED** (`cd_pulse_tick`, blokové schéma komunikace s_view=21): uzly ve stavu BAD/WARN "dýchají" (trojúhelníková vlna poloměru 4..6..4 px, perioda ~1,2 s); OK/ACC uzly zůstávají statické. Vyžaduje skutečný clear před každým překreslením (kruh mění poloměr, ne jen barvu → netriviální jako digit-highlight). ⚠️ **Clear box MUSÍ pokrýt MAX poloměr + AA okraj symetricky** (16×16 = ±8 od středu): AA ramp `prim_fill_circle` sahá na radius+1, takže menší box nechá on-axis/AA pixely většího poloměru jako „duchové čárky" vpravo/dole od LED (opraveno 2026-07-23, dřív 12×12). U širokých uzlů (FPGA GW1NR-9) zasahuje popisek do LED rohu → po clearu se `CD_LABEL[i]` obnoví (clip na box, stejné pořadí jako `cd_node`: kruh, pak text).
  - **Spinner u ULOŽIT v Kalibraci** — ⚠️ **jen statická ikona**, ne skutečný víceframový spin: `calib_save()` je jedno blokující volání (`w25q_store_write` = erase+payload+hlavička bez yield bodu) a UiTask je po tu dobu jednovláknově uvnitř něj, takže opravdová animace během zápisu by vyžadovala zásah do sdíleného (power-safety kriticky seřazeného) `w25q_store.c` — záměrně se do toho nešlo bez explicitního zadání.
  - **Boot splash fade-in** (`splash_draw_content`, `app_gpsdo_boot_splash_tick`): využívá existující jednorázovou smyčku 10×`osDelay(100)` v `StartUiTask` (PŘED hlavní smyčkou, ne za běhu) — barvy lineárně interpolované černá→cíl (`fade_color`) přes `SPLASH_FADE_TICKS=8`; na rozdíl od ostatních položek dělá **plný** redraw každý tik (bezpečné, protože je to jen 8× při bootu, ne steady-state).
- **SYS pilulka barevně** (`compute_sys_level` v screen_main.c): agregace VŠECH chyb → zelená „SYS OK" / amber „SYS !" / červená „SYS ERR". **AMBER** (degradace, funguje) = FPGA SIGNAL_LOST/no-link, Si5356 nečteno/kalibrace, sensor err_streak, watchdog/crash reset (zotaveno), CM4 absent. **RED** (kritické) = **Si5356 LOS_CLKIN (bit3!) nebo PLL_LOL** (ztráta 10 MHz reference — LOL se při fyzické ztrátě vstupu neasertuje, proto je bit3 nutný), selftest FAIL. **Bit2 = LOS_XTAL se záměrně NEhodnotí** (bez krystalu trvale 1 — status `0x04` je normální stav této desky). `screen_main_sys_poll` (v tick_clock) překreslí header při změně úrovně.
- **Trend fullscreen — časové okno až 60 DNÍ přes decimační pyramidu** (tlačítka `−`/`+` krokují presety **1 min / 10 min / 1 h / 6 h / 1 den / 7 dní / 30 dní / 60 dní**). ⚠️ Plochý ring `s_y[]` pokrývá jen 120 s — dlouhá okna kreslí **vlastní decimační pyramida `s_tr[]`** (`trend_feed`, stejný princip jako ADEV pyramida, ale **decimace ×4** místo ×10 a delší ringy → hladší křivka): 9 stagí, stage s má krok 4^s s a rozsah 128·4^s (s=0: 1 s/128 s … s=8: 65536 s/97 dní), paměť ~4,7 kB v RAM_D1. `screen_main_render_trend_big` vybere **nejjemnější stage, který okno pokryje** (`tr_pick`); pokud vyšší stage ještě nemá data (stage 8 se plní až po ~18 h běhu), **spadne na nejvyšší stage s ≥2 vzorky** — vždy je vidět něco místo „Waiting". Overlay ukazuje okno · skutečně pokrytý čas · krok decimace. ⚠️ `TREND_PRESETS` musí být **`int32_t`** (60 dní = 5 184 000 s přeteče int16).
- **⚠️ Radiální gradient pozadí — rychlý isqrt** (`gradient.c`): per-pixel vzdálenost byla dříve lineární hledání `while ((d+1)²≤d2) d++` = až ~540 iterací/pixel × 384k px → **stovky ms** (citelné hlavně při přepnutí schématu = rebuild `bg_cache`). Nahrazeno Newtonovým `isqrt32` (~O(log), pixel-identické) → ~40× rychleji. Rect pilulek se zachytává v `render_header` (`s_gnss_pill_rect`/`s_sys_pill_rect`), `screen_main_hit_gnss/sys` testuje zásah; `screen_main_hit_allan/hit_trend` = tap na Allan/trend kartu → fullscreen okno („↗" náznak v hlavičce karet):
  - **GNSS pill → GPS/GNSS okno** (`app_gpsdo_render_gps`, s_view=2): **ŽIVÉ** (refresh ~2×/s v `app_gpsdo_tick`, first/values split jako diag). **NEsymetrické sloupce** (`GPS_LX/LW/RX/RW` makra): levý široký (502 px) = FIX + Družice, pravý úzký (250 px, ~⅔) = Čas/Poloha/Lokator/Přijímač. **Řádek FIX**: „FIX 3D" (mono_25) vlevo + **TP 100 kHz/10 Hz** vpravo (timepulse přesunut z vlastní karty; s fixem 100 kHz = GPSDO PLL ref disciplinovaná na GNSS, bez fixu 10 Hz = holdover indikátor). **Karta Lokator** (bývalá TIMEPULSE): „Locator JN89NS85KN" = 10-znakový Maidenhead grid (`fmt_locator` z lat/lon, mono_18 accent). Čas+datum z **RTC** (UTC), poloha lat/lon/alt (mono_16), HDOP/PDOP, **Přijímač** = „NEO-7M" v headeru + živé `Vet:`/`Fix:` (z `g.sentences`/`g.fixes`). **Karta Družice = JEDNO zobrazení na plnou šířku, přepínatelné DOTYKEM** (`s_gps_polar`, tap na kartu `GPS_SAT_RECT`): **bargraf C/N0** (default, až 14 nejsilnějších, C/N0 nad + PRN pod sloupcem) ↔ **polární sky plot** (kruh r=86, 3 elevační kružnice + N/S/E/W kříž, tečka=družice: azimut 0=sever po směru hodin, poloměr ∝ 90−elevace = zenit ve středu, PRN vedle tečky). Barvy zelená/žlutá/červená dle C/N0. Data z GSV: `parse_gsv` plní `g.sats[]` PRN+elev+**azim**+C/N0+**`constel`**; `gps_sat_t`/`GPS_MAX_SATS`(=24) v `gps.h`. **Změnový klíč = mód + hash az/el/snr VŠECH družic** (tečka/bar se pohne i u slabé). **GSV je multi-souhvězdí (per-talker akumulace):** GPGSV/GLGSV/GAGSV/GBGSV se skládají do samostatných `done[GPS_CONSTEL_N]` a merge je spojí do `sats[]` (GPS first) — dřív jeden akumulátor, takže GLGSV (msgnum=1) vynuloval právě nasbíraný GPGSV. Jádro `gsv_feed`/`gsv_merge`/`gsv_constel` je bezstavové (kryté `gps_selftest`). Pole `constel` = `gps_constel_t` (GPS/GLONASS/Galileo/BeiDou) → **PRN v bargrafu i sky plotu nese RINEX prefix** (`G05`/`R68`/`E12`/`C07`, tabulka `k_constel_ltr[]="GREC"`); barva tečky/sloupce zůstává dle C/N0 (prefix je jen textový, sílu signálu nepřebíjí). Změnový klíč zahrnuje `constel`. **GLONASS se zapíná na HW přes UART `gps glonass`** (`gps_config_gnss`, UBX-CFG-GNSS GPS+SBAS+QZSS+GLONASS) — best-effort, NEvolá se z `gps_init` (nejde ověřit bez HW, špatný blok by vypnul GPS); NEO-7M může NAKnout, parser GLGSV zvládá tak jako tak. Viz [[gps-todo]].
  - **SYS pill → System Health okno** (`app_gpsdo_render_health`, s_view=3): **živé** (refresh 2×/s v `app_gpsdo_tick`, stejný first/values split jako diag). RTOS (heap/CPU), **volný stack tasků** (`osThreadGetStackSpace`, byty; <64 B → červený `!`), I2C chybovost (agregace `g_sensors[].err_total/streak`, **0x4A vyřazen** = neosazen), linky (FPGA/Si5356/senzory n/10), karta **System**: „Power supplies: OK/Unkn/FAIL" (verdikt z 12V/5V ±10 %, konkrétní napětí jen v Diag/SENZORY), Uptime, „Reset: <příčina>" (červeně při IWDG/crash), „Selftest: PASS/FAIL". **SENZORY > podmenu** (`app_gpsdo_render_sensors`, s_view=4) = přehled **aktuálních hodnot** všech 10 senzorů, dvousloupcově (vlevo Teploty, vpravo Napětí), `dlabel`+`dval` jako diag. Min/max/avg/err jen přes UART `sensors`. `osThreadGetStackSpace` jen při otevřeném okně (scan stacku nezatěžuje běžný provoz). **PAMET podokno** (`app_gpsdo_render_mem`, s_view=5, **tlačítko ve footeru Diagnostiky** — přesunuto 2026-07-18 z Health/Menu) = využití interní FLASH/RAM (linker symboly), RTOS heap (used/total), SDRAM 32 MB, W25Q 64 MB (JEDEC). **NASTAVENI podokno** (`app_gpsdo_render_settings`, s_view=7, tlačítko v Health footeru, **dvousloupcové**): vlevo mute zvuku (ikona `ui_icon_speaker/_muted`), jas −/+ (bargraf + %, clamp 25..255), auto-dim zap/vyp + prodleva −/+ (presety 15..600 s); vpravo **Vzhled** (cyklus 5 schémat TMAVÉ/SVĚTLÉ/STŘEDNÍ/OBRYS/KONTRAST — runtime přepnutí palety; dnes žije v okně DISPLEJ s_view=36), **Jazyk** (ČESKY/ENGLISH — zatím jen infrastruktura `g_lang_en`), **REFERENCE Si5356 >** (`REF_RECT`, s_view=14 — přesunuto 2026-07-18 z Menu dlaždice) a **O PRISTROJI >**; časová zóna má VLASTNÍ okno **Cas** (dlaždice v Menu, s_view=22). Persist DR2+DR6 přes `g_sys_cfg_dirty`. Statické okno (není v ticku, překreslí se při tapu).
  - **Barevná schémata (libui/src/theme.c):** `UI_COLOR_*` makra derefují ukazatel `g_ui_theme` (runtime tabulka) → přepnutí `ui_theme_select(idx)` NEVYŽADUJE změnu volajících. ⚠️ `UI_COLOR_*` proto NELZE použít ve file-scope `static const` inicializátorech. Po přepnutí NUTNÉ `screen_main_invalidate()` + `screen_main_init()` (bg_cache je předrenderovaná ve starých barvách) — dělá to THEME handler v `app_gpsdo_handle_touch`. Uložené schéma aplikuje `app_gpsdo_init` před prvním renderem. **5 schémat (2026-08-19, index `UI_THEME_*` 0..4, persist `g_theme_idx`):** **0 TMAVÉ** · **1 SVĚTLÉ** · **2 STŘEDNÍ** · **3 OBRYS** · **4 KONTRAST**. Schémata 2/3 jsou **varianty tmavého**, lišící se JEN vzhledem **NORMAL tlačítek** (GATE/CHAN/MENU/PERIOD + navigační): STŘEDNÍ = světlejší VÝPLŇ (tlačítka „vystoupí" z pozadí), OBRYS = jen světlejší RÁMEČEK (výplň zůstává tmavá). **KONTRAST = samostatná plná paleta** (černé pozadí, bílý text, syté jasné akcenty, tlačítka = černá výplň + jasný barevný obrys) pro max. čitelnost. V TMAVÉM schématu jsou NORMAL tlačítka nově **laděná do (světle) modra** (`btn_norm` = navy výplň + jasný sky rámeček); **ACTIVE zjasněno** (sytější modrý podklad + accent rámeček), aby vybraný stav vystoupil nad zesvětlalá NORMAL. ⚠️ NORMAL tlačítko má proto **vyhrazené role `btn_norm_top/border`** (`UI_COLOR_BTN_NORM_*`) — dřív bralo přímo `BG_1`/`LINE` (splývalo s pozadím). ⚠️ **Tlačítka = JEN plná výplň `_top` + rámeček `_border`** (dřívější `_bot` role všech variant byly mrtvý kód — `ui_button_render` čte jen výplň; gradient se záměrně nepoužívá, bleedoval by přes zaoblené rohy; odstraněno 2026-08-19). Varianty 2/3 se **staví za běhu** kopií `THEME_DARK` + override `btn_norm_*` (`build_variants` v theme.c); KONTRAST je statická `const` paleta (`THEME_HICONTRAST`). Tlačítko **Vzhled** v okně DISPLEJ (s_view=36) je **cyklické** (tap → další z 5, `THEME_LABELS[]` = jen názvy bez prefixu „VZHLED:", jinak by „KONTRAST" 16 zn. přetekl 200px tlačítko při mono_22 13px/zn.). **Persist = 3 bity:** BKP_DR6 **bit0** (nejnižší, = původní „světlé") **+ bity9:10** (vyšší dva, `g_theme_idx>>1`); staré záznamy měly bity9:10=0 → 0/1 = tmavé/světlé jako dřív (zpětně kompatibilní, žádný magic bump). Zrcadlí i syscfg blob (`theme_idx`, clamp `&0x07`) a setup profil.
  - **ALLAN okno** (`app_gpsdo_render_allan`, s_view=23, **tap na Allan náhled** na hlavní obrazovce — ztlumené „↗" za titulkem karty značí klikatelnost, `screen_main_hit_allan`): **velký log-log graf σy(τ)** (`screen_main_render_allan_big`: Y dekády 10⁻⁶..10⁻¹⁰ s popisky mono_16, X dynamické dekády τ 1 s..100k+ s, křivka z ADEV pyramidy + markery) + vpravo **σy(τ) tabulka** (sdílená `screen_main_render_stats_table`). **Karta na hlavní obrazovce (364×242, přes celou výšku mřížky) má plné osy v mono_14** + živou σy@1s v headeru; tohle okno je totéž ve větším (mono_16). Karta i okno sdílí `allan_plot(area, big)` → `adev_points()` + `allan_plot_curve()` (`ALLAN_Y_MIN/ALLAN_Y_DEC`).
  - **Histogram okno** (`app_gpsdo_render_histogram`, s_view=6, **tlačítko HISTOGRAM v okně ALLAN** — sesterská okna: v histogramu je zpět tlačítko ALLAN; **přepínání je BEZ `nav_push`**, takže ZPĚT z obou vede tam, odkud byla dvojice otevřena, ne k sobě navzájem): vlevo **histogram rozdělení y=(f−f₀)/f₀** (24 binů, auto-range, mean=zelená + medián=amber čára, **Gaussova referenční křivka** z (mean,σ), overlay N/x̄/s/med), vpravo **σy(τ) Allan tabulka** (τ=1/10/100/1k/10k s z ADEV pyramidy, `--` bez dat). Tlačítko **Y: LIN/LOG** (levý footer slot) přepíná osu (`screen_main_hist_logy/toggle`). **Change-key skip:** tick 2×/s překreslí JEN při změně `screen_main_stats_version()` (čítač vzorků, ~1×/s při RUN) nebo lin/log osy — jinak žádný sort/Gauss/ADEV/flip naprázdno (ALLAN okno má stejný mechanismus). **Vzorkování statistiky běží nezávisle na zobrazeném okně** (`app_gpsdo_tick_stats_sample` gatuje jen RUN/STOP; mimo main krokuje simulaci `screen_main_freq_sim_step` bez kreslení) → Allan/histogram rostou 24/7 i při screensaveru — dřívější vazba na main obrazovku zastavovala pyramidu na krátkých τ (bug „Allan nejde přes ~250 s"). Plot i tabulka si čistí svůj rect (`PRIM_BLEND_REPLACE`) → text MUSÍ mít baseline uvnitř rectu (ascent!), jinak AA hrany mimo clear oblast při refreshi tuhnou. Obě okna sdílí geometrii `HIST_PLOT_RECT`/`HIST_TABLE_RECT`.
- **Auto-dim** (UiTask): po `g_autodim_sec` bez doteku ztlumí backlight na 20/255 (`AUTODIM_LEVEL`, nikdy tma); **první dotek jen probudí** (nespustí akci tlačítka). Aplikace jasu = výhradně UiTask (`ws_panel_set_backlight` @ I2C4 pod mutexem, jen při změně cíle); app vrstva mění jen `g_brightness`.
- **Header hlavní obrazovky** (`screen_main_redraw_time`): čas HH:MM:SS + „UTC" popisek (RTC běží v UTC) + datum; vlevo **stavové mikro-ikony** — přeškrtnutý reproduktor (`ui_icon_speaker_muted`) při `g_sound_muted`, amber „H" při holdoveru (`!g.valid && g.fixes>0` = fix ztracen po tom, co už jednou byl). Změnový klíč zahrnuje i stav ikon → překreslí se i mimo tik sekundy.
- **Trend fullscreen** (s_view=9, tap trend karty): celá historie ringu (až `STAT_N`=120 s vs. 60 s na kartě), auto-scale + min/max frac popisky, drift overlay; change-key jako histogram. **O přístroji** (s_view=10, tlačítko v Nastavení): FW `gpsdo-ui v0.1` + `__DATE__ __TIME__` build, autoři OK2HAZ & OK2JNJ, MCU, uptime, selftest verdikt, sériové č. (zatím nepřiděleno → CALIB store). **Boot splash** (s_view=11): logo „GPSDO" (mono_75) + build + živý řádek Selftest (PASS/FAIL z `g_selftest_res`); UiTask ho drží ~1,4 s před hlavní obrazovkou.
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

**⚠️ `Project → Clean`: kdy ano a kdy je to ŠKODLIVÉ.** Clean je operace nad **výstupy**, ne nad
konfigurací — zahodí `.o` a **přegeneruje makefily z modelu, který má IDE v paměti**. Pomůže tedy
jen na zastaralé *výstupy*, nikdy na zastaralý *model*.

| změna | co udělat | proč |
|---|---|---|
| jen kód (`.c`/`.h`), bump verze | **nic — obyčejný Build** | `make` závislosti řeší sám; Clean je jen ztráta minut |
| linker script, `ipc_shared.h` | Build, ale **obě jádra** | závislost `make` zná; nesoulad bank ne |
| změna flagů/optimalizace uvnitř konfigurace | **Clean** té konfigurace | `.o` nesou staré flagy a `make` to nepozná |
| Debug ↔ Release | Build | každá konfigurace má vlastní adresář |
| **CubeMX „Generate Code", který přidal SOUBORY** | 🔴 **Close Project → Open Project → ověřit → teprve pak Clean → Build** | `.project` se parsuje **jen při otevření projektu** |

🔴 **Po regenu je Clean PŘED Close/Open aktivně škodlivý:** přegeneruje makefily ze stále
zastaralého modelu, takže z buildu zase vypadne middleware — a navíc přepíše případnou ruční
záplatu v `Debug/`. `F5 (Refresh)` nepomůže taky: odsvěží soubory, o kterých Eclipse **už ví**,
ale deklarace linkovaných zdrojů znovu nečte. Jediná operace, která model znovu načte z disku, je
**Close → Open** (poslední záchrana = odebrat projekt z workspace *bez* mazání obsahu a reimportovat).
Ověření **před** buildem (ať nestavíš minuty na rozbitém modelu):
`grep -c FatFs CM7/Debug/sources.mk` a `grep -c hal_eth CM4/Debug/Drivers/STM32H7xx_HAL_Driver/subdir.mk`
→ obojí musí být **> 0**. A po každém regenu `git diff` — ukáže, co CubeMX přepsal (včetně ručních úprav).

**Build bez IDE: `./scripts/build.sh [Debug|Release] [BOTH|CM7|CM4]`** (přidáno 2026-08-22).
Kromě stavění dělá **dvě kontroly, které dřív chyběly**: (a) porovná zdrojáky deklarované
v `.project` proti generovaným `subdir.mk` → **zastaralý model IDE nahlásí jménem souboru
ještě PŘED buildem** (místo záhadného `undefined reference` po minutách překladu);
(b) varuje, když je některý obraz **starší než `ipc_shared.h`** (= „přeložil jsem jen jedno jádro"
→ nesoulad bank). Obě kontroly jsou otestované vyvoláním té poruchy.
Staví přes ST `make` + jejich `arm-none-eabi-gcc` (cesty hledá globem, přežije upgrade IDE) nad
už vygenerovanými makefily; na konci vypíše velikosti obou obrazů a **`IPC_VERSION` s připomínkou
flashnout obě banky**. Vzniklo proto, že po CubeMX regeneraci **IDE přestává přegenerovávat
`Debug/*/subdir.mk`** a z buildu tiše vypadne middleware (2026-08-12 FatFs, 2026-08-22 FatFs na CM7
**i** HAL ETH na CM4) — projeví se to až jako `undefined reference` při linkování, ačkoli
`.project`/`.cproject` jsou v pořádku. Léčba je `Close Project → Open Project` (viz
`CUBEMX_CHECKLIST.md`); skript je nezávislá cesta, jak mezitím vyrobit firmware.
⚠️ Skript makefily **negeneruje** — poprvé je musí vyrobit IDE.
**⚠️ Velikost FW: stavěj konfiguraci `Release`, ne `Debug`.** Debug jede na **`-O0`**;
`Release` je v `.cproject` už nachystaná s **`-Os`** a od Debugu se **neliší ničím jiným**
(stejné defines včetně `DEBUG`, takže `__HAL_DBGMCU_FREEZE_IWDG1` a spol. se chovají stejně).
Změřeno plným buildem 2026-08-13: **676 612 → 525 688 B, tj. −150 924 B (−22 %)**, 0 varování.
V IDE: *Project → Build Configurations → Set Active → Release*. (Adresář `CM7/Release/`
zatím neexistuje, protože se nikdy nestavěla — makefily si IDE vygeneruje samo.)
⚠️ Optimalizace mění časování — po přepnutí ověř na HW to, co na něm visí: DWT `delay_us`
(SPI2/FPGA rámce), bit-bang pípání v `bootled_fail()`, I2C recovery pulzy.
✅ **Riziko latentního UB prověřeno (2026-08-13):** celý projekt přeložen `-Os` s
`-Wmaybe-uninitialized -Wstrict-aliasing=2` — to jsou varování, která `-O0` vypsat
NEUMÍ, protože potřebují datový tok z optimalizátoru. Mimo vendor kód vzniklo
**jediné** (`-Wformat-truncation` v `allan_ylabel`, mezitím opraveno) a **žádné**
uninitialized/aliasing. Přepnutí na Release tedy neodemkne skrytou UB — zbývá
ověřit už jen to časování výše.

**⚠️ UTF-8 v popiskách:** horní indexy (`⁻¹²`) jsou **3 bajty na znak**, ne jeden.
`allan_ylabel` skládá „10" + znaménko(3) + dvě cifry(6) + NUL = přesně 12 B. Buffery
pro takové řetězce dimenzuj podle bajtů, ne podle počtu znaků.

**Kde je flash doopravdy** (z map souboru, Debug/-O0, celkem 686 KB):
`.rodata` 333 KB — z toho **fonty ~285 KB = 41 % celého programu** (`ui_font_mono_22` sám 46 KB);
`.text` 353 KB. **Mazání nepoužitých FUNKCÍ velikost NEZMĚNÍ** — `-ffunction-sections`
+ `--gc-sections` je zahodí už dnes (ověřeno: po odstranění 24 mrtvých funkcí byl `.elf`
bajt za bajt stejný). ⚠️ **Neplatí to pro nedosažitelné VĚTVE uvnitř živé funkce** — ty
linker odstranit neumí a stojí flash doopravdy. Případ z 2026-08-13: po odstranění
`prim_path_quad_to()`/`prim_path_arc()` zůstaly jejich `case` větve ve `flatten()` (path.c)
a držely při životě celý `bezier.c`; jejich dobrání ušetřilo **888 B** a zmenšilo
`prim_path_op_t` z 20 na 8 B (cesty se alokují na haldě → i úspora RAM).
**Poučení: když odstraníš producenta, dober i konzumenta** — půlka odstraněné
funkcionality je horší než obě čisté varianty.
Pořadí páky podle výnosu: **Release −151 KB → fonty (~285 KB, ale kvalita) → zbytek**.
Toolchain (arm-none-eabi) není v PATH tohoto prostředí, ale **je na disku**:
`C:\ST\STM32CubeIDE_2.1.0\...\gnu-tools-for-stm32.14.3...\tools\bin\arm-none-eabi-gcc.exe`
(GCC 14.3) — použitelný pro **kompilátorový audit** (`-Wall -Wextra -Wshadow` +
`-fanalyzer`) jednotlivých souborů bez IDE. Flash/link jen z IDE.
⚠️⚠️ **CPU zátěž NEMĚŘ přes ladicí sondu.** `STM32_Programmer_CLI -c mode=HOTPLUG -r32 …`
cíl na dobu čtení **zastaví**. Běhový čítač FreeRTOS (DWT CYCCNT) přitom běží dál, ale
IDLE task se nevykonává → `g_rtos_cpu_pct` v následujícím okně vyjde **nafouklý**, a čím
častěji se čte, tím víc. Změřeno 2026-08-13: jedno izolované čtení dalo 71 %, po přechodu
na opakované čtení 95–98 %, zatímco **`stats` (bez sondy) ukázal 60 % a IDLE 40 %** —
a to sedí s dlouhodobou zkušeností, že zátěž nešla přes 80 % ani po dnech běhu.
**Jediné důvěryhodné měření CPU je UART `stats`** (per-task, DWT okno 1 s, bez debuggeru).
Sonda je v pořádku na *stavová* data (čítače, registry, řetězce) — ne na cokoli časového.
⚠️ Ze stejného důvodu může halt sondy rozbít probíhající I2C/SPI transakci, takže
čítače chyb čtené za běhu sondy můžou být nadhodnocené.

**Testy:** UART `selftest` = neblokující pure-logic unit testy na targetu (idiom
projektu — testy běží na zařízení, ne na hostu; `run_selftests` ve freertos.c, i při bootu):
CRC16, hystereze /4↔/16 (`fpga_freq_select_core`), GPS parser (`gps_selftest`), fmt_frac+hist_h
(`screen_main_selftest`), Maidenhead lokátor (`app_gpsdo_selftest`), kalendář+DST (`rtc_selftest`),
datalog záznam+CRC+čas (`datalog_selftest`), Math Mx+B/NULL/limit pass-fail (`meas_math_selftest`)
→ **„SELFTEST: 14/14 PASS"** (`SELFTEST_N` = 14, indexy 0..13; dál setup sanitizace, autocal
verdikt, prezentace měření, **SCPI parser**, **IPC seqlock+ring** a **vzory benchmarku pamětí**).
⚠️ **Při FAILu se vypíšou i indexy** neúspěšných testů a u SCPI přímo `scpi.c:<řádek>`.
`qspitest`/`storetest` = destruktivní HW testy.
⚠️ **`run_selftests()` NENÍ reentrantní a má vlastní zámek.** Několik testů drží velké buffery
jako `static` (`gps_selftest`, `scpi_selftest`, `ipc_selftest`) — jinak by přetekly stack malých
tasků (přesně to způsobilo boot-loop, viz [[triple-buffer-freeze]]). Tím ale vznikl sdílený stav
mezi **třemi** volajícími (defaultTask při bootu, UartTask `selftest`, UiTask SPUSTIT); souběh
se nečeká, vrátí se poslední známý výsledek. ⚠️ **Volat až za běžícím schedulerem** — před
`vTaskStartScheduler` má port `uxCriticalNesting = 0xaaaaaaaa`, takže by `taskEXIT_CRITICAL()`
přerušení už nikdy nepovolil (SysTick/`HAL_Delay` by zamrzly).
