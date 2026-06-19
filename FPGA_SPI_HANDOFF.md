> **⚠️ AKTUÁLNÍ ZDROJ PRAVDY = [`FPGA_INSTANCE_BRIEF.md`](FPGA_INSTANCE_BRIEF.md)** (sedí na reálný STM kód:
> SCK ~0,78 MHz / max ~10 MHz, mezirámcová pauza 25 µs, CS idle HIGH, aktuální bring-up `RX0:FF`).
> Tento soubor je starší a ponechán pro kontext; níže opraveny jen nejdůležitější zastaralé hodnoty.

# Handoff: SPI slave + čítač kmitočtu pro Gowin FPGA

Tento dokument je SAMOSTATNÝ brief pro implementaci FPGA strany. Protistrana (STM32H757,
Cortex-M7 @ 480 MHz) je **hotová a otestovaná na úrovni kódu** — čeká jen na FPGA.

## 1. Role a topologie
- **STM32 = SPI MASTER** (generuje SCK + CS), polluje ~10 Hz.
- **FPGA (Gowin) = SPI SLAVE** + čítač kmitočtu. Měří kmitočet, drží poslední hotové měření
  v TX bufferu a vrací ho ve full-duplex přenosu.
- Žádné handshake GPIO — vše in-band přes STATUS/FLAGS pole v rámci.

## 2. Piny (strana FPGA) + elektrika
| FPGA pin | Signál | Směr | Pozn. |
|---|---|---|---|
| PIN54 | MOSI | STM32 → FPGA (vstup) | data od mastera |
| PIN57 | MISO | FPGA → STM32 (výstup) | odpověď FPGA |
| PIN55 | SCK | STM32 → FPGA (vstup) | hodiny generuje master |
| PIN56 | CS | STM32 → FPGA (vstup) | **active LOW** |
- 3,3 V logika, **bez level shifteru**.
- FPGA NEGENERUJE hodiny — je čistý slave.

## 3. SPI parametry
- **Mode 0**: CPOL=0 (SCK idle LOW), CPHA=0 → **vzorkuj MOSI na náběžné hraně SCK**,
  **MISO měň na sestupné hraně** (a před první náběžnou musí být první bit platný).
- **MSB first**, 8-bit bajty.
- SCK: **aktuálně ~0,78 MHz** (STM cíl 1 MHz); strop **cíl ≤6 MHz, max ~10 MHz** (NE 20 MHz!).
- CS rámuje transakci. Master drží: CS↓ → 1.SCK **2 µs**; poslední SCK → CS↑ **2 µs**; **mezi rámci 25 µs** (≥20 µs).
- **Přesně 64 bajtů na transakci** (512 SCK hran). Master vždy vyclockuje 64 B a současně
  čte 64 B z MISO.

## 4. Transakční model (KLÍČOVÉ — častý zdroj chyb u SPI slave)
- Odpověď na MISO je **PŘEDNAČTENÁ z minulého cyklu** — FPGA nemůže odpověď v rámci téhož
  přenosu odvodit z právě přijímaného MOSI. Tj. obsah MISO = rámec připravený PŘED touto transakcí.
- Doporučený postup FPGA:
  1. **Mimo transakci (CS=1, idle)**: poskládej kompletní 64B TX rámec (DATA 0x80) z posledního
     měření, spočítej CRC, a **zalatchuj** ho do výstupního shift registru. Tím je vyclockovaný
     rámec vždy konzistentní (žádné trhání přes hranice clock domén).
  2. **Během transakce (CS=0)**: shiftuj ven připravený rámec (MISO) a zároveň shiftuj dovnitř
     MOSI do RX bufferu. Počítej bity/bajty, CS=1 = konec/reset.
  3. **Po CS↑**: zvaliduj RX rámec (MAGIC, CRC). Zpracuj TYPE od mastera (ACK/START/STOP/…).
     Připrav nový TX rámec pro příští transakci.
