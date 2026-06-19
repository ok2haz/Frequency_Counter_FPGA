# H757_LED — konsolidovaný projekt (baseline pred experimentem)

Konfigurace: **DSI lane clock 700 MHz, LTDC pixel clock 25 MHz**
Konzistentni IOC <-> dsihost.c <-> ltdc.c.

## Aktualni stav (overeno)

### Hardware
- MCU: STM32H747BITx (Cortex M7+M4 dual core)
- Displej: Waveshare 43H-800480-IPS (800x480 IPS, DSI, Pi 7" kompatibilni)
- Bridge: Toshiba TC358762 (DSI -> DPI)
- Panel MCU: Atmel ATTINY @ I2C 0x45 (rpi-panel-attiny-regulator kompatibilni)
- Touch: FocalTech FT5x06 family @ I2C 0x38 (NE Goodix!)
- TCXO: 10 MHz (HSE_VALUE=10000000)

### Co funguje
- [x] Panel MCU probe + power-on sekvence
- [x] TC358762 bridge init pres DSI generic write
- [x] Backlight ovladani pres ATTINY MCU
- [x] DSI Video Sync Pulses mode, 1 lane, RGB888, 700 MHz lane clock
- [x] LTDC ARGB8888, 25 MHz pixel clock, HSA=2 HBP=46 HFP=2 VSA=2 VBP=21 VFP=7
- [x] MPU konfigurace pro SDRAM framebuffer (Write-Through cacheable)
- [x] Color rotation workaround pres MAKE_RGB macro (kompenzuje cyklicky posun barev R->G->B->R)
- [x] testRED prikaz: cely framebuffer cervene
- [x] testY prikaz: 3 vodorovne pruhy R/G/B (kazdy radek uniform)

### Co nefunguje (known issue)
- [ ] test prikaz: 3 svisle pruhy R/G/B (kazdy radek varying) -> fine RGB stripes
- [ ] testM prikaz (memcpy approach pro varying data) -> stejny problem

Issue se objevuje SPECIFICKY pro varying data v ramci jednoho scanline.
memcpy diagnostika dokazala, ze problem NENI v CPU writes do SDRAM.
Root cause vyzaduje logic analyzer na DSI sbernici.

## Test prikazy pres UART (115200 8N1)

- `led on` / `led off` - testovaci LED
- `ram write` / `ram read` - test interni RAM (DTCM/AXI)
- `sdram write` / `sdram read` - test SDRAM
- `temperature` - precist TMP117
- `scanner` - I2C bus scan (mel by najit 0x38, 0x45, 0x48)
- `testRED` - cely displej cerveny (pres MAKE_RGB)
- `test` - 3 svisle pruhy R/G/B (POZOR: zobrazi fine stripes - known issue)
- `testY` - 3 vodorovne pruhy R/G/B (uniform per row)
- `testM` - testY pres memcpy (diagnostika - stejne stripes jako test)
- `pure_r` / `pure_g` / `pure_b` - RAW ARGB hodnoty BEZ workaroundu
  (slouzi pro overeni, ze color rotation R->G->B->R je stale pritomna v HW)
- `status` - status running

## Co bylo zmeneno oproti puvodni Cube generovane verzi

### CubeMX konfigurace (H757_LED.ioc)
| Parametr               | Puvodne                | Opraveno                |
|------------------------|------------------------|-------------------------|
| DSI lanes              | 2 (DSI_TWO_DATA_LANES) | 1 (DSI_ONE_DATA_LANE)   |
| DSI Mode               | Burst                  | Non-Burst Sync Pulses   |
| DSI Color Coding       | RGB565                 | RGB888                  |
| DSI PLL NDIV           | 100                    | 70                      |
| DSI PLL ODF            | DIV2                   | DIV1 (-> 700 MHz lane)  |
| LTDC HSync             | 5                      | 2                       |
| LTDC HBP               | 35                     | 46                      |
| LTDC HFP               | 35                     | 2                       |
| LTDC VBP               | 20                     | 21                      |
| LTDC VFP               | 20                     | 7                       |
| LTDC Layer 0 ImageWidth| 2403 (BUG)             | 800                     |

### Nove drivery
- `CM7/Core/Src/ws_panel.c` + `.h` - ATTINY MCU 0x45 driver
- `CM7/Core/Src/tc358762.c` + `.h` - TC358762 bridge init (11 DSI generic writes)
  - LCDCTRL = 0x001A0150 (VSDELAY=1, RGB888, UNK6, VTGEN, HSPOL, VSPOL)
  - Explicitni LCD timing registry (LCD_HS_HBP, LCD_HDISP_HFP, LCD_VS_VBP, LCD_VDISP_VFP)

### Upravene aplikacni soubory
- `main.c`:
  - MPU_Config() - SDRAM framebuffer @ 0xC0000000 jako Write-Through cacheable (2 MB)
  - Display init sekvence: ws_panel_probe -> power_on -> DSI_Start -> tc358762_init -> backlight
- `freertos.c`:
  - MAKE_RGB() macro pro color rotation workaround
  - 4 nove test prikazy (testRED, test, testY, testM)
  - 3 diagnosticke prikazy (pure_r, pure_g, pure_b)

## Hardware nuance (workaround)

**Color rotation R -> G -> B -> R**: STM32H7 LTDC->DSI->TC358762 retezec
posila bytes v poradi, ktere bridge interpretuje jako rotaci kanalu o 1 pozici.

Workaround: makro `MAKE_RGB(r, g, b)` v freertos.c (mozne presunout do main.h
pro globalni pouziti) - misto pisat raw 0xFFFF0000 pro cervenou, pouzivat
MAKE_RGB(0xFF, 0, 0) - to vrati pre-rotovanou ARGB hodnotu.

Mapovani:
  display_R = ARGB byte B (bit[7:0])
  display_G = ARGB byte R (bit[23:16])
  display_B = ARGB byte G (bit[15:8])

## DSI parametry - vypocet (700 MHz baseline)

Pro 1 lane RGB888, lane HS clock = 700 MHz:
- lanebyteclock = 700/8 = 87.5 MHz
- LTDC pixel clock = 25 MHz
- pomer lbc/pixel = 87.5/25 = 3.5

DSI VidCfg parametry:
- HorizontalSyncActive = HSA * 3.5 = 2 * 3.5 = 7 lbc
- HorizontalBackPorch  = HBP * 3.5 = 46 * 3.5 = 161 lbc
- HorizontalLine       = htotal * 3.5 = 850 * 3.5 = 2975 lbc

Pokud chces zmenit lane clock v CubeMX, musis regenrovat tyhle hodnoty.

## Postup pouziti

1. Import projektu do STM32CubeIDE: File -> Import -> Existing Projects
2. Build All (Ctrl+B)
3. Flash + UART terminal (115200 8N1)
4. Reset desku, ocekavany UART log:
   ```
   === Display init start ===
   ws_panel: MCU 0x45 detekovan, FW ID = 0xC3
   ws_panel: power-on sekvence dokoncena
   tc358762: init OK, bridge bezi
   ws_panel: backlight = 200
   === Display init dokoncen ===
   UART task ready
   ```
5. Pres UART zkusit: `scanner`, `testRED`, `testY`

## Co dalej

- [ ] FT5x06 touch driver (I2C 0x38, viz Linux edt-ft5x06.c)
- [ ] TouchGFX integrace + hello-world UI s tlacitkem
- [ ] Pripadne dale resit fine-stripes issue v context TouchGFX renderingu

## OPRAVA: TXEscapeCkdiv (DSI escape clock)

CubeMX hlasil DSI txclkesc = 21.875 MHz, ale max je 20 MHz.

Zmena:
- `DSIHOST.TXEscapeCkdiv` = 4 -> **5**
- DSITXEscFreq: 21.875 MHz -> **17.5 MHz** (pod 20 MHz max)
- v `dsihost.c`: `hdsi.Init.TXEscapeCkdiv = 5;`

Tahle hodnota se pouziva pro LP (Low-Power) escape mode v DSI - LPCommandEnable
a vsechny LP* parametry pro back porches a front porches. Pokud byla mimo spec,
mohla zpusobit problemy s LP packety, drift mezi packety nebo subtle data
corruption.

**Hypotezou je, ze tohle muze byt root cause fine-stripes problemu** v "test" prikazu,
jelikoz LP fáze probíhají MEZI scanline daty a jejich nestabilita by mohla rozhodit
synchronizaci pri varying dat per scanline.
