# NÁVRH: SPI protokol v3 (STM32H757 ↔ Tang Nano 9K) — nová deska, dva kanály

Stav: **NÁVRH k odsouhlasení FPGA stranou. Nic z toho zatím není v kódu ani v bitstreamu.**

> 🔴 **v3 NENÍ psaná na zelené louce.** Protokol **v2 už existuje a JE IMPLEMENTOVANÝ**
> (bitstream FW `0x0201`, 2026-07-10 — viz [`FPGA_PROTOCOL_V2_NAVRH.md`](FPGA_PROTOCOL_V2_NAVRH.md)):
> 128B rámec, gap-free okna, S1/S2 akumulátory pro Λ/Ω regresi, code-density histogram,
> `SET_CONFIG`. **Migrační checklist na STM straně je ale celý neodškrtnutý** — v2 se
> na našem konci nikdy nezačala používat.
>
> v3 proto **staví na v2 a mění jen to, co si vynutila nová deska.** Co ve v2 funguje
> (rámec, CRC, ACK/FRESH, okna, `SET_CONFIG`, histogram) se **nepřepisuje**.

Vstupní zadání: [`../citac_zadani_predavaci.md`](../citac_zadani_predavaci.md).
Smluvní tabulky (konektor J3, I²C mapa, pinout Tang Nano) → [`docs/HW_REFERENCE.md`](docs/HW_REFERENCE.md).

---

## 1. Proč v3 (co si vynutila nová deska)

| Změna HW | Důsledek pro protokol |
|---|---|
| **Dva symetrické kanály** `CH_A` (PIN25) / `CH_B` (PIN27) místo dvou odboček jednoho děliče | `freq16_x100000` a `status2/DIV16_ERR` končí; rámec musí nést **dvě rovnocenné sady** |
| **Carry-chain TDC** místo 4fázového vernieru ze Si5356 | `phase_status` (present/fine_seen) končí; místo něj stav kalibrace binů |
| **Předdělička ÷10 (`MC12080`) v modulu**, přepínaná relé ze STM32 | násobitel se musí **deklarovat**, ne hádat — viz §4 |
| **`GPS_1PPS` do FPGA** (PIN33, carry chain C) | nové pole `time_error_ps` — **bez něj nemá smyčka GPSDO vstup** |
| **PSRAM 8 MB, gap-free razítkování do 100 kHz** | 128B rámec na to nestačí → **bulk transakce**, §5 |
| Zákaz pollingu / požadavek na tichý dotaz | **příznak „mám data" na MISO**, §3 |

---

## 2. Fyzická vrstva — 4 vodiče, nic víc

`MISO` / `MOSI` / `SCK` / `CS`. **`Data_RDY` se nepřidává** (rozhodnuto 2026-08-30).

- SPI mode 0 (CPOL=0, CPHA=0), MSB first, 8-bit, CS active-low. STM32 = master.
- STM32 piny: `MOSI`=PB15, `SCK`=PI1, `MISO`=PI2, **`CS`=PB12 (ruční GPIO, idle HIGH)**.
  🔴 PB12 **musí být HIGH během načítání konfigurace GW1NR-9 z flash**, jinak FPGA nenaběhne
  (`RX0:FF`). Proto `osDelay(250)` ve `FpgaTask` před prvním clockem.
- **Sběrnice je dvoubodová** — jen Tang Nano, žádná další zařízení (zadání §6).

### SCK — hlavní otevřený bod

| | Hodnota | Zdroj |
|---|---|---|
| Dnešní STM strop | 10 MHz (`FPGA_SCK_MAX_HZ`), cíl 6 MHz | kontrakt v1 |
| **Dnešní FPGA strop** | **~2 MHz** | odpověď FPGA strany k v2, bod 6: *„PHY oversampluje 10MHz doménou"* |
| Zadání nové desky | 25 MHz reálně `[O]`, cíl 50 | `citac_zadani_predavaci.md` §6 |

🔴 **Při 2 MHz je strop proudu ~15 000 záznamů/s** (16 B/záznam) — to je limit, ne SPI ani SDRAM.
Zvýšení vyžaduje **přesun PHY na `clk_p0`**, což FPGA strana už plánuje (`BOARD_V20_CHECKLIST` §6).
**Bez toho je „co nejrychleji" fikce.** Viz rozpočet v §6.

