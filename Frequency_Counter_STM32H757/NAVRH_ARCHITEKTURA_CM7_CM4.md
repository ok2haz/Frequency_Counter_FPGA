# NÁVRH: Architektura CM7/CM4 + chybějící subsystémy — k diskusi

Stav: **NÁVRH. Nic z toho zatím není v kódu** (kromě already-hotových základů:
QSPI/W25Q store, USB CDC konzole, beeper driver bez volajícího).
Cíl: rozdělit práci mezi jádra, zapojit dosud mrtvý CM4, a naplánovat SD kartu,
Ethernet, webserver, SCPI, encoder, J7 LED/tlačítka, matematiku měření, beeper a USB.

## 0. HW fakta (ověřeno ze schématu `STM32H747BIT.pdf`)

| Subsystém | Na desce | Detail |
|---|---|---|
| **Ethernet** | ✅ **RMII PHY osazen** (LAN8720A/8742A pinout: TXP/TXN, MODE strapy, ~RST, LED1/REGOFF) | ETH_TXD0/1, TX_EN, RXD0/1, CRS_DV, MDC, MDIO, **REF_CLK + OSC_25M**, ETH_INT, ETH_RES |
| **SD karta** | ✅ slot na **SDMMC1, 4-bit** | D0–D3, CK, CMD + **card-detect** (SDMMC1_DET) |
| **USB** | ✅ OTG_FS (D+/D−, VBUS) | dnes běží CDC konzole |
| **J7** | ✅ 8pin „LED_Con" | **6× LED přes 560 Ω** + GND; tlačítka: piny/umístění POTVRDIT |
| **Encoder** | konektor dle dřívějšího plánu | **PA8/PA9 (TIM1 CH1/CH2) + PC13** tlačítko |
| **Beeper** | ✅ PH9 + TIM7 | driver hotový, `beeper_set()` zatím nikdo nevolá |

➜ **Ethernet modul (W5500 na SPI) NENÍ potřeba** — nativní MAC+PHY už na desce je.
Modul by byl krok zpět: ~10 Mbit efektivně, druhý TCP/IP stack mimo kontrolu, blokuje SPI.
Elegantní řešení = **nativní ETH + lwIP na CM4** (viz níže).

## 1. Filozofie dělení jader

**CM7 (480 MHz, dvojitá FPU, cache) = PŘÍSTROJ:** měření, matematika, UI, lokální
I/O, storage. Vše, co je real-time nebo počítá (CM7 má **double-precision FPU** —
Allan/regrese v `double`; CM4 jen single). Zůstává vlastníkem SDRAM/DMA2D/displeje.

**CM4 (240 MHz, D2 doména) = KONEKTIVITA:** celý síťový stack a vzdálená rozhraní.
Důvody: (a) ETH MAC + jeho DMA deskriptory žijí v **D2 SRAM (0x30000000) = domácí
doména CM4** — bez D-cache odpadají cache-koherenční pasti ETH DMA na H7;
(b) izolace: pád/záplava sítě nesmí ohrozit měření; (c) CM4 dnes jen bliká — 240 MHz leží ladem.

**Zásada:** mezi jádry teče jen malý, dobře definovaný stav (snapshot + příkazy).
Žádné sdílené periferie, žádný sdílený FatFs, žádné křížové HAL handly.

## 2. Rozdělení tasků

### CM7 (FreeRTOS — dnešní + nové)
| Task | Prio | Role |
|---|---|---|
| FpgaTask | Normal | SPI2 poll FPGA (20 Hz), protokol v1→v2 |
| **MathTask** *(nový)* | Normal | **matematika měření**: gap-free Σhran/ΣΔt (protokol v2), syntéza GATE 0,1/1/10/100 s, klouzavé statistiky (mean/σ/min/max/p-p), **ADEV pyramida z reálných dat**, lineární drift, Ω/LSQ regrese, korelace s teplotou OCXO. Plní UI + IPC snapshot. `double` FPU. |
| UiTask | BelowNormal | displej + touch + **encoder** (TIM1 CNT delta poll ~15 Hz vedle touche) + **J7 LED** (zrcadlí stav: LOCK/ALARM/LOG/…) + **J7 tlačítka** (debounce scan) |
| SensorsTask | Low | I2C senzory + ADC3 (beze změny) |
| defaultTask | Normal | GPS drain, RTC, BKP config, **IPC servis** (cmd ring z CM4 → aplikuj, snapshot publikuj) |
| **LogTask** *(nový)* | Low | **SD karta**: FatFs zápis měření/eventů (CSV/bin), rotace souborů, `SD:` stav do UI; jediný vlastník FatFs |
| — | — | **AlarmMgr** (modul, ne task): beeper vzory (ztráta locku/SIGNAL_LOST/SD plná), mute v UI, config v BKP/flash |

