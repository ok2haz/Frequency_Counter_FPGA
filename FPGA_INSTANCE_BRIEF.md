# KONTROLNÍ ZADÁNÍ — FPGA slave (GW1NR-9 / Tang Nano 9K) ↔ STM32H757 SPI master

Jsi Claude pracující na **FPGA straně** (Gowin GW1NR-9, Tang Nano 9K). FPGA je SPI
**slave** + čítač kmitočtu. Protistrana (STM32H757, Cortex-M7 @ 480 MHz, SPI master)
je **hotová a nasazená** — tento dokument popisuje PŘESNĚ její reálné chování, ať je
FPGA s ní 100% konzistentní. Hodnoty zde odpovídají skutečnému kódu `fpga_freq.c`
na STM (ne přáním). STM se NEMĚNÍ — přizpůsobuje se FPGA jen tam, kde je to označeno.

---

## 0. AKTUÁLNÍ STAV BRING-UPU (čti první)
STM právě hlásí na displeji/UART: **`NOLINK HAL:OK RX0:FF`**.
- `HAL:OK` = STM úspěšně vyclockuje 64 B (SCK + CS jedou).
- `RX0:FF` = na MISO čte **samé jedničky** → **FPGA na MISO nic nebudí.**
- `CRC:0` = ani se nedojde ke kontrole CRC (padá hned na `MAGIC != 0xA5`).

➜ **Úkol FPGA #1:** při CS=0 MUSÍŠ aktivně budit MISO připraveným rámcem (MSB napřed),
ne ho nechat v Hi-Z / pull-upu. Pokud FPGA MISO netri-statuje správně nebo se shift
registr nespouští na CS↓, STM vidí 0xFF. Ověř, že:
1. FPGA je nakonfigurované a běží (config z flash doběhla).
2. MISO (pin 57) je **výstup** a mění se během CS=0.
3. CS (pin 56) na FPGA reálně padá do 0 (STM ho teď drží HIGH v klidu, pulzuje na LOW).

---

## 0b. HODINY / CLOCKING (Si5356A → FPGA)
Externí clock generator **Si5356A** (řízený STM po I2C1) dodává FPGA 4× **100 MHz**,
vzájemně posunuté po **45°** (reference Si5356 = externí **10 MHz CLKIN**):
| Si5356 výstup | fáze | FPGA pin |
|---|---|---|
| CLK0 | 0°   | PIN33 |
| CLK1 | 45°  | PIN34 |
| CLK2 | 90°  | PIN40 |
| CLK3 | 135° | PIN35 |

⚠️ **KRITICKÉ pro SPI:** FPGA SPI slave oversampluje SCK na ~100 MHz → potřebuje
**funkční 100 MHz hodinovou doménu**. Systémový/oversampling clock musí přijít na
**clock-capable (GCLK/PLL) pin** GW1NR-9 — ne na obyčejné I/O. Pro master clock použij
**CLK0 (0°, PIN33)**; fázově posunuté (45/90/135°) jsou pro *měřené* signály, ne jako
hlavní clock. Pokud clock tree/PLL nenalockuje, SPI logika neběží → STM vidí `RX0:FF`.
**Ověř: vede 100 MHz na clock-capable pin a PLL/global buffer v designu lockuje?**

## 1. Role a piny (strana FPGA)
- **STM = MASTER** (generuje SCK + CS), **FPGA = SLAVE** (jen reaguje, NEGENERUJE hodiny).
- 3.3 V LVCMOS33, bez level shifteru. **Společná zem nutná.**

| FPGA pin | Signál | Směr (z pohledu FPGA) | Pozn. |
|---|---|---|---|
| 54 | MOSI | **vstup** | data od STM |
| 57 | MISO | **výstup** | odpověď FPGA (preload!) |
| 55 | SCK  | **vstup** | hodiny od STM |
| 56 | CS   | **vstup** | **aktivní v LOW**, na FPGA pull-up |

⚠️ Piny 54–57 jsou dedikované MSPI/SSPI konfigurační piny — uvolni je do GPIO. STM je
během konfigurace FPGA z flash NEBUDÍ (CS drží HIGH od bootu). Po naběhnutí FPGA platí
tabulka výše.

## 2. SPI parametry (PEVNÉ — STM je takhle nastaven, neměň protistranu)
- **Mode 0**: CPOL=0 (SCK idle LOW), CPHA=0 → **vzorkuj MOSI na náběžné hraně SCK**,
  **MISO měň na sestupné hraně**; **první bit MISO musí být platný už při CS↓** (před 1. SCK).