---

## 3. Příznak „mám data" na MISO (nahrazuje `Data_RDY`)

Při **deasertovaném CS** drží FPGA na `MISO` statickou úroveň:

| MISO při CS=1 | Význam |
|---|---|
| **HIGH** | FPGA má ≥1 nový záznam (nebo nové měření) |
| **LOW** | nic nového |

- STM čte `HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_2)` — `IDR` platí i v režimu alternativní funkce,
  takže se na SPI2 vůbec nesahá. **Dotaz je elektricky tichý**: žádné hodiny, žádná změna CS.
  Tím padá námitka zadání §6, že polling vstřikuje rušení do časovacích vstupů.
- ⚠️ **Proti zvyku SPI slave**, který drží MISO v Hi-Z při CS=1. Tady to nevadí — sběrnice
  je dvoubodová. **FPGA musí MISO aktivně budit i při CS vysoko.**
- **Pojistka:** STM si na PI2 zapne interní **pull-down**, takže mrtvá/nenaběhlá FPGA čte
  „nic nového", ne falešné „mám data". (PUPDR je nezávislý na MODER, takže AF5 + pull-down
  jde dohromady.)
- Při CS↓ přepne FPGA MISO na posuvný registr obvyklým způsobem.

---

## 4. Krátká transakce: STAV (128 B, TYPE 0x80)

Rámec **beze změny proti v2** — jen `VERSION = 0x03` a jiný payload:

| Off | Délka | Pole |
|---|---|---|
| 0 | 1 | MAGIC = 0xA5 |
| 1 | 1 | **VERSION = 0x03** |
| 2 | 1 | TYPE |
| 3 | 1 | FLAGS/STATUS (bity jako v1/v2) |
| 4 | 4 | SEQUENCE (u32 LE) |
| 8 | 2 | PAYLOAD_LEN (u16 LE, max 114) |
| 10 | 2 | RESERVED = 0 |
| 12 | 114 | PAYLOAD |
| 126 | 2 | CRC16 (LE) — **CRC-16/CCITT-FALSE přes byte 0..125**, jako v2 |

### Payload: sada na kanál (32 B), dvakrát

| P-off | Délka | Pole | Pozn. |
|---|---|---|---|
| 0 | 8 | `freq_x100000` (u64 LE) | **autoritativní**, předdělička už zahrnuta |
| 8 | 8 | `edge_count` (u64 LE) | počet period v okně |
| 16 | 8 | `dt_ns` (u64 LE) | skutečné Δt okna |
| 24 | 1 | **`presc`** | 1 = přímá cesta, 10 = přes `MC12080`; **0 = neznámo** |
| 25 | 1 | `ch_status` | bit0 VALID, bit1 SIGNAL_LOST, bit2 overflow, bit3 Δt alias |
| 26 | 2 | reserved = 0 | |
| 28 | 4 | `win_seq` (u32 LE) | gap-free čítač oken, +1 za okno |

**Kanál A = P-off 0..31, kanál B = P-off 32..63.**

🔴 **`presc` je nejdůležitější nové pole.** Dnešní STM kód násobitel `edge_count`
**hádá a ověřuje** (`fpga_freq_hires_mul` zkouší 1/4/16 a porovnává s `freq_x100000`).
Vzniklo to z chyby, kdy pevné „×4" hlásilo při `fpgasim on 10000000` **40 MHz**
(commit `a6c0128`). Když FPGA násobitel **deklaruje**, ověřování je jen pojistka,
ne zdroj pravdy — a neshoda `presc` vs. skutečnost se stane **detekovatelnou chybou**
místo tichého čtyřnásobku.

### Payload: společná část (P-off 64+)

| P-off | Délka | Pole |
|---|---|---|
| 64 | 8 | `timestamp` (u64) — ticky 10 MHz |
| 72 | 8 | **`time_error_ps`** (i64 LE) — OCXO vs `GPS_1PPS`, **v pikosekundách** |
| 80 | 4 | `pps_count` (u32) — počet viděných hran 1PPS (detekce výpadku) |
| 84 | 1 | `time_error_status` — bit0 platné, bit1 1PPS přítomen, bit2 holdover |
| 85 | 1 | `measurement_status` — bit0 VALID, bit1 FRESH |
| 86 | 2 | `error_flags` (u16) |
| 88 | 2 | `fw_version` (u16) |
| 90 | 2 | `caps` (u16) |
| 92 | 1 | `clk_status` — bit0 10 MHz přítomen, bit1 PLL lock, bit2 TDC kalibrace platná |
| 93 | 1 | `mode` — 0 = FREKVENCE, 1 = ČASOVÁNÍ, 2 = **ADRET** |
| 94 | 4 | **`fifo_count`** (u32) — kolik záznamů čeká v bulk FIFO |
| 98 | 1 | `adret_div_log10` — aktivní dělič N = 10^této hodnoty (0..6) |
| 99 | 15 | reserved = 0 |