USB (CDC) zůstává na CM7 (už funguje; SCPI parser bude sdílený — viz §6).

### CM4 (FreeRTOS — nové, dnes prázdné jádro)
| Task | Role |
|---|---|
| **NetTask** | lwIP + ETH driver (RMII, deskriptory v D2), DHCP/static, link status |
| **ScpiTask** | **SCPI server na TCP 5025** (raw socket, standard lab přístrojů) — parser `libscpi`; GET čte IPC snapshot, SET posílá do cmd ringu |
| **HttpTask** | webserver (lwIP httpd): statická remote-UI stránka (HTML+JS v CM4 flash), **REST/JSON** (`/api/meas`, `/api/status`), volitelně WebSocket push 1 Hz; **soubory z SD přes HTTP** (CM7 je podá přes IPC file-read protokol — NE sdílený FatFs) |
| **SvcTask** | mDNS (LXI-style discovery, `gpsdo.local`), SNTP (cross-check času), telnet konzole (volitelně) |

## 3. IPC CM7↔CM4 — sdílená SRAM4 (D3, 64 KB, 0x38000000)

Bez OpenAMP — jednoduchý, auditovatelný protokol (styl FPGA rámce):

| Blok | Směr | Mechanismus |
|---|---|---|
| **`ipc_snapshot_t`** (~1 KB) | CM7→CM4 | **seqlock**: `seq++` (lichá=zápis), data, `seq++`; CM4 čte, při změně seq opakuje. Obsah: freq×1e5 (/4,/16, zvolený), stats (σ@τ, drift, offset, ADEV body), GPS (fix/sat/čas), senzory souhrn, health, alarmy, uptime, verze. |
| **cmd ring** (16×64 B) | CM4→CM7 | SCPI/web SETy: GATE, RUN/STOP, CHAN, log on/off… defaultTask aplikuje, odpověď do resp ringu (echo id + status) |
| **resp ring** (16×64 B) | CM7→CM4 | odpovědi + async eventy (alarm) |
| **file-read okno** (4 KB) | CM7→CM4 | HTTP download logů z SD: CM4 žádá (path,offset), LogTask plní |

Notifikace: začít **pollingem** (CM4 10–50 Hz — pro laboratorní použití stačí a je
nejjednodušší); HSEM IRQ přidat až bude třeba latence. Boot CM4 přes stávající
HSEM gate (`system_stm32h7xx_dualcore_boot`), CM4 čeká na `IPC_MAGIC` v SRAM4.

## 4. SD karta — kolik systém obslouží

- **Řadič:** SDMMC1 (4-bit) + IDMA. Bez 1,8V přepínání (3,3V deska) = režim
  **High Speed 50 MHz → ~20–25 MB/s teoreticky, ~10–20 MB/s reálně** s FatFs.
- **Kapacita:** FatFs s `FF_FS_EXFAT=1` → **SDXC až 2 TB**; FAT32 (SDHC) do 32 GB
  (limit souboru 4 GB → pro logy stejně rotujeme). **Prakticky: jakákoli dnešní
  karta 8 GB–1 TB; doporučení 16–32 GB industrial** (endurance, teploty).
- **Rozpočet logování:** záznam ~64 B × 4 Hz ≈ **22 MB/den** → 32 GB ≈ **4 roky**
  nepřetržitě. Kapacita není limit; limitem je kvalita karty (výpadky napájení →
  f_sync po dávkách, žurnál nepotřebujeme).