- STM32 tak přečte měření, které bylo hotové NA ZAČÁTKU přenosu. ACK, který master pošle,
  potvrzuje předchozí SEQUENCE → FPGA pak smí shodit DATA_FRESH.

## 5. Formát 64B rámce (oba směry stejná struktura)
| Off | Size | Pole |
|---|---|---|
| 0 | 1 | MAGIC = **0xA5** |
| 1 | 1 | VERSION = **0x01** |
| 2 | 1 | TYPE |
| 3 | 1 | FLAGS / STATUS |
| 4 | 4 | SEQUENCE (uint32, **little-endian**) |
| 8 | 2 | PAYLOAD_LEN (uint16 LE, max 50) |
| 10 | 2 | RESERVED = 0x0000 |
| 12 | 50 | PAYLOAD |
| 62 | 2 | CRC16 (LE: byte62=low, byte63=high) |

**Veškerá vícebajtová pole jsou little-endian** (LSB na nižším offsetu).

## 6. CRC-16/CCITT-FALSE
- poly **0x1021**, init **0xFFFF**, refin=false, refout=false, xorout=0x0000.
- Pokrývá **byte 0 až 61** (hlavička + payload). Výsledek: byte62 = low, byte63 = high.
- Referenční C (přesně co počítá STM32 — replikuj bit-identicky):
```c
uint16_t crc16_ccitt(const uint8_t *d, int n) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < n; i++) {        // n = 62
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}
```
V FPGA buď LUT, nebo sériový LFSR (MSB-first, poly 0x1021).

## 7. TYPE hodnoty
| TYPE | Význam | Směr |
|---|---|---|
| 0x01–0x07 | nastavení gate time / měřicího režimu | STM32→FPGA |
| 0x06 | **ACK** | STM32→FPGA |
| 0x08 | start continuous measurement | STM32→FPGA |
| 0x09 | stop continuous measurement | STM32→FPGA |
| 0x80 | **DATA response** (měření) | FPGA→STM32 |
| 0xA0 | autokalibrace | (budoucí) |
| 0xB0 | vyžádání FFT dat | (budoucí) |

## 8. STATUS/FLAGS byte (offset 3)
| Bit | Význam |
|---|---|
| 0 | DATA_VALID — měření je platné |
| 1 | DATA_FRESH — nové od posledního ACK |
| 2 | FIFO_EMPTY |
| 3 | FIFO_OVERFLOW |
| 4 | BUSY |
| 5 | RX_CRC_ERROR — poslední přijatý rámec měl špatný CRC |
| 6 | ACK_OK |
| 7 | ERROR |

STM32 bere měření jako platné, když: **CRC OK ∧ DATA_VALID=1 ∧ DATA_FRESH=1 ∧ SEQUENCE ≠ poslední přečtená.**
→ FPGA tedy MUSÍ: nastavit DATA_VALID když má reálné měření, DATA_FRESH když je nové,
a **inkrementovat SEQUENCE při každém novém měření** (tím STM32 pozná čerstvost).

## 9. DATA payload (TYPE 0x80), offsety v payloadu / absolutní
| P-off | Abs | Size | Pole |
|---|---|---|---|
| 0 | 12 | 8 | **frequency_x100000** (uint64 LE) |
| 8 | 20 | 8 | edge_count (uint64 LE) |
| 16 | 28 | 8 | gate_time_ns (uint64 LE) |
| 24 | 36 | 8 | timestamp_10MHz_ticks (uint64 LE) |
| 32 | 44 | 1 | channel_id |
| 33 | 45 | 1 | measurement_status |
| 34 | 46 | 4 | error_flags (uint32 LE) |
| 38 | 50 | 12 | reserved = 0 |

PAYLOAD_LEN pro DATA = 50.

## 10. Kódování kmitočtu (BEZ float)
- `frequency_x100000` = kmitočet × 100000 (5 desetinných míst v Hz).
- Příklady:
  - 12,34567 Hz → 1234567
  - 123 456 789,01234 Hz → 12345678901234