⚠️ **`time_error_ps`, ne `_ns`.** Rozlišení TDC je ~22 ps single-shot; v nanosekundách
by se zahodilo o dva řády víc, než přístroj umí. i64 ps = ±106 dní rozsahu.

---

## 5. Dlouhá transakce: BULK — proud dat plnou rychlostí

**Tohle je jádro v3.** Zadání chce souvislý proud, ne 4 hodnoty za sekundu.

### Proč nestačí posílat rámce rychleji

Kontrakt žádá **≥20 µs mezi rámci** (FPGA potřebuje čas složit rámec). Při 25 MHz trvá
128B rámec 41 µs, takže mezera sebere **třetinu** propustnosti; u 64B rámců polovinu.
Řešení je **jedno dlouhé CS okno přes tisíce bajtů** — mezera se pak platí **jednou
za blok**, ne za každý záznam.

### Průběh

1. STM pošle v běžném 128B rámci **`TYPE = 0x82` BULK_REQ**:

   | P-off | Délka | Pole |
   |---|---|---|
   | 0 | 1 | `kind` — 0 = WINDOW (32 B/zázn.), 1 = STAMP (16 B/zázn.) |
   | 1 | 1 | reserved |
   | 2 | 2 | `count` (u16) — kolik záznamů |
   | 4 | 4 | `from_seq` (u32) — 0 = od nejstaršího dostupného |

2. **Následující CS okno** je bulk přenos: STM naclockuje `count × recsize + 4 B`,
   FPGA proudí z PSRAM. Pipelining je stejný jako u v2 (odpověď se připraví z předchozího rámce).
3. Trailer 4 B: `CRC16` (u16, přes celý blok) + `status` (u8: bit0 = došla data) + reserved.
4. **Dojdou-li záznamy dřív**, FPGA doplní **výplňový záznam** s `flags` bit15 = 1;
   STM ho zahodí. Blok se nikdy nezkracuje — délku určuje master.

### Formát záznamů

**WINDOW (32 B)** — režim FREKVENCE. Oba kanály sdílejí hradlo, takže **jeden záznam nese oba**:

| Off | Délka | Pole |
|---|---|---|
| 0 | 4 | `win_seq` (u32) |
| 4 | 8 | `dt_ns` (u64) — společné okno |
| 12 | 4 | `edges_a` (u32) |
| 16 | 4 | `edges_b` (u32) |
| 20 | 2 | `fine_a` (u16) — jemný TDC kód |
| 22 | 2 | `fine_b` (u16) |
| 24 | 2 | `flags` (bit15 = výplň/neplatný) |
| 26 | 6 | reserved |

**STAMP (16 B)** — režim ČASOVÁNÍ, gap-free razítka:

| Off | Délka | Pole |
|---|---|---|
| 0 | 8 | `ts` (u64) — razítko v TDC tickách |
| 8 | 4 | `edge_index` (u32) |
| 12 | 1 | `ch` (0 = A, 1 = B) |
| 13 | 1 | `fine` |
| 14 | 2 | `flags` (bit15 = výplň) |

⚠️ **Obě velikosti jsou mocniny 2 záměrně** — index se pak na STM straně maskuje, ne dělí,
a záznamy nikdy nepřekročí hranici cache line.

### Souvislost a ztráty

- `win_seq` / `edge_index` jsou **monotónní**; díra = STM restartuje statistiku
  (žádná havárie, ale musí to poznat).
- `fifo_count` v rámci STAV říká, kolik toho čeká → STM si sám řídí tempo a pozná
  hrozící přetečení dřív, než k němu dojde.

---

## 6. Rozpočet propustnosti — kde je strop doopravdy

