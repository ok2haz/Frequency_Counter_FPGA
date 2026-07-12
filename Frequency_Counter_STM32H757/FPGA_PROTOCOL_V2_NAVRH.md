# NÁVRH: SPI protokol v2 (STM32H757 ↔ GW1NR-9) — k diskusi

Stav: **NÁVRH k odsouhlasení FPGA stranou. Nic z toho zatím není v kódu.**
Aktuálně jede protokol v1 (64 B, VERSION=0x01) dle [`FPGA_INSTANCE_BRIEF.md`](FPGA_INSTANCE_BRIEF.md)
a je plně funkční — v2 je evoluce, ne oprava.

## Proč v2 (motivace)

1. **V1 rámec je prakticky plný.** Payload 50 B, poslední pole končí na abs. offsetu 60,
   spare = 2 B. Každé další pole = lámání kompatibility. Zadání: **nechat velkou rezervu.**
2. **STM chce průměrovat / počítat statistiku z oken, ne jen zobrazovat poslední hodnotu.**
   K tomu potřebuje záruku, že vidí **každé okno** a že okna jsou **souvislá** (gap-free):
   Σhran/ΣΔt přes N oken = delší gate bez ztráty informace, dále Allanova deviace,
   drift, Ω-regrese. V1 nese jen "poslední měření" — při zaváhání pollingu okno zmizí
   a kontinuitu nelze ověřit.
3. **Dynamický gate:** tlačítko GATE (0,1/1/10/100 s) na displeji zatím nic nedělá.
   S krátkým základním oknem FPGA (100 ms) a streamem oken si STM **syntetizuje
   libovolný gate sám** (součty oken) — FPGA nemusí gate vůbec přepínat.
4. **Nové HW vstupy:** FPGA má 4× 100 MHz (0/90/180/270°) + **přímo 10 MHz z GPSDO**
   → protokol má mít místo na health/fázová měření vůči 10 MHz.
5. Chybí **verze FW / capabilities** — STM neumí poznat, co bitstream umí.

## Rámec v2: 128 B, VERSION=0x02

Stejná filozofie jako v1 (full-duplex, přednačtená odpověď, ACK/pull, CS rámuje),
jen delší tělo a víc rezervy:

| Off | Délka | Pole | Pozn. |
|---|---|---|---|
| 0 | 1 | MAGIC = 0xA5 | beze změny |
| 1 | 1 | VERSION = **0x02** | |
| 2 | 1 | TYPE | viz níže |
| 3 | 1 | FLAGS/STATUS | stejné bity jako v1 |
| 4 | 4 | SEQUENCE (u32 LE) | beze změny |
| 8 | 2 | PAYLOAD_LEN (u16 LE) | max **114** |
| 10 | 2 | RESERVED = 0 | |
| 12 | 114 | PAYLOAD | |
| 126 | 2 | CRC16 (LE: 126=low, 127=high) | **CRC-16/CCITT-FALSE přes byte 0..125** (stejný algoritmus, kontrolní vektor `crc16("123456789")==0x29B1` platí dál) |

- **Přesně 128 B na transakci** (1024 SCK hran). Časování CS/mezirámcová pauza beze změny
  (CS↓→SCK 2 µs, SCK→CS↑ 2 µs, mezi rámci 25 µs).
- **SCK: doporučeně zvednout na 4 MHz** (rámec = 256 µs; strop kontraktu ≤6 MHz cíl
  / ~10 MHz max platí dál). Při 20 Hz pollingu = zátěž linky ~0,5 %.
- **Migrace:** STM driver dostane compile-time přepínač (v1 64 B / v2 128 B) — přepne se
  společně s nahráním v2 bitstreamu. Žádný autodetect za běhu (master určuje délku rámce).

## TYPE mapa v2 (úklid překryvu v1)

V1 deklarovala „0x01–0x07 gate/režim" a zároveň 0x06=ACK (kolize). V2 čistě:

| TYPE | Význam | Směr |
|---|---|---|
| 0x01 | **SET_CONFIG** (viz níže) | STM→FPGA |
| 0x06 | ACK (echo SEQUENCE) | STM→FPGA |
| 0x08 | START (informativní) | STM→FPGA |
| 0x09 | STOP (informativní) | STM→FPGA |
| 0x80 | **DATA** | FPGA→STM |
| 0xA0 | rezerva: autokalibrace | budoucí |
| 0xA1 | rezerva: fáze/PPS report (10 MHz vs 100 MHz, 1 PPS) | budoucí |
| 0xB0 | rezerva: FFT | budoucí |
| ostatní | rezervováno = ignorovat | |

## DATA payload v2 (TYPE 0x80)

**Payload-offsety 0..47 jsou 1:1 s v1** (abs. 12..59) → minimální přepis FPGA logiky:

| P-off | Abs | Délka | Pole |
|---|---|---|---|
| 0 | 12 | 8 | freq_x100000 (u64 LE) — pin28 **/4**, dělička zahrnuta |
| 8 | 20 | 8 | edge_count — počet period aktuálního okna |
| 16 | 28 | 8 | gate_time_ns — skutečné Δt okna [ns] |
| 24 | 36 | 8 | timestamp — 10MHz ticky (běžící z 10 MHz GPSDO vstupu) |
| 32 | 44 | 1 | channel_id |
| 33 | 45 | 1 | measurement_status (bit0 VALID, bit1 FRESH) |
| 34 | 46 | 4 | error_flags (bit0 meas/4, bit1 SIGNAL_LOST, bit2 overflow) |
| 38 | 50 | 1 | phase_status (present[3:0] / fine_seen[3:0]) |
| 39 | 51 | 1 | status2 (bit0 = /16 chyba) |
| 40 | 52 | 8 | freq16_x100000 (u64 LE) — pin27 **/16** |
| **48** | **60** | **2** | **fw_version** (u16 LE, verze bitstreamu; 0 = neznámá) |
| 50 | 62 | 2 | **caps** (u16 LE): bit0=window stream, bit1=SET_CONFIG, bit2=10MHz health, bit3=fáze/PPS, zbytek 0 |
| 52 | 64 | 1 | **clk_status**: bit0=10 MHz přítomen, bit1=PLL/DLL lock, zbytek 0 |
| 53 | 65 | 1 | **win_count** (0–2): kolik window záznamů níže je platných |
| 54 | 66 | 2 | pad = 0 (zarovnání) |
| 56 | 68 | 16 | **window[0]** (nejnovější uzavřené okno) |
| 72 | 84 | 16 | **window[1]** (předchozí okno) |
| 88 | 100 | **26** | **reserved = 0 (REZERVA do budoucna)** |

**Window záznam (16 B):** stavební kámen průměrování/statistiky na STM.

| Off v rec | Délka | Pole |
|---|---|---|
| 0 | 4 | win_seq (u32 LE) — čítač oken, +1 za každé uzavřené okno |
| 4 | 4 | edges (u32 LE) — počet period v okně |
| 8 | 8 | dt_ns (u64 LE) — Δt okna [ns] (stejná definice jako gate_time_ns) |

- **Souvislost oken:** okno `win_seq+1` musí začínat hranou, kterou skončilo okno
  `win_seq` (gap-free reciproké čítání). STM kontinuitu ověří monotonií win_seq;
  díra → statistika se restartuje (žádná havárie).
- 2 záznamy na rámec při pollingu 20 Hz = kapacita 40 oken/s ≥ 4× rezerva
  na navrhované tempo oken 10/s.

## Základní okno: 250 ms → **100 ms** (k diskusi)

Návrh: FPGA měří pevné reciproké okno ~**100 ms** (10 oken/s). STM pak:
- headline obnovuje 10×/s (nižší latence než dnešní 4×/s),
- **gate 0,1 s = nativní okno; 1/10/100 s = Σedges/Σdt_ns přes 10/100/1000 souvislých
  oken** (matematicky identické s dlouhou bránou — žádná ztráta rozlišení),
- tlačítko GATE tak funguje čistě na STM straně, FPGA gate nemění.

Pokud je 100 ms na FPGA nepříjemné, 250 ms zůstává OK — jen gate 0,1 s na displeji
nebude nativní (vyřadí se z nabídky).

## SET_CONFIG (TYPE 0x01), payload STM→FPGA — volitelné (caps bit1)

| P-off | Délka | Pole |
|---|---|---|
| 0 | 1 | config_id: 0x01 = base window |
| 1 | 1 | hodnota: 0=100 ms, 1=250 ms (default), 2=1 s |
| 2 | 12 | reserved = 0 |

FPGA potvrdí tak, že příští DATA rámec má v `caps`/chování novou hodnotu (žádný
speciální ACK rámec). Když FPGA SET_CONFIG neumí (caps bit1=0), STM ho neposílá.

## Co zůstává beze změny

- SPI mode 0, MSB first, 8-bit, CS active-low, přednačtená (pipelined) odpověď.
- FLAGS bity, ACK sémantika (FRESH shodí až validní ACK té SEQ), auto-re-START,
  SIGNAL_LOST watchdog, škálování ×1e5 s děličkou zahrnutou ve FPGA, LE endianita.
- CRC algoritmus (jen délka pokrytí 0..125).

## Otevřené body pro FPGA stranu

1. Souhlas se 128 B rámcem a rozložením výše? (rezerva 26 B v payloadu + 3 TYPE sloty)
2. Gap-free okna: potvrdit, že okno N+1 navazuje na hranu konce okna N (nebo co to stojí).
3. Základní okno 100 ms vs 250 ms (viz výše).
4. win_seq/edges u32 stačí? (u32 edges = strop 4,3e9 period/okno → i 21s okno @100 MHz
   na pinu je 2,1e9 → OK.)
5. `clk_status` bit0: detekce přítomnosti 10 MHz — má FPGA jak? (čítač 10 MHz hran
   za 100MHz okno.)
6. SCK 4 MHz — ověřit integritu (CRC čítač na STM to změří sám).

---

# ODPOVĚĎ FPGA STRANY — IMPLEMENTOVÁNO (aktuální bitstream FW 0x0201)