- Card-detect (SDMMC1_DET) → hot-plug: mount/unmount v LogTasku, stav v UI/PAMĚŤ okně.

## 5. Ethernet + webserver — nativně, žádný modul

- **PHY na desce** (LAN87xx, RMII, 25 MHz zdroj) → `ETH` periferie + **lwIP na CM4**.
  100 Mbit, plná kontrola stacku, 0 Kč HW navíc. W5500 modul zamítnut (viz §0).
- **Známé H7 pasti (zapsat do checklistů):** deskriptory + RX buffery v D2
  ne-cache (CM4 D-cache nemá → odpadá), MPU na CM7 straně D2 nechat být; PHY
  adresa/strapy dle desky; ETH_RES pulz při initu.
- **Webserver:** statická SPA (jeden HTML+JS, tmavý styl jako displej) v CM4
  flash bance; data přes `/api/*` JSON z IPC snapshotu; push 1 Hz WebSocketem
  (nebo poll — jednodušší start). Logy z SD ke stažení přes HTTP (IPC file-read).

## 6. Standardizovaná komunikace — SCPI (jako laboratorní přístroje)

- **SCPI-99 přes TCP 5025** (raw socket — standard; VISA `TCPIP::…::5025::SOCKET`),
  parser **libscpi** (malá, C, embedded-friendly). Běží na CM4.
- **Tentýž parser i přes USB CDC** na CM7 (sdílený zdroják, dva transporty) —
  SCPI po USB od prvního dne; **USBTMC** (VISA bez socketu) jako fáze 2.
- mDNS discovery (`_scpi-raw._tcp`, hostname `gpsdo.local`) = LXI-lite; plné LXI později.
- Návrh stromu (jádro):
  ```
  *IDN?  *RST  *OPC?  SYSTem:ERRor?
  MEASure:FREQuency?              ; poslední měření [Hz, plné rozlišení]
  SENSe:FREQuency:GATE {0.1|1|10|100}   / GATE?
  SENSe:FREQuency:CHANnel {A|B}   / CHANnel?
  CALCulate:ADEV? <tau>           ; σy(τ) z pyramidy
  CALCulate:DRIFt?  CALCulate:OFFSet?
  SYSTem:GPS:STATus?  SYSTem:TEMPerature? <id>
  STATus:OPERation:CONDition?     ; lock/alarm bity
  MMEMory:CATalog?  MMEMory:DATA? <file>   ; logy z SD
  ```

## 7. Menší subsystémy

- **Encoder (PA8/PA9 TIM1 + PC13):** TIM1 encoder mode = HW kvadratura, 0 % CPU;
  UiTask čte CNT deltu při touch pollu. Funkce: krok GATE/kanál, listování okny,
  PC13 = potvrzení/mute alarmu. CM7. (.ioc: TIM1 encoder, PC13 EXTI ne — poll.)
- **J7 LED (6×):** mapování návrh — 1 PWR/RUN, 2 GPS LOCK, 3 DISC (PLL disc.),
  4 ALARM, 5 LOG (SD zápis), 6 LINK/ACT (ETH — stav z IPC). Zrcadlí stavové
  bity, obsluha v UiTask 2 Hz (levné). **Tlačítka na J7: potvrdit zapojení** —
  pak stejné debounce schéma jako PC13.
- **Beeper (piezo PH9):** driver hotový → dodat **AlarmMgr**: vzory (1× píp =
  event, 3× = ztráta locku, trvale přerušovaně = kritické), mute tlačítkem/SCPI,
  config perzistentní. Volá `beeper_set()` — oživí osiřelé API.
- **USB dodělání:** (1) SCPI přes stávající CDC — jen napojit parser; (2) fáze 2
  **USBTMC** třída (VISA-kompatibilní); (3) **MSC zamítnuto** — kolize vlastnictví
  FatFs s logováním; export souborů řeší HTTP (§5). CDC konzole zůstává.

## 8. Rozpočty a rizika