| Článek | Propustnost | Poznámka |
|---|---|---|
| **FPGA PHY dnes (~2 MHz)** | 250 kB/s → **~15 k zázn./s** (16 B) | 🔴 **dnešní strop** |
| SPI @25 MHz | 3,1 MB/s → 195 k zázn./s (16 B) / 97 k (32 B) | po přesunu PHY na `clk_p0` |
| Zadání: gap-free do 100 kHz | 1,6 MB/s (16 B) | **sedí do 25 MHz s rezervou** |
| SDRAM zápis | 34 MB/s (změřeno `membench`) | zdaleka ne limit |
| FMC sdílený s LTDC | LTDC bere ~46 MB/s | 3 MB/s navíc je ~6 % — v pohodě |
| **Datová cache 8 MB** | při 3,1 MB/s = **okno 2,7 s** | při 4/s = 18 h, při 10/s = 7,3 h |

**Závěr: strop je FPGA PHY, ne nic na naší straně.** Dokud jede 2 MHz, „co nejrychleji"
znamená 15 k záznamů/s.

### Co musí udělat STM strana (a co ji omezuje)

- **DMA přímo do SDRAM cache** (`.measlog` @0xC1000000). ⚠️ Region je WBWA cacheable,
  takže po DMA zápisu **musí** konzument volat `sdram_log_invalidate()` — DMA obchází
  D-cache stejně jako DMA2D u framebufferu.
- ⚠️ **DMA1/DMA2 nedosáhnou na DTCM.** Buffer musí být v AXI SRAM nebo přímo v SDRAM.
- 🔴 **`NDTR` je 16bitový → max 65 535 položek na jeden přenos.** Při 32bitových slovech
  to je **256 kB na přenos**, takže 8MB kruh nejde naDMAovat najednou.
  Řešení: **double-buffer mód DMA** (`DMA_SxCR_DBM`) — dvě okna, která se automaticky
  střídají, a v callbacku se přepíše jen to neaktivní. Tím vzniká souvislý proud
  do velkého kruhu **bez kopírování CPU**.
- Jedno CS okno 256 kB @25 MHz = **84 ms**; po tu dobu musí FPGA proudit bez přestávky
  (jinak výplň, viz §5).

---

## 7. Režim ADRET (fázové porovnání normálů)

Zadání §6: dělič v FPGA propustí do TDC **každou N-tou hranu**; 10 MHz ÷ 10⁶ = 10 razítek/s.
Rozlišení: τ = 1 s → 2,2×10⁻¹¹, τ = 100 s → **2,2×10⁻¹³**. Adret 4110A dosahuje 10⁻⁸…10⁻¹²,
takže ho překonáme už při τ = 10 s.

🔑 **Adret je NÍZKORYCHLOSTNÍ případ** — 10 razítek/s = 160 B/s. **Bulk transakci nepotřebuje
vůbec**, vejde se do běžného rámce STAV. Co potřebuje, je **dlouhá historie**, a tu dává
datová cache: při 10/s pokryje 8 MB ring **7,3 hodiny** souvislé fáze.