## Changelog bitstreamu

- **0x0200** (2026-07-07): první implementace v2.
- **0x0201** (2026-07-10): opravy z auditu (koherence rámce: S1/S2 latch spolu
  s oknem; rx_pend proti ztrátě povelu mimo IDLE; tx_valid — PHY drží poslední
  KOMPLETNÍ rámec, nikdy mix payload/CRC; saturace histogramu na 0xFFFFFF;
  ignorace SCK hran nad 1024) + **error_flags bit3 = Δt alias** (okno bez hran
  > 42,9 s → hodnota neplatná, zahodit).
  **Pravidla pro STM driver:** (1) `dt_ticks = round(dt_ns·2/5)` — bezztrátová
  rekonstrukce ticků; gate syntézu sčítat v TICÍCH, ne v ns (dt_ns je floor,
  jinak systematika ~−1e-9). (2) ACKovat pouze DATA rámce (ne 0xA0 — sdílí
  SEQUENCE a shodil by FRESH nepřečteného měření). (3) Po epizodě SIGNAL_LOST
  zahodit první okno. (4) Polling ≥ 2× tempo oken, jinak díry ve win_seq.
  (5) Degenerovaný histogram (1–2 biny) = soudělný vstup → Λ negainuje,
  vykazovat nejistotu ~bin/T.

v2 je implementována (`Frequency_Counter_FPGA_Module`, VERSION=0x02, rámec 128 B,
CRC přes 0..125, CRC na 126/127, PAYLOAD_LEN=114). Odpovědi na otevřené body:

1. **128 B rámec + rozložení: ANO**, 1:1 dle návrhu výše. caps = **0x0013**.
2. **Gap-free okna: ANO** — uzavírací hrana okna N je první hranou okna N+1
   (win_seq monotónní, +1 za okno; window[0]=nejnovější, window[1]=předchozí).
3. **Základní okno: přes SET_CONFIG** (config_id 0x01): 0=100 ms, 1=250 ms
   (default po startu), 2=1 s. FPGA gate sám nemění.
4. win_seq/edges u32: ANO (interně 26 bitů period/okno).
5. clk_status: bit0=1 (10 MHz běží implicitně — celá aplikační doména z něj žije,
   bez něj rámec nevznikne), bit1=0 (žádný PLL). caps bit2=0.
6. **SCK: zatím max ~2 MHz** (PHY oversampluje 10MHz doménou; rámec ~512 µs,
   při 20 Hz pollingu ~1 % linky). Zvýšení na 4–20 MHz = přesun PHY na clk_p0,
   plánováno spolu s flash-bridge streamem (BOARD_V20_CHECKLIST §6).

## Rozšíření nad rámec návrhu (k odsouhlasení STM stranou)

- **caps bit4 = Λ/Ω + fine rozšíření v rezervě DATA** (abs 100..117):
  100 = {fine_last[3:2], fine_first[1:0]} krajních hran okna,
  101..107 = **S1** (u56) = Σ ts_rel, 108..117 = **S2** (u80) = Σ S1
  (LSB = tick 2,5 ns, vztaženo k první hraně okna). Λ/Ω regrese: body i=0..N
  (t₀=0, t_N=Δt, N=edge_count): Σt=S1, Σi·t=N·S1−S2; LSQ slope
  β = (Σi·t − Σi·Σt/(N+1)) / (Σi² − (Σi)²/(N+1)); f = PRESC/(β·2,5 ns), PRESC=4.
  Kvantizace i nelinearita binů klesá ~1/√N (při N≈10⁶ příspěvek FPGA ~1e-11).
- **SET_CONFIG config_id 0x02 = cal_mode** (0/1): 1 = vstup /4 přepnut na interní
  ring oscilátor (~6–10 MHz) pro code-density kalibraci binů bez vstupního
  signálu. Okna kolem přepnutí zahodit.
- **TYPE 0xA0 (autokalibrace)**: STM pošle rámec TYPE=0xA0 (payload se ignoruje)
  → příští TX rámec je **0xA0 CAL report**: 12..23 hist0..3 (4×u24 histogram fine
  kódů za poslední okno — čítá se trvale i na měřeném signálu = background
  tracking šířek binů/teplotního driftu), 24 fine kódy, 25 flags{bit0=cal_mode},
  26..32 S1, 33..42 S2, 43..46 edges (u32), 47..125 = 0.

## Migrace STM driveru (checklist)

- [ ] rámec 64→128 B (compile-time přepínač dle návrhu), CRC 0..125, CRC @126/127
- [ ] parser: VERSION==0x02; offsety 12..59 beze změny (v1 kód lze recyklovat)
- [ ] nová pole: fw_version (60), caps (62), clk_status (64), win_count (65),
      window[0] (68), window[1] (84); statistika/gate z window streamu
- [ ] volitelně: SET_CONFIG 0x01 (GATE tlačítko!), 0x02 cal_mode; 0xA0 CAL report;
      Λ/Ω výpočet z S1/S2 (u128 mezivýpočty nebo double)
