# NÁVRH: Architektura CM7/CM4 + chybějící subsystémy — k diskusi

> 🔄 **Revize 2026-07-20 (firmware v0.4.0) — čti §11 na konci.** Od sepsání návrhu se část
> předpokladů změnila (datalog už existuje, USB CDC běží) a v návrhu IPC je **kritická chyba**
> (sdílená paměť by se na CM7 kešovala). §11 to opravuje; text §1–§10 je jinak platný.

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

> Aktualizováno 2026-07-20 podle §11 (LogTask zrušen — datalog už existuje).
> **Tato sekce je autoritativní**; §11 je revizní protokol s odůvodněním.

### ⚠️ Nejdřív jedno rozlišení: doména ≠ jádro

`D1/D2/D3` jsou **napájecí a hodinové domény**, ne vlastnictví jádra. **CM7 může ovládat
periferie v D2 a naopak** — přiřazení k jádru je věc kontextu v CubeMX, ne fyzického umístění.
ETH proto nedáváme na CM4 „protože je v D2", ale ze dvou konkrétních důvodů:
(a) jeho DMA deskriptory a buffery leží v **D2 SRAM**, kterou CM4 čte **bez D-cache** → odpadá
celá třída koherenčních pastí, na které se na H7 běžně naráží; (b) **izolace** — zaplavení
sítě nebo pád stacku se nesmí dotknout měření.

### CM7 (480 MHz, double FPU, I/D cache) = **PŘÍSTROJ**

Měření, matematika, UI, lokální I/O, úložiště. Vlastní displej, SDRAM, DMA2D, veškerou flash.

| Task | Prio | Stack | Role | Stav |
|---|---|---|---|---|
| defaultTask | Normal | 1536 B | GPS drain, RTC, persist nastavení, **datalog**, alarmy, `watchdog_supervise`, USB pump · **+ IPC servis** (publikuj snapshot, aplikuj cmd ring) | dnes běží, IPC nový |
| UartTask | Normal | 4 KB | konzole (USB CDC / USART1) a její příkazy · **+ SCPI parser** (týž zdroják jako na CM4) | dnes běží |
| FpgaTask | Normal | 2 KB | SPI2 poll FPGA 20 Hz, protokol v1→v2 | dnes běží |
| **MathTask** | Normal | — | matematika měření: gap-free Σhran/ΣΔt, syntéza GATE 0,1/1/10/100 s, klouzavé statistiky, **ADEV pyramida z reálných dat**, drift, LSQ regrese, korelace s teplotou OCXO. Plní UI i IPC snapshot. **`double` FPU — proto CM7, CM4 má jen single.** | **nový**, blokován #2 |
| UiTask | BelowNormal | 8 KB | render displeje, touch, backlight, auto-dim · **+ encoder** (TIM1 CNT delta při touch pollu) · **+ J7 LED** (zrcadlí stav 2 Hz) · **+ J7 tlačítka** | dnes běží |
| I2C4Task (Sensors) | Low | 1536 B | TMP117 ×3, ADS1115, ADC3, status Si5356 | dnes běží, beze změny |
| — (modul) | — | — | **AlarmMgr**: vzory beeperu, mute, konfigurace | nový |

**Periferie CM7:** LTDC + DSI + DMA2D (displej), FMC (SDRAM 32 MB), QUADSPI (W25Q 64 MB),
SPI2 (FPGA), I2C1 + I2C4 (senzory, panel, touch), USART1 (GPS), USB OTG FS (CDC konzole),
ADC3, RTC + BKP, TIM7 (beeper), IWDG1 · **plánované:** SDMMC1 (SD karta), TIM1 (encoder).

**Paměť CM7:** FLASH **bank 1** (`0x08000000`, 1 MB) · AXI SRAM D1 · DTCM/ITCM ·
SDRAM 32 MB (`0xC0000000`) · W25Q 64 MB (QSPI).

> ⚠️ **Veškerá flash (W25Q i SD) zůstává výhradně CM7.** Síť na ni nesahá přímo — přes IPC
> pošle žádost a CM7 ji vyřídí ve svém tempu. Důvod: `w25q wait_ready()` blokuje při erase
> desítky až stovky ms (lekce z v0.4.0, viz `CLAUDE.md` watchdog) a jediný vlastník úložiště
> je zároveň to, co dělá zbytečným „sdílený FatFs" jako riziko.