Protokol:
- `SET_CONFIG` `config_id 0x03` = `adret_div_log10` (0..6) → dělič N = 10^hodnota.
- Rámec STAV hlásí aktivní hodnotu zpět v `adret_div_log10` (readback musí číst
  **nastavení**, ne poslední naměřenou hodnotu — poučení z pasti #11 na IPC straně).
- 🔴 **Dělič musí být ZA vstupem TDC, v FPGA** (zadání §6) — čítač hran běží souběžně,
  takže číslo propuštěné hrany znáš přesně a fáze se nerozjede.

### Co už na naší straně existuje a co chybí

| | Stav |
|---|---|
| Okno **„Odchylka ×N"** (`s_view=47`, `render_devmult`) | ✅ hotové — `df = f − f₀`, velké číslo `df × N` (×1…×1e6), tlačítko **NUL**, vedle ppb/ppm |
| Zdroj dat pro něj | ⚠️ **čistě SW nad headline** — žádná fázová data z FPGA |
| Skutečné fázové porovnání | ❌ chybí dělič ve FPGA + `time_error_ps` / razítka |

Takže UI polovina Adretu **je hotová**; chybí jí pod tím reálná fáze. Po v3 se okno
napojí na razítka místo na rozdíl headline a rozlišení skočí o několik řádů.

---

## 8. Co z v2 zůstává BEZE ZMĚNY (nepřepisovat)

- Rámec 128 B, MAGIC, CRC-16/CCITT-FALSE přes 0..125, CRC na 126/127, LE endianita.
- ACK/FRESH sémantika (`FRESH` shodí až platný ACK té SEQ), auto-re-START, SIGNAL_LOST watchdog.
- **Gap-free okna** — uzavírací hrana okna N je první hranou okna N+1, `win_seq` monotónní.
- `SET_CONFIG` (TYPE 0x01) + `fw_version` / `caps` / `clk_status`.
- **`dt_ticks = round(dt_ns·2/5)`** — gate syntézu sčítat **v TICÍCH, ne v ns**
  (`dt_ns` je floor, jinak systematika ~−1×10⁻⁹). Pravidlo z auditu bitstreamu 0x0201.
- **ACKovat pouze DATA rámce** (ne 0xA0 — sdílí SEQUENCE a shodil by FRESH nepřečteného měření).
- Po epizodě SIGNAL_LOST **zahodit první okno**.
- **Code-density histogram** (TYPE 0xA0) + `cal_mode` — nová deska ho potřebuje ještě víc
  (carry chain, průběžná kalibrace podle `TMP117`, bin 50 ps @25 °C → ~65 ps @85 °C).
- S1/S2 akumulátory pro Λ/Ω regresi. ⚠️ Zadání §6 mluví o `N, Σx, Σx², Σy, Σxy` —
  **S1/S2 řeší totéž levněji** (specializace na x = index hrany, y = razítko).
  Nezavádět druhou sadu; jen doplnit druhý kanál.

## 9. Co z v1/v2 KONČÍ

`freq16_x100000` · `status2` / `FPGA_ST2_DIV16_ERR` · `phase_status` (present/fine_seen) ·
hystereze /4↔/16 (`fpga_freq_select*`, včetně selftestu) · `channel_id` v původním významu ·
pevný předpoklad `PRESC = 4`.

---

## 10. Otevřené body pro FPGA stranu

1. **SCK:** kdy půjde PHY na `clk_p0` a jaký bude reálný strop? Bez toho je proud limitován
   na ~15 k záznamů/s a `[O]` odhad 25 MHz ze zadání je nedosažitelný.
2. **Bulk transakce:** dá se udržet souvislý proud po celé CS okno 256 kB (84 ms @25 MHz),
   nebo je potřeba kratší bloky?
3. **MISO při CS=1:** může FPGA pin aktivně budit (ne Hi-Z)?
4. **`time_error_ps`:** měří FPGA fázi OCXO vs 1PPS, nebo jen razítkuje 1PPS a výpočet
   necháváme na STM? (Pro smyčku GPSDO stačí obojí, ale musíme vědět které.)
5. **`presc`:** může FPGA hodnotu držet z `SET_CONFIG` a hlásit ji zpět, aby STM nemusel
   násobitel hádat?
6. **Dva kanály:** sdílejí jedno hradlo (jeden `dt_ns`), nebo má každý vlastní?
   Návrh výše předpokládá **společné hradlo** — je to podmínka párování v datové cache.
7. **Adret dělič:** N jako mocnina 10 (0..6) stačí, nebo chceš libovolné u32?

## 11. Migrace STM driveru (checklist)

- [ ] `VERSION == 0x03`, payload dle §4; rozšířit `fpga_meas_t` o druhý kanál
- [ ] **`presc` z rámce** místo hádání v `fpga_freq_hires_mul()` (ověřování nechat jako pojistku)
- [ ] Odstranit `fpga_freq_select*` + hysterezi + selftest #1 (⚠️ `SELFTEST_N` klesne, srovnat
      `NAMES[]` a `_Static_assert` v okně Selftest)
- [ ] Tichý dotaz přes `MISO` GPIO + pull-down na PI2 místo 20 Hz pollingu
- [ ] BULK: DMA double-buffer do `.measlog`, `sdram_log_invalidate()` po přenosu
- [ ] `sdram_log_put()` plnit **oba kanály** (`SDRAM_LOG_F_B_VALID`)
- [ ] `time_error_ps` → smyčka GPSDO (`AD5693R` @0x4C, STATUS #82)
- [ ] Okno „Odchylka ×N" (s_view=47) napojit na reálnou fázi místo rozdílu headline
