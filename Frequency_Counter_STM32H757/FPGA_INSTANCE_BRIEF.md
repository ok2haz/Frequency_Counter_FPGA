# KONTROLNÍ ZADÁNÍ — FPGA slave (GW1NR-9 / Tang Nano 9K) ↔ STM32H757 SPI master

Jsi Claude pracující na **FPGA straně** (Gowin GW1NR-9, Tang Nano 9K). FPGA je SPI
**slave** + čítač kmitočtu. Protistrana (STM32H757, Cortex-M7 @ 480 MHz, SPI master)
je **hotová a nasazená** — tento dokument popisuje PŘESNĚ její reálné chování, ať je
FPGA s ní 100% konzistentní. Hodnoty zde odpovídají skutečnému kódu `fpga_freq.c`
na STM (ne přáním). STM se NEMĚNÍ — přizpůsobuje se FPGA jen tam, kde je to označeno.

---

## 0. STAV: LINK FUNGUJE (sekce ponechána jako troubleshooting)
Bring-up je hotový — STM hlásí `LINK:OK`, protokol vč. 4-fázového reciprokého
měření (`PH:F/F`) je v provozu. Níže ponecháno pro případ regrese:

Kdyby STM hlásil **`NOLINK HAL:OK RX0:FF`**:
- `HAL:OK` = STM úspěšně vyclockuje 64 B (SCK + CS jedou).
- `RX0:FF` = na MISO čte **samé jedničky** → **FPGA na MISO nic nebudí.**
- `CRC:0` = ani se nedojde ke kontrole CRC (padá hned na `MAGIC != 0xA5`).

➜ Při CS=0 MUSÍ FPGA aktivně budit MISO připraveným rámcem (MSB napřed),
ne ho nechat v Hi-Z / pull-upu. Ověř, že:
1. FPGA je nakonfigurované a běží (config z flash doběhla).
2. MISO (pin 57) je **výstup** a mění se během CS=0.
3. CS (pin 56) na FPGA reálně padá do 0 (STM ho drží HIGH v klidu, pulzuje na LOW).

---

## 0b. HODINY / CLOCKING (Si5356A → FPGA)
Externí clock generator **Si5356A** (řízený STM po I2C1) dodává FPGA 4× **100 MHz**,
vzájemně posunuté po **90°** (= 0/2,5/5/7,5 ns — vernier TDC s jemným krokem 2,5 ns;
reference Si5356 = externí **10 MHz CLKIN**):
| Si5356 výstup | fáze | posun | FPGA pin |
|---|---|---|---|
| CLK0 | 0°   | 0 ns   | PIN33 |
| CLK1 | 90°  | 2,5 ns | PIN34 |
| CLK2 | 180° | 5,0 ns | PIN40 |
| CLK3 | 270° | 7,5 ns | PIN35 |