### CM4 (240 MHz, single FPU, bez cache) = **KONEKTIVITA**

Dnes jádro jen bliká — 240 MHz leží ladem. Dostane celý síťový stack a vzdálená rozhraní.

| Task | Role |
|---|---|
| **NetTask** | lwIP + ETH driver (RMII, deskriptory v D2), DHCP/statická IP, stav linky |
| **ScpiTask** | **SCPI server na TCP 5025** (raw socket = standard lab. přístrojů, VISA `TCPIP::…::5025::SOCKET`), parser `libscpi`. GET čte IPC snapshot, SET jde do cmd ringu. |
| **HttpTask** | webserver: statická SPA v CM4 flash, **REST/JSON** (`/api/meas`, `/api/status`), push 1 Hz. Stahování logů přes IPC → **`datalog_read_back()`**, NE přes FatFs (funguje pak stejně pro W25Q i SD). |
| **SvcTask** | mDNS (`gpsdo.local`, LXI-lite), SNTP cross-check času, volitelně telnet |

**Periferie CM4:** ETH (RMII + PHY LAN87xx). Nic dalšího.

**Paměť CM4:** FLASH **bank 2** (`0x08100000`, 1 MB — lwIP+httpd+SCPI+mDNS ≈ 200–350 KB
+ web assets) · **D2 SRAM** (`0x30000000`, 288 KB — ETH deskriptory ~32 KB, lwIP heap 32–64 KB,
stacky ~20 KB). ⚠️ D2 dnes patří v linkeru CM7 → viz #22.

### Hranice mezi jádry

| Prostředek | Kdo |
|---|---|
| **SRAM4** (D3, `0x38000000`, 64 KB) | **sdílené — JEN pro IPC.** Snapshot (seqlock, CM7→CM4) + cmd/resp ringy. ⚠️ **Musí být non-cacheable (MPU region), viz #19.** |
| **HSEM** | boot gate CM4 |
| všechno ostatní | **nesdílí se** — žádné sdílené periferie, žádný sdílený FatFs, žádné křížové HAL handly |

**Co teče mezi jádry:** jen malý, verzovaný stav — snapshot měření a stavu (~1 KB) směrem
k CM4, příkazy (GATE, RUN/STOP, kanál, log on/off) směrem k CM7. Nic víc.

## 3. IPC CM7↔CM4 — sdílená SRAM4 (D3, 64 KB, 0x38000000)

Bez OpenAMP — jednoduchý, auditovatelný protokol (styl FPGA rámce):

| Blok | Směr | Mechanismus |
|---|---|---|
| **`ipc_snapshot_t`** (~1 KB) | CM7→CM4 | **seqlock**: `seq++` (lichá=zápis), data, `seq++`; CM4 čte, při změně seq opakuje. Obsah: freq×1e5 (/4,/16, zvolený), stats (σ@τ, drift, offset, ADEV body), GPS (fix/sat/čas), senzory souhrn, health, alarmy, uptime, verze. |
| **cmd ring** (16×64 B) | CM4→CM7 | SCPI/web SETy: GATE, RUN/STOP, CHAN, log on/off… defaultTask aplikuje, odpověď do resp ringu (echo id + status) |
| **resp ring** (16×64 B) | CM7→CM4 | odpovědi + async eventy (alarm) |
| **file-read okno** (4 KB) | CM7→CM4 | HTTP download logů: CM4 žádá (index záznamu), CM7 plní přes `datalog_read_back()` — NE přes FatFs (viz §11.3) |

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
- Card-detect (SDMMC1_DET) → hot-plug: mount/unmount v SD backendu datalogu, stav v UI/PAMĚŤ okně.

## 5. Ethernet + webserver — nativně, žádný modul

- **PHY na desce = `LAN8742A`** (potvrzeno ze schématu list 4/7, 2026-08-06), RMII,
  25 MHz zdroj → `ETH` periferie + **lwIP na CM4**. 100 Mbit, plná kontrola stacku,
  0 Kč HW navíc. W5500 modul zamítnut (viz §0). Detail zprovoznění = §6a.
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

## 6a. Zprovoznění a nasazení SCPI + Ethernetu (detail)

> Rozpracování §5/§6 se zaměřením na **clocking, autonegotiaci rychlosti a
> velikosti paketů**. Váže se na žebřík M9–M12 (viz §11 a `STATUS.md`). Přidáno
> 2026-08-06. ⚠️ Zlaté pravidlo trvá: **nic ze sítě nesmí předběhnout #2** (reálná
> data z FPGA) — SCPI/web vracející simulaci je horší než mlčení.