- **MSB first**, **8-bit** slova.
- **SCK aktuálně ~0,78 MHz** (STM cíl 1 MHz, prescaler vyjde 0,78 MHz; kernel SPI123 ~100 MHz / 128).
  **Strop: cíl ≤ 6 MHz, absolutní maximum ~10 MHz.** FPGA SCK oversampluje na ~100 MHz
  přes 3-FF synchronizér + detekci hran (robustní pro ≤10 MHz).
- CS rámuje transakci, používej ho jako asynchronní reset čítače bitů/bajtů (start na CS↓).

## 3. Transakční model
- **Každá transakce = přesně 64 bajtů** (512 SCK hran), plně duplexně. CS↓ → 512 SCK → CS↑.
  Žádný jiný počet. CS↑ ukončuje/resetuje rámec.
- **Odpověď je PŘEDNAČTENÁ (pipelined):** obsah MISO = rámec připravený PŘED touto transakcí.
  FPGA nemůže odpověď odvodit z právě přijímaného MOSI. Výsledek příkazu z transakce N
  (např. shození FRESH po ACK) se projeví až v rámci čteném v transakci **N+1**.
- **Časování od STM (reálné hodnoty v `fpga_freq.c`, přes DWT):**
  - CS↓ → 1. SCK: **2 µs** (kontrakt ≥1 µs).
  - poslední SCK → CS↑: **2 µs** (≥1 µs).
  - **mezi rámci: 25 µs** (kontrakt ≥20 µs; FPGA má čas složit nový rámec + CRC).
  - poll perioda ~**100 ms** (10 Hz), takže reálné mezery jsou typicky 100 ms.
- Doporučený postup FPGA:
  1. **CS=1 (idle):** slož kompletní 64B TX rámec (DATA 0x80) z posledního měření, spočti CRC,
     **zalatchuj** do výstupního shift registru (žádné trhání přes clock domény).
  2. **CS=0:** shiftuj ven připravený rámec na MISO + shiftuj dovnitř MOSI do RX bufferu.
  3. **CS↑:** zvaliduj RX (MAGIC, CRC), zpracuj TYPE (ACK/START/STOP), připrav nový TX rámec.

## 4. Formát 64B rámce (společný oba směry, vícebajtová pole LITTLE-ENDIAN)
| Off | Délka | Pole | Popis |
|---|---|---|---|
| 0 | 1 | MAGIC | vždy **0xA5** |
| 1 | 1 | VERSION | vždy **0x01** |
| 2 | 1 | TYPE | viz §5 |
| 3 | 1 | FLAGS | od FPGA bitové (viz §6); od STM vždy **0x00** |
| 4 | 4 | SEQUENCE | u32 LE |
| 8 | 2 | PAYLOAD_LEN | u16 LE (FPGA v DATA posílá **50**) |
| 10 | 2 | RESERVED | 0 |
| 12 | 50 | PAYLOAD | viz §7 |
| 62 | 2 | CRC16 | u16 LE: byte62=LOW, byte63=HIGH |

## 5. TYPE
| TYPE | Význam | Směr |
|---|---|---|
| 0x80 | **DATA** (měřicí rámec) | FPGA → STM |
| 0x06 | **ACK** (potvrzení, SEQUENCE = echo) | STM → FPGA |
| 0x08 | START (informativní, měření beží i bez něj) | STM → FPGA |
| 0x09 | STOP (informativní) | STM → FPGA |

## 6. FLAGS (byte 3) v DATA rámci od FPGA — bitově
| Bit | Význam |
|---|---|
| 0 | DATA_VALID — máš reálné měření |
| 1 | DATA_FRESH — nové od posledního ACK |
| 2 | FIFO_EMPTY (= ~VALID) |
| 3 | rezerva (0) |
| 4 | rezerva (0) |
| 5 | RX_CRC_ERROR — minulý rámec od STM měl špatné MAGIC/CRC |
| 6 | ACK_OK — minulý platný rámec od STM přijat |
| 7 | ERROR (error_flags != 0) |

## 7. DATA payload (TYPE=0x80), absolutní offsety
| Off | Délka | Pole | Popis |
|---|---|---|---|
| 12 | 8 | freq_x100000 | u64 LE; Hz = hodnota/100000 (provizorně = edge_count) |
| 20 | 8 | edge_count | u64 LE; počet hran za bránu |
| 28 | 8 | gate_ns | u64 LE; délka brány v ns (provizorně 1e9 = 1 s) |
| 36 | 8 | timestamp | u64 LE; volně běžící čítač 10 MHz ticků |
| 44 | 1 | channel | u8 |
| 45 | 1 | meas_status | bit0=VALID, bit1=FRESH |
| 46 | 4 | error_flags | u32 LE (0 = OK) |
| 50 | 12 | reserved | nuly |

`freq_x100000 = edge_count * 100000` (provizorka). PAYLOAD_LEN pro DATA = 50.