⚠️ **MUSÍ být 90° rozestupy, NE 45°** (dřívější konfigurace) — při 45° jemné kódy
nesedí na 2,5ns mřížku TDC → systematická chyba kmitočtu. Konfiguraci fází drží
STM (Si5356 register map, viz CLAUDE.md „Si5356A"); FPGA jen kontroluje 90° rozestup
(fine_seen v `phase_status`).

**Navíc jde do FPGA přímo i 10 MHz z GPSDO** (tatáž reference, ze které Si5356
odvozuje 100 MHz → domény jsou koherentní). Využití: časová báze `timestamp`
(10MHz ticky), health-check (detekce výpadku 10 MHz nezávisle na Si5356 LOS),
do budoucna fázové porovnání / poměrové měření 100 MHz vs 10 MHz. 10 MHz se
VZORKUJE 100MHz doménou (čítá se) — neclockuje logiku → stačí běžné IO na headeru.

### Fyzická mapa headerů (ze schématu Tang Nano 9K — pozice: FPGA pin)
- **J5** (vše 3,3 V): 1–4: `38·37·36·39` (TF) | 5–10: `25·26·27·28·29·30` | 11–14:
  `33·34·40·35` | 15–18: `41·42·51·53` | 19–22: `54·55·56·57` (SPI→STM) | 23/24: HDMI_CK.
  **⚠️ J5 nemá žádný GND pin** → základní deska musí dát GND plochu pod modul;
  guardy 29/30 (25/26) nastavit ve FPGA jako výstupy v log. 0 (stínění vstupů 27/28).
- **J6**: 1: `63` (RPLL_T_in, strap) | 2–9: `86…79` (**1,8V banka!**, 86=BL_PWM) |
  10/11: `77·76` (SPILCD) | 12–17: HDMI | 18: +5V | 19/20: `48·49` | 21/22: `31·32`
  (strapy) | 23: GND (jediná zem).
- **Doporučené přiřazení:** 100 MHz = blok J5-11…14 (`33·34·40·35`, 0° na **35**),
  expanze CLK4–7 = J5-15…18 (`41·42·51·53`); **10 MHz z GPSDO = pin 63 (J6-1)** —
  druhý header (daleko od vstupů 27/28 i od 100MHz bloku) a zároveň RPLL_T_in
  (budoucí opce 10 MHz → rPLL); záloha 48/49. Piny `25/26/29/30` NIKDY pro hodiny
  (obklopují měřicí vstupy 27/28) — jsou to guardy.

### Clock-capable piny na headeru (analýza UG803 GW1NR-9C QN88P + schéma Tang Nano 9K)
Tang Nano 9K (GW1NR-LV9**QN88P**) má z headeru dostupné tyto clock pady:
| Pin | IO | Funkce | Dostupnost | Pozn. |
|---|---|---|---|---|
| **35** | IOB29A | **GCLKT_4** | header ✅ (sdílené s RGB-LCD CK — nepoužito) | **jediný pravý single-ended global-clock vstup na headeru** |
| 36 | IOB29B | GCLKC_4 | header ✅ (sdílené s TF SCLK) | ⚠️ single-ended na GCLKC **NENÍ** global clock (UG803: C-pin je clock jen v diff. páru) |
| 51 | IOR17B | GCLKC_3 | header ✅ (RGB B5) | totéž omezení GCLKC |
| 52 | IOR17A | GCLKT_3 | ❌ obsazeno 27MHz krystalem modulu | |
| 10 | IOL15A | GCLKT_6 | ❌ LED1, není na headeru | |
| 63 | IOR5A | **RPLL_T_in** | header ✅ (RGBINIT strap) | dedikovaný vstup rPLL (ne přímý GCLK) |
| 4 | IOL5A | LPLL_T_in | ❌ tlačítko S2 (1V8!) | |

**Doporučení (bez zásahu do HW — jen prohození fází v Si5356 + constraints):**
1. **0° (master) → PIN 35 (GCLKT_4).** Master 100 MHz clockuje SPI oversampling
   i TDC → patří na jediný pravý GCLK pad. Dnes na 35 sedí CLK3 (270°) a master 0°
   na obyč. PIN 33 → **prohodit se řeší čistě přeprogramováním fází výstupů Si5356
   na STM straně** (fáze je per-output registr; fyzické dráty zůstávají).
2. **180° a 270° NEVODIT externě — vyrobit interní inverzí** (negedge/DDR registry
   IOB): inverze je přesných 180° bez dodatečného skew a bez dalších vstupů.
   → Si5356 pak stačí 2 fáze (0° na PIN35, 90° např. na PIN34/63) a 2 výstupy
   Si5356 se uvolní. Pro vernier TDC architektonicky čistší (2 domény místo 4).
   **Trade-off rozlišení (čísla ze Si5356A datasheetu):** krok TDC zůstává 2,5 ns
   v obou variantách; u DDR závisí poloha bodů 5/7,5 ns na střídě (spec 45–55 %
   worst case → posun až ±500 ps, typicky míň), u 4 externích fází na output-output
   skew Si5356 (±150 ps) + fabric-routing skewu 3 ne-GCLK hodin (stovky ps). Obojí
   je STATICKÉ → kalibrovatelné code-density testem (histogram fine kódů na
   asynchronním signálu); dopad na kmitočet ≤ ~4e-9/okno, pod kvantizací (2e-8).
   Výhoda 4 fází: každý bod laditelný fázovým registrem Si5356 (LSB 3,55 ps).
3. Pokud FPGA design zůstane u 4 externích fází: 0°→35, zbylé na 33/34/40 jako
   dnes (funguje, `PH:F/F`) — fabric routing fází je OK, statický skew se stejně
   kalibruje; jen master nechat na 35.
4. PIN 63 (RPLL_T_in) držet v záloze pro budoucí clock vstup (např. 10 MHz do
   rPLL, kdyby bylo potřeba násobit lokálně).

**Varianta C — 1 externí clock + fáze interně (UG286, GW1NR-9C podporováno):**
Jediný čistý 100 MHz ze Si5356 → PIN 35 (GCLKT_4) → **rPLL**: CLKOUT (0°) +
CLKOUTP s **PSDA** fázovým posuvem (krok 22,5° = **625 ps @ 100 MHz** → přesných
90°), jemné dorovnání **FDLY** (krok 125 ps) a střída **DUTYDA** (krok 1/16
periody) → 180°/270° přes IDDR s laditelnou sestupnou hranou. K dispozici i
**DLLDLY** (dynamické zpoždění hodin, krok ~30 ps, 8bit rozsah) na jemnou
kalibraci mřížky. Fáze 0/90 jsou pak odvozené z VCO tapů jednoho PLL →
uniformita mřížky výborná, jitter rPLL je náhodný (průměruje se), statický
offset PLL je společný oběma timestampům (vyruší se v Δt). Výhoda: 1 pin
(zrovna ten jediný pravý GCLK), žádné fabric-routované hodiny, 3 výstupy Si5356
volné. Nevýhoda: přestavba clockingu v designu + ověřit jitter rPLL v DS117.
Srovnání jemnosti ladění: Si5356 fázový registr **3,55 ps** LSB (nejjemnější,
externí) vs FDLY 125 ps / DLLDLY ~30 ps (interní).

**Rozšíření na 8 fází (1,25ns mřížka) — HW opce:** Si5356A má 8 výstupů a fázi
laditelnou **per výstup** (LSB 3,55 ps, chyba <20 ps, rozsah ±45 ns, bez SS) →
8× 100 MHz po 45° je v možnostech čipu. FPGA: 8 primárních GCLK sítí = strop
GW1NR-9 (7 z 8 přes fabric — statický skew lze dorovnat fázovými registry Si5356
u zdroje). Doporučené piny pro CLK4–7: **41/42/51/53** — navazují na 35 (souvislý
blok hodin 33→53, sdílené jen s nepoužitým RGB-LCD) a jsou DALEKO od měřicích
vstupů 27/28. ⚠️ **NEdávat hodiny na 25/26/29/30** — na headeru obklopují měřicí
vstupy 27/28 (pořadí `…25 26 27 28 29 30 33…`) → přeslech 100 MHz přímo do
měřeného signálu; tyhle piny naopak nechat klidné (guard). Dále se vyhnout
79–86 (1,8V banka!), 5–8 (JTAG), 17/18 (budí je BL702 UART), 31/32/50/63
(RGBINIT strapy), 36–39 (TF), 68–75 (HDMI pull-upy). Elektrika: sériové ~33 Ω
u Si5356, krátké stejně dlouhé spoje, zem co nejblíž. Levnější mezikrok se STEJNÝMI 4 dráty:
přeprogramovat fáze na 45° (0/45/90/135) + IDDR obě hrany = týchž 8 bodů
(daň: sestupné hrany závisí na střídě 45–55 % → ±0,5 ns w.c., kalibrovatelné).
Zisk 1,25ns mřížky: σy @1 s ~6,5e-10 (2× proti 2,5 ns).

**Vernier čistě uvnitř FPGA (náměty k budoucímu zjemnění, řazeno zisk/práce):**
1. **DLLDLY dithering** — DLLDLY posouvá vzorkovací hodiny známým vzorem
   (~30ps kroky) okno od okna → kvantizační mřížka se rozmaže a průměrovaná
   měření konvergují pod 2,5ns LSB. Jedna primitiva, minimální zásah. Nejlepší
   poměr zisk/práce.
2. **Dual-PLL vernier (beat):** 2. rPLL na 100×(N+1)/N (např. 26/25 = 104 MHz →
   krok T1−T2 = 385 ps; 34/33 → 294 ps; PFD ≥ 3 MHz, VCO 400–1200 MHz — ověřit
   v PLL kalkulátoru). Koherentní s referencí, poměr exaktní. Rizika: kumulace
   jitteru obou PLL přes ~N cyklů konverze (→ efektivně ~150 ps), koincidenční
   detektor = metastabilita (nejtěžší část). Konverze ~250–400 ns/událost —
   pro reciproké timestampy OK.
3. **Carry-chain TDC** (~30–60 ps/tap): nejjemnější, ale vyžaduje průběžnou
   code-density kalibraci (taps = PVT drift), bubble korekci a metastabilitní
   ošetření. Až jako poslední krok.
   **Recept na linearitu (kdyby na něj došlo):** (a) hybrid — hrubý čítač +
   4fázový kód + KRÁTKÝ řetěz jen ~3 ns (~40–60 tapů; 4× menší INL než linka
   přes 10 ns); (b) placement: ALU sloupec zamknout CST constraintem, vzorkovací
   FF u tapu, tapy registrovat 2×, bubble-tolerantní dekodér; (c) linearizace
   code-density testem — FPGA posílá SYROVÉ kódy (rezerva protokolu v2),
   histogram + převodní LUT počítá STM; (d) live měřítko: periodicky změřit
   počet tapů na periodu 100 MHz → ratiometrická PVT normalizace; (e) upgrade
   průměrné linearity: DLLDLY dither (DLL-vázané ~30ps kroky = lineární
   z konstrukce) nebo Wave Union. Akceptace: DNL<0,5 LSB, INL<2 LSB po
   kalibraci, stabilní přes teplotní přejezd.
   Kontext: s Ω-regresí na STM naráží dnešní 2,5ns mřížka na strop reference
   u τ≈100 s; dual-PLL to posune na ~10 s, carry-chain pod ~3 s. Pro delší τ
   zlepšení TDC nic nepřidá.

⚠️ **KRITICKÉ pro SPI:** FPGA SPI slave oversampluje SCK na ~100 MHz → potřebuje
**funkční 100 MHz hodinovou doménu**. Systémový/oversampling clock musí přijít na
**clock-capable (GCLK/PLL) pin** GW1NR-9 — ne na obyčejné I/O. Pro master clock použij
**CLK0 (0°, PIN33)**; fázově posunuté (90/180/270°) jsou pro *měřené* signály, ne jako
hlavní clock. Pokud clock tree/PLL nenalockuje, SPI logika neběží → STM vidí `RX0:FF`.
**Ověř: vede 100 MHz na clock-capable pin a PLL/global buffer v designu lockuje?**

## 0c. MĚŘICÍ FRONT-END (ze schématu `../Frequency_Counter_FPGA_Module/FPGA_module_schematic.pdf`, list 2 `FPGA_Core`)
Řetězec měřeného signálu **PŘED** vstupem do FPGA (samostatné čipy na desce, ne fabric):
- **Vstupy:** 2 kanály — **CH A (`J1`) / CH B (`J2`)**, tvarované komparátorem **`MAX9601`**
  (`U1`, dual ultrarychlý komparátor). Toto je „tvarovač"; **strop řetězce ~1,4 GHz.**
- **Dělič = JEDINÝ čítač `MC100EP016A`** (`U2`, 8-bit synchronní binární čítač, ECL, ~1,4 GHz).
  Jeho Q výstupy jsou binární odbočky: **Q1 = ÷4**, **Q3 = ÷16**. Výstupy jdou přes
  ECL→LVTTL převodníky **`MC100EPT23DT`** (`U20`/`U24`) na 3,3 V (`DIV_Bit…`) do FPGA.
- **Mapa na FPGA:** **pin28 = /4** (primár), **pin27 = /16** (rozšíření rozsahu). Guardy
  25/26/29/30 (stínění 27/28) — viz header mapa §0b.

⚠️ **`/4` a `/16` NEJSOU nezávislé** — jsou to dvě odbočky TÉHOŽ čítače nad TÍMŽE tvarovačem.
Miscount čítače / výpadek tvarovače u 1,4 GHz se projeví v OBOU shodně. Porovnání `freq_x100000`
(/4) vs `freq16_x100000` (/16) na STM je proto jen **downstream sanity** (chytne rozbité čítání
na jednom pinu ve FPGA), **ne nezávislá validace front-endu.** `/16` primárně = **rozsah nad
~400 MHz** (kde by /4 výstup přesáhl ~100 MHz strop fabricu). Skutečně nezávislý cross-check by
vyžadoval druhý samostatný dělič (ideálně nesoudělný poměr) = HW úprava desky.

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
  - poll perioda ~**50 ms** (20 Hz), takže reálné mezery jsou typicky 50 ms.
    Polling je úmyslně rychlejší než tempo měření (~4 nová měření/s) kvůli latenci;
    protokol je pull/ACK, rychlejší polling měření neztrácí.
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

## 7. DATA payload (TYPE=0x80), absolutní offsety — AKTUÁLNÍ (v provozu)
| Off | Délka | Pole | Popis |
|---|---|---|---|
| 12 | 8 | freq_x100000 | u64 LE; **pin28 /4** (primár); Hz = hodnota/100000; **dělička /4 UŽ zahrnuta** (STM nenásobí) |
| 20 | 8 | edge_count | u64 LE; počet period v reciprokém okně (diagnostika) |
| 28 | 8 | gate_ns | u64 LE; skutečné Δt okna [ns], ≈250e6 (reciproké → kolísá) |
| 36 | 8 | timestamp | u64 LE; volně běžící čítač 10 MHz ticků |
| 44 | 1 | channel | u8 |
| 45 | 1 | meas_status | bit0=VALID, bit1=FRESH |
| 46 | 4 | error_flags | u32 LE: bit0=meas err (/4, Δt==0), bit1=**SIGNAL_LOST** (watchdog ~2,5 s bez měření; zároveň VALID=0), bit2=overflow (okno >~21,5 s) |
| 50 | 1 | **phase_status** | bity3:0=present[3:0] (živost 4 fází), bity7:4=fine_seen[3:0] (viděné jemné 2,5ns kódy); **zdravé = 0xFF** |
| 51 | 1 | **status2** | bit0 = chyba dělení pin27 (/16, Δt==0) |
| 52 | 8 | **freq16_x100000** | u64 LE; **pin27 /16** (vyšší rozsah); dělička /16 UŽ zahrnuta |
| 60 | 2 | spare | nuly |

PAYLOAD_LEN pro DATA = 50. Pozn.: SEQUENCE u nízkých kmitočtů roste pomaleji
(okno čeká na hrany) — to NENÍ chyba; ztrátu signálu hlásí error_flags bit1.

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
- **Každý poll (~20 Hz):** rámec **ACK (0x06)** se SEQUENCE = poslední přijatá platná.
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
- Driver: `CM7/Core/Src/fpga_freq.c`; `FpgaTask` poll 20 Hz; CS=PB12 (manuál, active-low).
- SCK = SPI2, MOSI=PB15, SCK=PI1, MISO=PI2. Mode 0, MSB, 8-bit, NSS software.
- Diagnostika na STM: displej řádek `SPI x.xxMHZ LINK/NOLINK ...`, UART `freq` a `fpgaraw`
  (výpis HAL stavu + všech 64 přijatých bajtů).
