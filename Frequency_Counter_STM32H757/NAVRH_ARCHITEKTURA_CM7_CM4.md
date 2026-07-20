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

## 11.5 🟡 Vlastnictví D2 SRAM je dnes u CM7 — vyřešit před předáním

§8 to zmiňuje jednou větou, ale je to konkrétnější:

- `CM7/STM32H757BITX_FLASH.ld` má `RAM_D2 (xrw) : ORIGIN = 0x30000000, LENGTH = 288K`,
- `freertos_task_uart.c` používá `RAM_BASE 0x30000000` pro UART `ram write/read`.

Než CM4 dostane D2 (ETH deskriptory + lwIP heap), je potřeba **rozdělit D2 v obou linker
skriptech** a test buffer buď přesunout, nebo zrušit. Jinak si jádra tiše přepíšou paměť —
a bude to vypadat jako náhodná chyba sítě.

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