### 6a.1 Hardware a clocking (LAN8742A, RMII)
- **PHY = `LAN8742A` (U4), předpokládaný režim „REF_CLK OUT".** 25 MHz **oscilátor**
  (ECS-2520MV, X1 — single-ended, XTAL2 = NC) → `XTAL1/CLKIN` (pin 5), PHY interní PLL
  vyrobí **50 MHz RMII** na `INT/REFCLKO` (pin 14, R21 33 Ω) → zpět do MAC `ETH_REF_CLK`.
  Magnetika **TG110-E050N5xx** (TR1), RJ45 **J8**. Není potřeba externí 50 MHz oscilátor —
  dělá ho PHY. ⚠️ **Režim REF_CLK OUT závisí na strapu `nINTSEL`** — ověřit ze schématu,
  že je nastaven na výstup ref. hodin (jinak MAC nedostane RMII clock a link nenaběhne).
- **Piny (RMII):** ETH_TXD0/1, TX_EN, RXD0/1, CRS_DV, MDC, MDIO, REF_CLK, ETH_RES(nRST),
  ETH_INT. Deskriptory + RX/TX buffery v **D2 SRAM** — CM4 **nemá D-cache** → žádná
  cache maintenance (hlavní důvod, proč ETH patří na CM4, ne na CM7).
- ⚠️ **PHY adresa (PHYAD):** strapy R22–R26 (10k) na RXD0/MODE0, RXD1/MODE1,
  CRS_DV/MODE2, RXER/PHYAD0 — **dopočítat ze skutečných pull-up/down a MUSÍ sedět
  s HAL/lwIP konfigurací** (jinak MDIO scan PHY nenajde). Bezpečně: init udělá
  **scan adres 0–31** a najde první živou (čtení PHY ID = `0x0007C130/C131` pro
  LAN8742A) — nezávislé na strapu.

### 6a.2 Bring-up sekvence (M9: link + DHCP + ping)
1. **Reset PHY:** pulz `ETH_RES` (nRST) low ≥100 µs, po náběhu ≥ ~a pár ms než PHY odpoví na MDIO.
2. **SMI/MDIO:** scan adres → čtení **PHY ID (reg 2/3)** = ověření `LAN8742A`. Uložit PHYAD.
3. **Autonegotiace** (viz 6a.3) → počkat na dokončení → přečíst výsledek.
4. **MAC config z výsledku** (Speed/Duplex) → `HAL_ETH_SetMACConfig`.
5. **DMA deskriptory** (D2) + start RX/TX, lwIP `netif` up.
6. **DHCP** (default) nebo statická IP (fallback po ~5 s bez DHCP).
7. **Ping** z PC projde = M9 splněno. **Flood test** `ping -f` = test izolace jader:
   displej na CM7 nesmí zpomalit, žádný watchdog reset, CPU % CM7 se nehne.

### 6a.3 Autonegotiace rychlosti (IEEE 802.3 clause 28)
- **Princip:** PHY inzeruje své schopnosti (registr **ANAR, reg 4**) — 10/100,
  half/full, případně 802.3x PAUSE. Link partner (switch) totéž přes FLP burst,
  výsledek se **rozhodne prioritně** (100FD > 100HD > 10FD > 10HD).
- **MAC autoneg NEDĚLÁ** — jen se řídí PHY. Po `AUTONEG COMPLETE` (BMSR reg 1 bit5)
  přečíst **rozlišený režim**: LAN8742A má vendor **reg 31 (PSCSR)** bity HCDSPEED
  → přímý „100M full" apod.; HAL `lan8742.c` to obalí do
  `LAN8742_GetLinkState()` → např. `LAN8742_STATUS_100MBITS_FULLDUPLEX`.
- **Doporučení:** inzerovat **plný rozsah (10/100, half/full)** a nechat rozhodnout
  link partner — na switchi typicky vyjde **100BASE-TX FD**. Forced 100FD (bez autoneg)
  jen pro debug SI. **Flow control (PAUSE) inzerovat** — při stahování logu s malými
  RX buffery na CM4 pomůže RX PAUSE proti přetečení DMA ringu.