- STM32 zobrazí: `integer = v/100000`, `frac = v%100000` → `123.456.789,01234Hz`
  (tečky = tisíce, čárka = desetinná, přesně 5 míst).

## 11. ACK detail
- ACK je rámec STM32→FPGA: TYPE=0x06, SEQUENCE = poslední přijatá SEQUENCE od FPGA.
- Po VALIDNÍM ACK (CRC OK, SEQUENCE souhlasí s naposledy odeslaným DATA) smí FPGA shodit DATA_FRESH.
- STM32 posílá ACK v každém pollu (i jako „prázdný" poll) — tím zároveň vyclockuje aktuální DATA.

## 12. Co master reálně posílá (referenční chování STM32)
- Při startu: rámec TYPE=0x08 (START).
- Každý poll (~10 Hz): rámec TYPE=0x06 (ACK) se SEQUENCE = naposledy přijatá platná.
- TX rámce mastera mají FLAGS=0, payload prázdný (PAYLOAD_LEN=0), validní CRC.
- Master IGNORUJE odpovědi se špatným MAGIC nebo CRC (tiše).

## 13. Clock domain / Gowin doporučení
- Čítač kmitočtu běží ve své doméně (referenční hodiny + gate). Výsledek přenes do SPI domény
  přes stabilní latch (double-buffer), latchni TX rámec když je CS idle.
- SPI slave shift registr: pro 5 MHz lze klockovat přímo SCK; pro ≥20 MHz zvaž oversampling
  systémovými hodinami FPGA + detekci hran SCK (robustnější vůči metastabilitě a delším drátům).
- CS použij jako asynchronní reset čítače bitů/bajtů (start rámce na CS↓).
- MISO: výstup měň na sestupné hraně SCK, aby byl stabilní pro náběžnou (mode 0).

## 14. Bring-up plán (doporučený postup)
1. **Fáze 1 — dummy rámec**: FPGA vrací PEVNÝ DATA rámec (známá frekvence, např.
   frequency_x100000 = 12345678901234 → displej musí ukázat `123.456.789,01234Hz`),
   DATA_VALID=1, DATA_FRESH=1, **SEQUENCE++ při každé transakci**, správný CRC.
   → Ověří framing, endianitu, CRC, MISO timing. STM32 příkaz `freq` (UART) vypíše hodnotu.
2. **Fáze 2 — reálný čítač**: napoj měřený kmitočet do frequency_x100000, dopočítej edge_count
   / gate_time_ns. SEQUENCE inkrementuj při každém NOVÉM měření, DATA_FRESH řiď přes ACK.
3. **Fáze 3 — rychlost**: zvedni SCK z 5 MHz k 20 MHz, ověř integritu CRC.

## 15. Časté chyby (na co si dát pozor)
- Endianita: vše LE. uint64 frekvence = 8 bajtů LSB-first na offsetu 12.
- CRC: MSB-first, init 0xFFFF, BEZ reflexe; pokrývá přesně 62 bajtů (0..61).
- MISO musí být přednačtené (rámec z minulého cyklu), ne odvozené z aktuálního MOSI.
- SEQUENCE se MUSÍ měnit, jinak STM32 nové měření nepozná (bere ho jako staré).
- Přesně 64 bajtů; CS↑ ukončuje/resetuje. Když master pošle ACK, neposílej nové DATA okamžitě
  ve stejném přenosu — odpověď je vždy ta připravená.

## 16. STM32-side reference (pro kontext)
- Driver: `CM7/Core/Src/fpga_freq.c` (build/parse rámce, CRC, ACK, parsing DATA).
- `FpgaTask` polluje 10 Hz, SPI2 master, CS=PB12 manuál active-low, mode 0, ~5 MHz (auto-prescaler).
- Validní DATA → zobrazí frekvenci (velký font) + GATE/EDGES/CH/SEQ (malý font) na displeji;
  UART příkaz `freq` vypíše poslední hodnotu.