### CPU rozpočet CM7 — displej neutrpí (klíčová otázka návrhu)
Dnešní největší žrout je **20 Hz simulace velkého čísla**; reálná data = ~4 Hz
→ fáze 1 UiTasku **uleví** (~5× méně DMA2D/blit práce na čísle). Grafika je
z velké části HW (DMA2D/LTDC/dirty-rect) — nové tasky soutěží jen o CPU:

| Přírůstek CM7 | CPU | Pozn. |
|---|---|---|
| MathTask | <1 % | feed 4 Hz O(6); přepočty 1 Hz nad ~120 floaty, double FPU |
| LogTask (SD, Low prio) | <1 % | dávkované 64B záznamy, IDMA, f_sync ~2 s |
| Beeper ISR 1600 Hz | ~0,1 % | toggle |
| Encoder/J7 LED | ~0 % | HW čítač; GPIO 2 Hz |
| SCPI přes CDC | <0,5 % | jen při příkazu |
| IPC snapshot 10 Hz | ~0 % | memcpy ~1 KB → SRAM4 |
| **Σ nové** | **~2–3 %** | vs. ~10–15 % uvolněných zrušením 20 Hz sim |

Pojistky: síť celá na CM4 (nárazová zátěž se renderu nedotkne); LogTask POD
prioritou UI; pravidlo „nic blokujícího v UiTasku" trvá. Záložní páky: **-O2/
Release build** (největší), SPI SCK ↑, event-driven redraw, DMA2D IT-yield.
Měření reálně: UART `stats` / System Health CPU %.

### Paměť a rizika

- **CM4 flash (bank 2, 1 MB):** lwIP+httpd+SCPI+mDNS ≈ 200–350 KB + web assets
  (komprim. ~50–150 KB) → pohodlně.
- **CM4 RAM (D2 288 KB):** ETH deskriptory+buffery ~32 KB, lwIP heap 32–64 KB,
  stacky ~20 KB → rezerva. ⚠️ CM7 dnes v D2 drží jen `ram` test buffer (0x30000000,
  UART `ram write/read`) → **přesunout/zrušit** při předání D2 CM4.
- **Rizika:** oživení CM4 buildu (druhý CubeIDE projekt, dnes ladem); ETH errata
  H7 (deskriptory — mitigováno CM4/no-cache); jediný FatFs vlastník (vyřešeno
  designem); IPC verzování (magic+verze v hlavičce snapshotu).

## 9. Fázování (každá fáze samostatně testovatelná)

| # | Fáze | Jádro | Závislost |
|---|---|---|---|
| 1 | **MathTask + reálná data → headline** (+ GATE syntéza) | CM7 | protokol v2 (gap-free okna) pro plný GATE; základ i bez v2 |
| 2 | **SD logging** (SDMMC1+FatFs, LogTask, UI stav) | CM7 | .ioc SDMMC1 |
| 3 | **CM4 oživení + IPC** (snapshot+ringy, „hello" po UART/ITM) | oba | — |
| 4 | **ETH + lwIP + ping/DHCP** | CM4 | 3 |
| 5 | **SCPI 5025 + SCPI přes CDC** | CM4+CM7 | 3,4 |
| 6 | **Webserver (REST→SPA→WS)** | CM4 | 4 |
| 7 | Encoder, J7 LED/tlačítka, AlarmMgr+beeper | CM7 | kdykoli (nezávislé) |
| 8 | USBTMC, mDNS/LXI, HTTP download logů | oba | 5,6 |

## 10. Otevřené otázky (před implementací potvrdit)

1. **PHY typ + adresa** (LAN8720A vs 8742A; strapy MODE/PHYAD ze schématu/desky).
2. **J7 tlačítka** — jsou na J7, nebo jiný konektor/piny?
3. **SDMMC1 piny v .ioc** (PC8–PC12+PD2 standard? potvrdit + DET pin).
4. **Protokol v2 s FPGA** — gap-free okna jsou podmínkou plné GATE syntézy (fáze 1).
5. CM4 projekt: build config v repu je, ale nikdy neběžel nic reálného — ověřit boot gate.