- **Link change:** hlídat přes `ETH_INT` (PHY interrupt) nebo 1 Hz poll BMSR;
  při změně re-číst rychlost a přenastavit MAC (10↔100). Stav → IPC snapshot → J7 LINK LED.

### 6a.4 Velikosti paketů a jejich účel
Rychlost (10/100) ovlivňuje jen **propustnost**, ne velikosti — MTU zůstává 1500 vždy.

| Vrstva | Velikost | Účel |
|---|---|---|
| **Ethernet MTU** | **1500 B** (rámec 1518) | standard; **žádné jumbo** — switch nemusí umět, RAM na CM4 drahá, náš provoz to nepotřebuje |
| **TCP MSS** | **1460 B** (1500−20 IP−20 TCP) | lwIP `TCP_MSS`; největší segment bez IP fragmentace |
| **RX/TX DMA buffer** | **≥1524 B** × N (4/4) | jeden plný rámec/deskriptor, back-to-back příjem bez CPU; D2 SRAM |
| **TCP okno (`TCP_WND`)** | ~2–4× MSS (2920–5840 B) | kompromis propustnost ↔ RAM na CM4 |
| **lwIP `PBUF_POOL_BUFSIZE`** | ≥ plný rámec | RX buffering; počet poolů dle zátěže |

**Aplikační payloady — proč které velikosti:**
- **SCPI raw 5025:** drobné (`*IDN?`→~7 B dotaz / ~60 B odpověď; `MEAS:FREQ?`→~20 B).
  Hluboko pod MSS → jeden malý segment, **latency-bound**. ⚠️ **`TCP_NODELAY`
  (vypnout Nagle)** — jinak se interaktivní odpovědi zdrží ~200 ms. Účel: odezva jako lab. přístroj.
- **1 Hz status push / WebSocket:** JSON snapshot ~200–500 B → 1 segment. Držet
  IPC snapshot **< MSS**, ať se vejde do jednoho segmentu. Účel: živý dashboard.
- **Stahování logu (`datalog_read_back()`):** **bulk** — chceme **plné 1460 B
  segmenty** + slušné okno. 32 B záznamy → ~45 záznamů/segment. Účel: efektivní
  přenos, minimum režie na paket. (Čtení přes datalog abstrakci — funguje pro W25Q i SD.)
- **Web SPA:** statická z CM4 flash, plné MSS segmenty; ideálně **gzip**
  předkomprimovat = méně paketů, méně flash.

### 6a.5 Nasazení
- **MAC adresa** z 96-bit unique ID MCU (locally-administered bit) → stabilní unikát bez EEPROM.
- **DHCP** default + **statická fallback**; **mDNS `gpsdo.local`** (`_scpi-raw._tcp`) = LXI-lite.
- **USB CDC transport SCPI jde hned** (`USE_USB_CDC_CONSOLE=1` běží) — jen napojit
  `libscpi` parser; síťová varianta 5025 sdílí tentýž zdroják (GET čte IPC snapshot,
  SET → cmd ring). Test: `*IDN?`/`MEAS:FREQ?` přes USB i TCP **musí dát shodnou hodnotu i s displejem**.
- ⚠️ **CM4 stejně jako CM7: nic blokujícího déle než ~10 ms** v síťovém tasku
  (stejné pravidlo jako `wait_ready` incident) — lwIP callbacky nesmí spinovat.

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
| datalog + SD backend (defaultTask) | <1 % | dávkované 64B záznamy, IDMA, f_sync ~2 s |
| Beeper ISR 1600 Hz | ~0,1 % | toggle |
| Encoder/J7 LED | ~0 % | HW čítač; GPIO 2 Hz |
| SCPI přes CDC | <0,5 % | jen při příkazu |
| IPC snapshot 10 Hz | ~0 % | memcpy ~1 KB → SRAM4 |
| **Σ nové** | **~2–3 %** | vs. ~10–15 % uvolněných zrušením 20 Hz sim |

Pojistky: síť celá na CM4 (nárazová zátěž se renderu nedotkne); zápis úložiště POD
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
| 2 | **SD backend do datalogu** (SDMMC1 + `datalog_sd.c`) | CM7 | .ioc SDMMC1 |
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

---

# 11. Revize 2026-07-20 (firmware v0.4.0)

Návrh vznikl před v0.4.0. Tahle sekce ho **neruší** — opravuje jednu kritickou chybu,
srovnává ho s tím, co mezitím vzniklo, a doplňuje tři věci, které v něm chyběly.