## 8. CRC-16/CCITT-FALSE (PEVNÝ, bit-identický se STM)
- poly **0x1021**, init **0xFFFF**, BEZ reflexe vstupu/výstupu, BEZ xorout.
- Počítá se přes **byte 0..61**, výsledek: byte62 = LOW, byte63 = HIGH.
- **Kontrolní vektor: crc16("123456789") == 0x29B1** (STM to ověřuje self-testem při bootu).
```c
uint16_t crc16(const uint8_t* d, int n){           // n = 62
    uint16_t c = 0xFFFF;
    for(int i=0;i<n;i++){ c ^= (uint16_t)d[i]<<8;
        for(int b=0;b<8;b++) c = (c&0x8000)?(c<<1)^0x1021:(c<<1); }
    return c;
}
```

## 9. SEQUENCE / ACK / čerstvost
- FPGA **inkrementuje SEQUENCE při každém NOVÉM měření** (jinak STM nepozná čerstvost).
- STM bere měření jako platné, když: **CRC OK ∧ DATA_VALID=1 ∧ DATA_FRESH=1 ∧ SEQUENCE ≠ poslední přečtená.**
- STM posílá **ACK (TYPE 0x06)** se SEQUENCE = poslední přijatá platná SEQUENCE.
- Po VALIDNÍM ACK (CRC OK ∧ SEQUENCE souhlasí s naposledy odeslaným DATA) smí FPGA **shodit DATA_FRESH** (projeví se v dalším rámci — pipelined).

## 10. Co STM master reálně posílá (referenční chování, neměnné)
- **Boot:** STM počká ~250 ms (než FPGA dokonfiguruje), pak DWT/CRC self-test, pak pošle **START (0x08)**.
- **Auto-re-START:** když ~3 s nepřijde žádný platný rámec (mrtvý link), STM znovu pošle START
  → pokrývá FPGA, které nabootuje/resetuje až po STM. (Tj. nevadí, když FPGA naběhne později.)
- **Každý poll (~10 Hz):** rámec **ACK (0x06)** se SEQUENCE = poslední přijatá platná.
- TX rámce od STM: FLAGS=0, PAYLOAD_LEN=0, prázdný payload, **validní CRC**.
- STM **tiše ignoruje** odpovědi se špatným MAGIC nebo CRC (link pak hlásí jako mrtvý).
- CS: STM ho drží **HIGH v klidu** (od bootu), pulzuje LOW jen na transakci.

## 11. Bring-up plán (doporučené pořadí)
1. **Fáze 1 — dummy rámec:** FPGA vrací PEVNÝ DATA rámec, **SEQUENCE++ při každé transakci**,
   DATA_VALID=1, DATA_FRESH=1, správný CRC. Např. `freq_x100000 = 12345678901234`
   → STM displej musí ukázat **`123.456.789,01234Hz`**. UART `freq` / `fpgaraw` na STM to ověří.
   → Ověří framing, endianitu, CRC, MISO timing a hlavně že **MISO vůbec budíš** (řeší RX0:FF).
2. **Fáze 2 — reálný čítač:** napoj měřený kmitočet do freq_x100000/edge_count, SEQUENCE++ při
   novém měření, DATA_FRESH řiď přes ACK.
3. **Fáze 3 — rychlost:** STM je teď na 0,78 MHz; lze zvednout k ≤6 MHz, ověř integritu CRC.

## 12. Časté chyby (na co si dát pozor)
- **MISO Hi-Z / nebudíš → STM čte 0xFF (aktuální problém).** Při CS=0 aktivně shiftuj rámec.
- **První bit MISO musí být platný na CS↓** (CPHA=0), ne až po první hraně SCK.
- Endianita: vše LE; u64 freq = 8 bajtů LSB-first na offsetu 12.
- CRC: MSB-first, init 0xFFFF, bez reflexe; přesně 62 bajtů (0..61); 62=LOW, 63=HIGH.
- SEQUENCE se MUSÍ měnit, jinak STM bere měření jako staré.
- Přesně 64 bajtů; CS↑ resetuje. Odpověď je vždy ta **přednačtená** (pipelined), neodvozuj ji z aktuálního MOSI.
- MISO měň na **sestupné** hraně SCK (stabilní pro náběžnou, kterou STM vzorkuje).

## 13. STM-side reference (kontext)
- Driver: `CM7/Core/Src/fpga_freq.c`; `FpgaTask` poll 10 Hz; CS=PB12 (manuál, active-low).
- SCK = SPI2, MOSI=PB15, SCK=PI1, MISO=PI2. Mode 0, MSB, 8-bit, NSS software.
- Diagnostika na STM: displej řádek `SPI x.xxMHZ LINK/NOLINK ...`, UART `freq` a `fpgaraw`
  (výpis HAL stavu + všech 64 přijatých bajtů).