## 11.1 🔴 KRITICKÉ: IPC přes SRAM4 je v navrženém tvaru ROZBITÉ

§3 navrhuje seqlock nad sdílenou SRAM4 (`0x38000000`). **Tak jak je to popsané, fungovat nebude.**

**Proč:** `MPU_Config()` v `main.c` konfiguruje jen **region 0** (`0xC0000000`, framebuffery, WT)
a **region 1** (`0xC0400000`, scratch, WBWA). Pro `0x38000000` **žádný region neexistuje**, takže
platí výchozí mapa ARMv7-M: oblast `0x20000000–0x3FFFFFFF` = *Normal, cacheable, write-back,
write-allocate*. Jinými slovy **CM7 si sdílenou paměť nakešuje**:

- zápisy CM7 uvíznou v D-cache a CM4 (bez cache, čte skutečnou SRAM) je **neuvidí**,
- CM7 bude číst zastaralé řádky s daty, která mezitím zapsal CM4.

Seqlock to nezachrání — kešuje se i samotné `seq`, takže CM7 může donekonečna číst starou
hodnotu a myslet si, že se nic nezměnilo.

**Oprava — přidat MPU region pro SRAM4 jako NEKEŠOVATELNÝ:**

```c
/* Region 2: IPC okno CM7<->CM4 (SRAM4, D3). NESMI byt kesovatelne — CM4 nema
 * D-cache a cetl by jinou pamet nez do ktere pise CM7. Shareable + non-cacheable. */
MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
MPU_InitStruct.BaseAddress      = 0x38000000;
MPU_InitStruct.Size             = MPU_REGION_SIZE_64KB;
MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;
MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
```

⚠️ **Write-Through by NESTAČIL.** WT sice pošle zápisy CM7 do paměti (CM4 by je viděl), ale
**čtení** CM7 by pořád chodilo z cache → data od CM4 by CM7 neviděl. IPC je obousměrné,
takže jedině **non-cacheable**.

> Tohle je stejná třída chyby, na kterou projekt už jednou narazil u DMA2D (viz „Cache
> koherence" v `CLAUDE.md`). Tam se řeší explicitní invalidací; tady je čistší MPU, protože
> IPC blok je malý a přistupuje se k němu často.

## 11.2 🔴 Seqlock potřebuje bariéry, jinak ho přeuspořádá kompilátor

§3 popisuje `seq++` → data → `seq++`. Bez bariér to **není korektní ani po opravě 11.1** —
kompilátor i procesor smí zápisy přeuspořádat a CM4 uvidí nová data pod starým `seq`
(nebo naopak). Minimální správná podoba:

```c
/* Zapisovatel (CM7) */
s->seq++;            /* licha = zapis probiha */
__DMB();             /* seq viditelne PRED daty */
memcpy(&s->data, &snap, sizeof snap);
__DMB();             /* data viditelna PRED zvysenim seq */
s->seq++;            /* suda = konzistentni */

/* Ctenar (CM4) */
do {
    a = s->seq; __DMB();
    memcpy(&out, &s->data, sizeof out);
    __DMB(); b = s->seq;
} while ((a & 1u) || a != b);
```

Pole `seq` musí být `volatile uint32_t`. Bez `volatile` smí kompilátor čtení ve smyčce
vyhodit úplně.

## 11.3 Co se od sepsání návrhu změnilo

| Návrh říká | Realita v0.4.0 | Důsledek pro plán |
|---|---|---|
| §2: **LogTask** jako nový task pro SD/FatFs | **Datalog už existuje** (`datalog.c/h`) — běží v defaultTask na CM7, zapisuje do W25Q, 32 B / 10 s, ~600 dní | LogTask **není potřeba jako nový task**. SD = jen **další backend** (`datalog_backend_t`), rozhraní je hotové a `datalog_sd.c` má připravený 5bodový plán. |
| §5: HTTP download logů ze SD přes „IPC file-read" nad FatFs | Datalog má `datalog_read_back(n)` — jednotné čtení bez ohledu na úložiště | File-read okno by **nemělo sahat na FatFs**, ale volat `datalog_read_back()`. Funguje pak stejně pro W25Q i SD a odpadá „jediný vlastník FatFs" jako riziko. |
| §6: SCPI přes USB CDC „fáze 2" | **CDC konzole běží** (`USE_USB_CDC_CONSOLE=1`), USART1 je volný pro GPS | Transport je hotový, zbývá jen napojit parser. SCPI po USB je levnější, než návrh předpokládal. |
| §9 fáze 2: „SD logging" | Logování běží, chybí jen médium | Fáze 2 se smrskla na „dopsat SD backend + SDMMC1 v .ioc". |
| §8: selftest neuveden | Selftest **7/7** včetně datalogu | Přidat kontrolu IPC (magic+verze) jako 8. test, až vznikne. |

**Nemění se:** rozdělení „CM7 = přístroj, CM4 = konektivita", ETH+lwIP na CM4, SCPI na 5025,
CPU rozpočet i fázování §9 od fáze 3 dál.

## 11.4 🟡 Chybí: co když se zasekne CM4

Návrh řeší, že CM4 **nenaběhne** (`g_cm4_absent`, boot gate), ale ne že **přestane odpovídat
za běhu**. Dnešní watchdog (IWDG1) hlídá jen CM7 a jeho tasky.

Doplnit:

- **IWDG2** — CM4 má vlastní nezávislý watchdog, použít ho stejně jako IWDG1 na CM7.
- **Heartbeat v IPC** — CM4 inkrementuje čítač ve snapshotu (nebo ve vlastním poli); CM7 ho
  sleduje a při zamrznutí zobrazí `SYS !` (amber, degradovaný provoz — přístroj měří dál,
  jen nemá síť). **CM7 nesmí kvůli mrtvému CM4 resetovat sám sebe** — konektivita je
  doplněk, ne podmínka měření.
- Symetricky: CM4 pozná mrtvý CM7 podle zamrzlého `seq` a přestane vydávat data přes SCPI/HTTP,
  místo aby servíroval staré hodnoty jako aktuální.

Rozšíření `stall:<task>` black-boxu (viz `CLAUDE.md`, watchdog) o `stall:CM4` je přímočaré.

## 11.5 ✅ Vlastnictví D2 SRAM rozděleno (2026-08-09)

Dřív si D2 (288K) nárokovala **celá obě jádra**: CM7 `RAM_D2 @0x30000000/288K`, CM4
`RAM @0x10000000/288K` (= tentýž fyzický D2, CM4-alias). CM7 tam nic nelinkoval, jen
diagnostický `ram write/read` (`RAM_BASE 0x30000000`, ~78 KB od 0x30001000). Kolize by
udeřila, až CM4 dostane ETH (lwIP heap + deskriptory v D2).

**Rozděleno bankově zarovnaně** (D2 = SRAM1 128K @0x30000000 + SRAM2 128K @0x30020000 +
SRAM3 32K @0x30040000):
- **SRAM1 (128K) → CM7**: `RAM_D2 @0x30000000 LENGTH 128K` (oba CM7 linkery). Diagnostický
  `ram` test (0x30001000, ~78 KB) se do SRAM1 vejde; CM7 do RAM_D2 jinak **nic nelinkuje**.
- **SRAM2+3 (160K) → CM4**: `RAM @0x10020000 LENGTH 160K` (CM4 FLASH.ld — **sjednoceno s CM4
  `_RAM.ld`**, které tuto konfiguraci ST-tooling už vygeneroval; `RAM_EXEC @0x10000000/128K`
  = SRAM1 se pro flash build nepoužívá). `_estack` vrchol `0x10048000` beze změny.

**Validace (bez HW):** všechny 4 linkery parsují; **CM4 relink novým skriptem OK** (sekce
relokované na 0x10020000, footprint 1,6 KB / 160K, žádné přetečení); CM7 map = do D2
nelinkuje nic; regiony disjunktní (CM7 fyz. [0,128K), CM4 fyz. [128K,288K)).
⚠️ **HW-gated (nevalidovatelné bez flashe bank2):** CM4 skutečně bootující a používající
0x10020000; D2 SRAM2/3 clock enable **z CM4 strany** (per-core RCC, viz §11.6).

## 11.6 🟡 Dvě pasti, na které projekt už doplatil

- **Per-core hodiny periferií.** Na H7 má každé jádro vlastní sadu RCC enable bitů
  (`RCC_C1_*ENR` / `RCC_C2_*ENR`). Periferie přiřazená CM4 musí mít hodiny zapnuté **z CM4**.
  CubeMX to řeší přiřazením kontextu, ale při ručních úpravách (kterých je v tomhle projektu
  hodně — `MX_I2C1_Init` v USER CODE, `fpga_freq_init`, …) je to snadné minout.
- **Žádné blokující spiny v taskech s heartbeatem.** Lekce z v0.4.0: `w25q wait_ready()`
  spinoval až 1 s a vyhladověl UiTask. Pro CM4 to platí stejně — **lwIP ani SCPI nesmí
  spouštět operace nad W25Q/SD přímo**. Flash zůstává výhradně CM7 a přes IPC se posílá jen
  žádost, kterou CM7 vyřídí ve svém tempu.

## 11.7 Doporučené pořadí (upravené §9)

Fáze 1 (MathTask + reálná data) je pořád **blokovaná #2** — SPI link. Do té doby dává smysl
dělat jen věci, které na měření nezávisí:

| # | Fáze | Blokováno? |
|---|---|---|
| 0 | **MPU region pro SRAM4 + seqlock s bariérami** (11.1, 11.2) | ne — udělat dřív než cokoli IPC |
| 3 | CM4 oživení + IPC „hello" + heartbeat (11.4) | ne |
| 2′ | SD backend do `datalog` (SDMMC1 v .ioc) | ne |
| 7 | Encoder, J7 LED, AlarmMgr | ne |
| 1 | MathTask + reálná data → headline | **ano (#2)** |
| 4–6, 8 | ETH, SCPI, web, USBTMC | až po 3 |

⚠️ Ani jedna z těchto fází by neměla předběhnout **#2**. Síť, SCPI ani webserver nemají co
publikovat, dokud přístroj neměří — jinak se jen vybuduje víc vrstev nad simulovaným číslem.

## 11.8 ✅ Revize rozdělení úkolů jader (kritická validace 2026-08-09)

Otázka: nepřesunout I2C/beeper/… na CM4 kvůli vybalancování? **Verdikt: NE — rozdělení nechat.**
Řídí ho (v pořadí důležitosti), NE „vytížení CPU":

1. **🔴 Měření musí přežít smrt CM4** (§11.4). Cokoli měření-kritického (**FPGA SPI2, GPS, senzory,
   RTC**) MUSÍ zůstat na CM7 — na CM4 by pád konektivity oslepil přístroj.
2. **Lokalita dat** — smyčka naměř→spočítej→zobraz je nejtěsnější na jednom jádru (CM7).
3. **Pevné vazby**: displej = D1 (DMA2D/LTDC/framebuffery) → nelze mimo CM7; statistika = **double FPU**
   (jen M7; M4F je single-precision).
4. CM4 = **nezávislá síťová zátěž** (ETH/SCPI-TCP/web), jejíž data neживí měření (klienti čtou snapshot);
   ETH DMA navíc těží z **D2-bez-cache**.

**Steelman „přesunout" — proč to padá:** I2C senzory jsou 2 Hz (levné) + měření-kritické (teplotní komp.) →
přesun skoro nic neušetří a oslepí přístroj při pádu CM4. Beeper = ISR + alarm logika na CM7 (potřebuje
měřicí stav) → split je složitější. **CM7 hrdlo = renderování (D1, nepřesunutelné)** → přesun periférií
neuleví; měřicí tasky jsou navíc už nad renderem v prioritě (scheduler je izoluje i na jednom jádru).
→ **CM4 není load-balancer dnešního přístroje, je rezerva pro síťovou fázi.**

**Implementovaný důsledek validace — datový kontrakt.** Aby byl split *realizovatelný* (SCPI/web na CM4),
musí snapshot nést **kompletní instrument-state**, protože CM4 nemá `g_sensors`/`g_calib`. Rozšířeno
(**`IPC_VERSION` 1→2**): všechny teploty (OCXO/deska/MCU), napětí (12V/5V/VREF/VBAT/Vc), RF mV + **AD8307
kalibrace** (aby CM4 spočítalo dBm), Si5356 stav, kanál. Bez toho by SCPI dotazy `MEAS:VOLT?`/`SYST:TEMP?`/
`MEAS:POW?` po přesunu na TCP praskly (dnes fungují jen protože SCPI běží na CM7 s přímým přístupem).
**Math cfg config sync HOTOVO (v3):** čtení = cfg mirror ve snapshotu (`math_m/b/null_ref/lim_lo/hi` + flagy →
CM4 obslouží `CALC:` readbacky + `CALC:DATA?/LIM?`); zápis = cmd ring `IPC_CMD_CFG_SET` (`ipc_cfg_apply` na
CM7 → `g_meas_cfg`, mirror `scpi_calc_set`, commit jen při reálné změně). `ipc_cmd_t` rozšířen o `double argd`
(aby `lo/hi/m/b` nesly plný rozsah). **g_meas_cfg zůstává single-source-of-truth na CM7** (CM4 jen navrhuje
změny přes ring, CM7 je jediný zapisovatel). **Zbývá:** GATE/RUN/CHAN/LOG dispatch (dozraje se SCPI/web na CM4).

## 11.9 🔍 Audit komunikace CM7↔CM4 (2026-08-10) — nálezy + zlepšení

Kritický audit IPC (snapshot/ring/heartbeat/notifikace). **Implementováno:**

- **🔴 A. CM4 nedetekoval zamrzlý CM7 → HOTOVO.** `ipc_cm4_read` vracel poslední snapshot jako
  platný, i když CM7 zamrznul (seqlock zůstal konzistentní, jen `seq` se přestal měnit) → CM4 by
  servíroval **stará data jako aktuální** (SCPI/web). Nový `ipc_cm4_cm7_alive(now_ms)` sleduje růst
  `seq` (>2 s beze změny = mrtvý CM7); CM4 smyčka teď snapshotu nedůvěřuje, když CM7 nežije. Symetrické
  k CM7-straně `ipc_cm4_alive`. Naplňuje §11.4 („CM4 pozná mrtvý CM7 podle zamrzlého seq").
- **🟡 B. Snapshot podvzorkovával měření → HOTOVO (event-driven).** Pevný 2 Hz throttle vs FPGA ~4 měření/s
  (gate 0,25 s) → SCPI/web klient viděl každé druhé měření, data až 500 ms stará. `ipc_publish` teď publikuje
  **na každé nové měření** (`seq_meas` se změní) NEBO ≥2 Hz (heartbeat, aby `seq` rostl pro liveness A).
  Latence měření → snapshot je teď ~jeden defaultTask tik. CPU dopad ~0 (NAVRH §8 rozpočtoval i 10 Hz).
- **🟡 C. cfg kopie uvnitř seqlocku → HOTOVO.** `taskENTER_CRITICAL(g_meas_cfg)` bylo mezi `publish_begin`/
  `end` → prodlužovalo seq-odd okno (maskovalo IRQ) = víc retry na CM4. Přesunuto PŘED `publish_begin`.
- **🟡 H. `ipc_service` plýtval při prázdném ringu → HOTOVO (2. audit pass).** Běží 100 Hz z defaultTasku;
  i s **prázdným cmd ringem** (běžný stav — CM4 posílá config zřídka) dělal každý tik 2× kopii `g_meas_cfg`
  (IRQ-off) + `fpga_freq_get_last` (IRQ-off) + memcmp naprázdno. Přidán **fast-path `head==tail` early-out**
  (SPSC prázdný check = porovnání dvou volatile čítačů) → idle cesta je teď prakticky zdarma. (Nález z
  kritického re-review vlastních změn A–C.)

**Zbývá (design pozn., nízká priorita / HW):**
- **🟡 D. CM4 IPC servis svázaný s LED smyčkou (~800 ms)** → pomalé zpracování resp ringu. Dnes CM4 nic
  nedělá; při reálné práci (SCPI/web) mít proper loop (50–100 Hz servis nezávislý na LED).
- **🟢 E. Notifikace = polling.** HSEM IRQ (CM7 budí CM4 na nový snapshot, CM4 budí CM7 na cmd) = nižší
  latence + CM4 může spát (WFI, power). Až bude potřeba; polling je dle §11.4 záměrný start.
- **🟢 F. File-read okno pro download logů (#26)** — není; SRAM4 je z ~99 % volná (680 B / 64 KB) → místo je.
- **🟢 G. Gap-free měřicí FIFO** (Ω-counter/MDA #48/#27) — snapshot je STAV, ne proud; gap-free potřebuje
  FIFO ve sdílené paměti. Inherentní limit snapshot modelu; řeší se až s MathTaskem.

**Obstálo (bez zásahu):** seqlock korektnost (DMB + bounded retry + per-read magic), SPSC ringy, velikosti
rámců/ringů, SET→readback latence (zmírněná optimistic local update v `scpi_src.set_cfg`).
