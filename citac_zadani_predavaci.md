# Přesný frekvenční čítač — předávací zadání

Dokument shrnuje architektonická rozhodnutí pro dva navazující úkoly:
**návrh vstupního modulu** a **firmware FPGA**.

Deska FPGA je hotová a schéma zafixované. Rozhraní popsaná v §3 a §4 jsou
smluvní — na nich stojí obě navazující práce.

Značení: **[D]** ověřeno z datasheetu, **[M]** změřeno, **[O]** odhad, **[?]** neověřeno

---

# 1. Cíl přístroje

Dvoukanálový reciproční čítač s GPSDO. Priorita: **rozlišení a absolutní
přesnost**, ne maximální kmitočet.

| Parametr | Cíl | Omezeno čím |
|---|---|---|
| Rozsah přímo | DC – **200 MHz** | `SY100ELT23L` f_MAX **[D]** |
| Rozsah přes ÷10 | do 1,1 GHz, jen frekvence | `MC12080` |
| Single-shot TI | **~22 ps** | carry chain, bin 50 ps **[O]** |
| Frekvence @1 s, N=1000 | 1,8×10⁻¹² | TDC + regrese |
| Trigger level | ±1,024 V, krok 0,50 mV | `MCP4728` 12 bit |
| Hystereze | 1–60 mV | `MAX9601` **[D]** |
| Funkce | f, T, TI, fáze, poměr, totalize, ADEV, Adret režim | |

Srovnání: Pendulum CNT-91 má 300 MHz a 50 ps, Keysight 53230A 350 MHz a 20 ps.
Oba řeší vyšší kmitočty oddělenou cestou, která TI neumí — stejně jako my.

---

# 2. Rozdělení funkcí mezi desky

```
VSTUPNÍ MODUL (jeden, oba kanály)          ← úkol A
  2× analogový řetězec
  MAX9601 dvojitý + MCP4728 + OPA2277      ← práh a hystereze ZDE
  SY100ELT23L dvojitý translátor
  ADS1115, TMP117, 2× MCP23017
  B0505S-1WR3 → −5 V lokálně
  2× MC12080 ÷10

DESKA FPGA (hotová)
  Tang Nano 9K, GPSDO, Si5356A, napájení z VBUS

DESKA STM32 (existuje)
  STM32H747, displej, 64 MB SDRAM, Ethernet
```

**Klíčové rozhodnutí:** komparátor je ve vstupním modulu, protože pak přes
kabel teče rozhodnutá logika, ne analog. Zároveň tím zůstává `MAX9601`
dvojitý pro oba kanály — dvě sekce jednoho substrátu driftují společně
(skew 15–70 ps, drift **0,12 ps/°C** **[D]**), dvě pouzdra ne.

---

# 3. Rozhraní modul ↔ deska FPGA — SMLUVNÍ

## Koaxy

```
2× SMA, 50 Ω:  CH_A, CH_B
```

Signál: **LVTTL z `SY100ELT23L`**, hrana ~350 ps, do 200 MHz.

Zakončení: sériové `49R9` u budiče v modulu, paralelní `R19`/`R20 49R9`
na desce FPGA (osazeno).

SMA místo U.FL kvůli 500 cyklům místo 30 a možnosti připojit osciloskop
při kalibraci offsetu.

## Konektor J4, 20 pinů

Přes konektor **neteče žádný analogový signál.** `RF_Level` se měří ADC
přímo v modulu.

| Pin | Signál | Poznámka |
|---|---|---|
| 1, 2 | GND | |
| 3 | `SPI2_SCK` | rezerva, dnes nepoužito |
| 4 | GND | |
| 5 | `SPI2_MOSI` | rezerva |
| 6 | GND | |
| 7 | `SPI2_CS` | rezerva |
| 8 | GND | |
| 9 | `Reset` | |
| 10 | GND | |
| 11 | rezerva | |
| 12 | GND | |
| 13 | `SDA` | |
| 14 | GND | |
| 15 | `SCL` | |
| 16 | GND | |
| 17 | rezerva | |
| 18 | +5V | **zdvojit, viz níže** |
| 19, 20 | GND | |

**Proud:** cívka `G6K-2F-Y` bere 40 mA. Osm relé naráz = 320 mA. Na FFC
20 cm to je ~320 mV úbytku. **Zdvoj napájecí vodiče** a ve firmwaru
nespínej relé naráz — rozděl je do skupin po pár ms.

## I²C adresní mapa — SMLUVNÍ

| Adresa | Zařízení | Deska | Strap |
|---|---|---|---|
| 0x20 | `MCP23017` #1 | modul | A0–A2 → GND |
| 0x21 | `MCP23017` #2 | modul | A0 → V+ |
| 0x48 | `ADS1115` | FPGA | ADDR → GND |
| 0x49 | `TMP117` | FPGA (OCXO) | ADD0 → V+ |
| **0x4A** | `TMP117` | **modul** | ADD0 → SDA |
| **0x4B** | `ADS1115` | **modul** | ADDR → SCL |
| 0x4C | `AD5693R` | FPGA | A0 → GND |
| 0x60 | `MCP4728` | modul | tovární |
| 0x70 | `Si5356A` | FPGA | I2C_LSB → GND |

Pull-upy **jen na desce FPGA**: `R64`, `R65` po **2k2**. Do modulu žádné.

Rychlost: **100 kHz**. Sběrnice je vytížená pod 10 %, limitem jsou relé
(5 ms) a převody ADC (1,2 ms), ne sběrnice.

Kapacita odhadem 210 pF z limitu 400 pF **[O]** — změř náběžnou hranu na
`SDA`; nad 1 µs sniž pull-upy na 1k5 (minimum je 967 Ω).

**`General Call 0x06` nikdy neposílej** — resetuje `MCP4728`, `TMP117`
i `ADS1115` naráz.

---

# 4. Přiřazení pinů Tang Nano 9K — SMLUVNÍ

| Pin | FPGA pin | Signál | Poznámka |
|---|---|---|---|
| 1–4 | PIN38, 37, 36, 39 | **nezapojeno** | TF slot osazený, pahýly **[D]** |
| **5** | **PIN25_IOB8A** | **`CH_A`** | carry chain A |
| 6 | PIN26_IOB8B | volný | **odstup** |
| **7** | **PIN27_IOB11A** | **`CH_B`** | carry chain B |
| 8 | PIN28_IOB11B | volný | odstup |
| 9, 10 | PIN29, PIN30 | LED stavu | statické, ne PWM |
| 11 | PIN33_IOB23A | `GPS_1PPS` | carry chain C |
| 12, 13 | PIN34, PIN40 | volné | |
| **14** | **PIN35_IOB29A** | **`REF_100MHz`** | **`GCLKT_4`, PRIMARY [M]** |
| 15 | PIN41_IOB41A | `Ref_Ctrl` | statický, **oddělovač referencí** |
| 16 | PIN42_IOB41B | `REF_10MHz` | záloha, surová z OCXO |
| 17 | PIN51_IOR17B | `Ext_Ref_Sens` | 10 MHz monitor, jiný bank |
| 18 | PIN53_IOR15B | volný | odstup od SPI |
| 19–22 | PIN54–57 | `MOSI`, `SCK`, `FPGA_CS2`, `MISO` | SPI1 |
| 25 | +3V3 | **nezapojeno** | modul má vlastní DC/DC |
| 26 | GND | zem | jediná v headeru |
| 27, 28 | PIN32, PIN31 | volné | **rezerva pro alternativu** |
| 29 | PIN49_IOR24A | `Data_RDY` | přerušení do STM32 |
| 31 | +5V | přes ferit + Schottky | |
| 40–47 | IOT_1V8 | **nepoužívat** | bank 1,8 V **[D]** |
| 48 | PIN63_IOR5A | rezerva | GCLK nedoložen |

**Zdůvodnění klíčových voleb**

`CH_A` a `CH_B` s odstupem, ne u země: zemní posuv je oběma společný a
v rozdílu A−B se odečte, kdežto vzájemný přeslech se neodečte a mění se
s měřeným intervalem. Jedna volná pozice sníží vazbu ~4×.

`REF_100MHz` na pin 14: 100 MHz → 500 MHz je ×5, tedy **+14 dB** násobení
fázového šumu, proti +34 dB u 10 MHz. Ověřeno, že pin má `GCLKT_4` a
signál jde na `PRIMARY` síť **[M]**.

Obě reference oddělené `Ref_Ctrl`: jsou koherentní (100 MHz vzniká z 10 MHz),
takže jejich přeslech by se neprůměroval. **Ve firmwaru drž vždy jen jednu aktivní.**

---

# 5. Zadání A — vstupní modul

## Pořadí bloků (kritické)

```
BNC → OCHRANA → K_CAL → relé AC/DC → relé 50Ω/1MΩ → útlum 0…−27 dB
   ├ 1MΩ:  JFET převodník → lineární OZ, DC vázaný ──────┐
   └ 50Ω:  1–2× ERA-3SM (AC) ─┬ AD8307 detektor          ├ relé volby (3 cesty)
                               └ MC12080 ÷10 ────────────┘
   → MAX9601 dvojitý + MCP4728 + OPA2277
   → SY100ELT23L → 49R9 → SMA
```

**Ochrana hned za BNC**, ne až u JFETu — jinak relé a útlum dostanou plné
vstupní napětí.

**Odbočka na AD8307 až za zesilovačem**, ne z útlumu — detektor musí měřit
tu samou amplitudu, jakou vidí komparátor.

**Předdělička je paralelní větev na 50Ω cestě**, ne článek v řadě. Přímá
50Ω cesta ji obchází.

## Kalibrační relé K_CAL

Hned za ochranou, dvoupólové. V kalibračním režimu pustí do obou kanálů
společný signál `CAL_SRC` (volný výstup `Si5356A`) přes symetrický
rozbočovač `2× 51R`.

**Oba odpory ze stejného balení, spoje naprosto stejně dlouhé** — 1 mm ve
FR4 je 6,7 ps, tedy třetina rozlišení.

Kalibruj **při několika amplitudách** a ulož tabulku — dispersion `MAX9601`
je až 40 ps a mění se s overdrive **[D]**.

Pull-down na budiči relé je povinný: `MCP23017` se probouzí se vstupy ve
vysoké impedanci a čítač nesmí startovat v kalibračním režimu.

## Napájení MAX9601

| Pin | Napětí |
|---|---|
| `VCC` (14, 7) | +5 V |
| `VEE` (15, 6) | −5 V |
| `VCCO` (18, 3) | **+3,3 V přes ferit** |
| `/LE` (16) | na `VCCO` |
| `LE` (17) | GND |

`V_S` = 10 V, střed okna 9,5–11,5 V **[D]**. Souhlasný rozsah vstupů
−2 až +3 V, práh ±1,024 V je uvnitř s rezervou.

`VCCO` odděl feritem s 10 µF — emitory berou stálých 53 mA a spínají při
každé hraně; bez oddělení se to propíše do `MCP4728` a odtud do prahu.

Théveninovo zakončení výstupů: **130R na +3V3 / 82R na GND**, `V_T` = 1,276 V,
`R_th` = 50,3 Ω. Datasheet žádá pulldown 50–75 Ω a `V_T` = `VCCO` − 2 V **[D]**.

`B0505S-1WR3` lokálně. Zátěž: MAX9601 24–33 mA + OPA2277 1,6 mA +
lineární OZ ~24 mA + bleeder → **~75 mA**, tedy 37 % jmenovité zátěže.
Ber **regulovanou verzi** — neregulovaná by nechala `V_S` plavat.

## Prahový řetězec

```
MCP4728 kanál A → R 10k0 0,1% → sumační uzel → OPA2277 [−]
MCP4728 kanál C → R 1k0 → C 1u → V_MID = 0,512 V → OPA2277 [+]
zpětná vazba: R 10k0 0,1% ‖ C 22p
výstup → R 47R → C 100n + 1n u pinu → MAX9601 IN−

přenos: V_TRIG = 1,024 − V_DAC,  ±1,024 V, krok 0,50 mV
```

Hystereze **bez zesilovače** — přímo z kanálu D přes `12k7` na oba piny `Hys`:

```
I_HYS = (2,500 − V_DAC) / 12k7  →  197…36 µA  →  ~48…10 mV
```

Pin `Hys` je interně na 2,5 V, tedy proudově řízený vstup. Zesilovač by byl
buffer za bufferem. **Bonus:** DAC se fyzicky nedostane nad 2,048 V, takže
proud nemůže obrátit směr — chyba vyloučená topologií.

**Zvaž zisk ×2 na kanálu D** a `R` na 10k0 → plný rozsah čipu 1–60 mV.

`LDAC` na GPIO nebo `0R` s testpadem — na GND natvrdo přijdeš o možnost
přeprogramovat I²C adresu.

**OZ:** `OPA2277UA` (SOIC-8). JFET nemá — je bipolární, ale I_B = 1 nA dá
přes 10k jen 10 µV. Vos 20 µV, drift 0,15 µV/°C. Alternativa `OPA2134`.
**Nepoužívej** `TLV9001` ani `OPA365` — max. napájení 5,5 V, na ±5 V je zničíš.

## CHYBÍ: lineární DC zesilovač

Jediný blok, který v dnešním modulu není a **bez kterého trigger level
nemá absolutní význam.**

Zisk je svázaný s rozsahem prahu a s pracovním bodem JFET sledovače:

| Zisk OZ | Citlivost | Rozsah prahu na vstupu |
|---|---|---|
| ×1 | 8 mV_ef | ±1,14 V |
| ×3 | 2,6 mV_ef | ±380 mV |
| ×10 | 0,8 mV_ef | ±114 mV |

Pro triggerování na 5V logice chceš rozsah aspoň ±2,5 V. **Zvaž rozšíření
prahu na ±2,048 V**: `R` zpětné vazby na 20k0, `V_MID` na 0,683 V,
přenos `V_TRIG = 3·V_MID − 2·V_DAC`, krok 1,0 mV.

Kandidáti: `ADA4857-1`, `AD8055`, `OPA692`. Napájení ±5 V, DC vazba,
šířka pásma > 300 MHz.

**Tuto trojici navrhni najednou** — samostatně to nejde.

## Předdělička

`MC12080` (onsemi, datasheet rev. 8 z 11/2024, stále vyráběný):
÷10/20/40/80, 1,1 GHz, jedno napájení +5 V, **výstup ~1,2 V_pp**.

Ten výstup je klíčový — není TTL, takže ho AC vazbou pustíš do komparátoru
bez převodu úrovní. TTL by přesáhl souhlasný rozsah `MAX9601` (`VCC` − 2 V).

Dělicí poměr přepínatelný piny SW1–SW3 z `MCP23017`.

**Bez signálu se sama rozkmitá** — potlač odečet podle `RF_Level`.
Přepínání cest dělej **s hysterezí 10 %**, jinak bude čítač na hranici
cvakat mezi režimy s různým zpožděním.

## ADC v modulu

`ADS1115` na 0x4B, `FSR` nastavitelný per kanál (`PGA` je v konfiguračním
registru, který stejně přepisuješ):

| Kanál | Signál | FSR |
|---|---|---|
| AIN0 | `RF_Level_A` | ±4,096 V (AD8307 dá až 2,4 V) |
| AIN1 | `RF_Level_B` | ±4,096 V |
| AIN2 | monitor −5 V | ±4,096 V |
| AIN3 | +5 V za konektorem | ±4,096 V |

Monitor −5 V: `R 7k5` z +5V, `R 10k0` z −5V, **`D 1N4148W`** katodou na
uzel, anodou na GND. Přenos −5,0 V → 0,714 V; meze −4,5 V → 0,929 V,
−6,5 V → 0,071 V.

Ta dioda **je potřeba** — při vypínání může +5 V klesnout dřív než −5 V.
`1N4148W`, ne Schottky: svod 25 nA proti 2 µA, na 4,3 kΩ to je rozdíl
0,11 mV proti 8,6 mV.

Absolutní maximum vstupu je **`VDD` + 0,3 V = 3,6 V** bez ohledu na `PGA`.

## Řízení relé

`2× MCP23017` na 0x20 a 0x21, 32 výstupů (potřeba 19).

Výstup dá 25 mA, cívka `G6K-2F-Y` bere 40 mA → **potřebuješ budiče**,
`ULN2803` nebo diskrétní `2N7002` s flyback diodami.

Alternativa: `TPIC6C596` na SPI — 100 mA otevřený drain s integrovanými
flyback diodami, budí relé přímo, ale nemá zpětné čtení a přidává 4 vodiče
do konektoru.

## Layout modulu

- Ochrana hned za konektorem, před vším ostatním
- Sumační uzly prahového řetězce **fyzicky malé**, nic spínajícího pod nimi
- `12k7` blízko komparátoru, ne blízko DACu
- `AD8307` → `ADS1115`: sériový 1k a 10n u pinu ADC
- Rozbočovač K_CAL: oba spoje **stejně dlouhé**
- Zakončení `49R9` u budiče do koaxu

---

# 6. Zadání B — firmware FPGA

## TDC architektura

**Carry chain, ne Gowin TDC IP.**

Gowin TDC IP (`IPUG1208`) je čistě fabric, 64 LUT + 36 REG, `PLL → CLKDIV(4)`,
`Precision = 1/(fclk×2)` **[D]**. Při `CLKOUT` 600 MHz to je **833 ps**, tedy
340 ps single-shot TI. Podlaha regrese 2,6×10⁻¹¹ leží **nad stabilitou OCXO** —
Adret režim ani ADEV pod 10⁻¹¹ by nefungovaly.

Carry chain dá odhadem **50–100 ps** (55nm proces) **[O]**, tedy ~22 ps
single-shot a podlahu 1,8×10⁻¹².

**Riziko:** carry-chain TDC na Gowinu nikdo nepublikoval **[?]**. Toolchain
na tuhle disciplínu není ověřený.

**Doporučený postup:** rozjeď to nejdřív na Gowin TDC IP (340 ps) a ověř
celý řetězec — úrovně, symetrii, kalibraci offsetu, gap-free razítkování,
regresi. V tom stavu už měříš frekvenci, periodu, poměr a totalize.
Carry chain přidej, až budeš mít čím měřit sám sebe.

## Hodinový plán

```
REF_100MHz (pin 14, GCLKT_4) → PLL1
   CLKOUT  400 MHz  →  CLKOUTD ÷2  →  200 MHz vzorkování carry chainů
   CLKOUTD 100 MHz  →  hrubý čítač
```

**`SDIV` musí být sudý [M]** — proto 400 MHz, ne 500. Při 500 MHz vyjde
`CLKOUTD` na 83,3 MHz, ne 100.

Vzorkuj na **200 MHz**, ne 400. Změřené f_MAX je 203,6 MHz pro 25bitový
čítač, `Logic Level 1`, při **Slow 1,14 V 85 °C** **[M]**. Při 25 °C
a nominálu bude reálně o 20–30 % víc, ale rezervu si nech.

Řetězec musí pokrýt periodu vzorkování: 5 ns / 50 ps = **~100 článků**.

**GW1NR-9 má 2 PLL [D]** — druhá je volná.

## Constrainty (kritické)

```
zákaz promoce CH_A a CH_B na globální hodinovou síť
placement constraint na oba carry chainy, shodná relativní poloha k pinu
oba řetězce orientované stejným směrem
oba kanály ve stejném kvadrantu GCLK (LittleBee 9K má 4 kvadranty [D])
LVCMOS33, bez pull-up i pull-down, hystereze vypnutá
```

Bez zákazu promoce ti nástroj vloží do časovací cesty hodinový buffer
a zpoždění se změní **s každým buildem**.

**Kalibraci ukládej s hashem bitstreamu** — po rekompilaci může být
offset jiný a firmware to musí poznat.

## Kalibrace

**Code-density kalibrace binů** — signál nekoherentní s referencí,
statistika obsazenosti, tabulka do BSRAM.

**Musí být průběžná, ne startovací.** Zpoždění hradel se mění s teplotou
a napětím stejně jako f_MAX, tedy o 20–30 %:

| | Bin |
|---|---|
| 25 °C, nominál | 50 ps |
| 85 °C, 1,14 V | ~65 ps |

Kompenzuj podle `TMP117`. Signál pro kalibraci máš zdarma — jakékoli
běžné měření.

**Dekódování termometrického kódu s ošetřením bublin** — jednotlivé
překlopené bity uprostřed souvislé sekvence vznikají nerovnoměrným
routingem a bez ošetření dají nesmyslné hodnoty.

## Datová architektura

**Gap-free razítkování znamená gap-free zpracování, ne ukládání.**

| Vstup | Známek/s | PSRAM 8 MB (~1 M známek) |
|---|---|---|
| 1 kHz | 4 000 | souvisle |
| 100 kHz | 400 000 | souvisle |
| 1 MHz | 4 M | 0,25 s |
| 200 MHz | 400 M | nelze |

Dva režimy:

```
FREKVENCE   akumulátory v FPGA, gap-free, libovolná frekvence
            N, Σx, Σx², Σy, Σxy  → 5 čísel místo milionu známek
            → sklon, 12 číslic za sekundu

ČASOVÁNÍ    surové známky do PSRAM, do 100 kHz souvisle
            → TI, jitter, histogram, ADEV, fáze
```

Akumulátory jsou **matematicky ekvivalentní** regresi ze všech dat, ale
zahodí tvar rozdělení. Pro frekvenci to stačí, pro časoměřič ne.

**Akumulátory drž široké:** `Σx²` při milionu vzorků přeteče 32 bitů
okamžitě. Počítej se 64 bity, `Σxy` klidně s 96.

## SPI na STM32

**Jen Tang Nano na `SPI1`**, krátká trasa, žádné další zařízení.
`SPI2` do modulu je oddělená sběrnice — jinak se kapacity sčítají
a `SCK` klesne z 25 MHz na ~10.

Realistické `SCK`: **25 MHz [O]**, cíl 50. Podřízený musí synchronizovat
příchozí hodiny do vlastní domény, tedy potřebuje vnitřní takt ≥ 4×.

Vyčtení celé PSRAM: 2,7 s při 25 MHz.

**`Data_RDY` (pin 29) je klíčový.** STM32 nesmí SPI pollovat — každý burst
vstřikuje rušení do časovacích vstupů asynchronně k měření. FPGA řekne,
kdy má data, a řekne to v okně mezi hranami.

## Funkce, které stojí za implementaci

**Auto-trigger.** Prahem projeď rozsah, v každém kroku nech FPGA počítat
hrany po pevný interval. Krajní prahy s hranami dávají špičky signálu,
optimum je střed. Dostaneš měření amplitudy nezávislé na `AD8307` — dvě
hodnoty k porovnání.

**Křížová kontrola.** Mezi ~100 a 200 MHz fungují přímá cesta i ÷10
současně. Jedním testem ověříš dělicí poměr, zpoždění obou cest
i kalibraci TDC.

**Adret režim.** Fázové porovnání normálů. Dělič v FPGA propustí do TDC
každou N-tou hranu (10 MHz ÷10⁶ → 10 známek/s, 80 B/s). Rozlišení:
τ = 1 s → 2,2×10⁻¹¹, τ = 100 s → **2,2×10⁻¹³**. Adret 4110A dosahuje
10⁻⁸ až 10⁻¹² — překonáš ho už při τ = 10 s.

Dělič musí být **za** TDC vstupem, v FPGA — čítač hran běží souběžně
a číslo hrany znáš přesně.

## Firmware — pasti

**Kanál C obvodu `MCP4728` zapiš první.** Kdybys nastavil kanál A dřív,
než je `V_MID` na 0,512 V, vyjde práh −1,024 V místo nuly.

Tovární EEPROM (nuly) dá `V_MID` = 0 a `V_DAC` = 0 → práh 0 V, hystereze
maximum. To je **bezpečná strana** ✓

**Trigger level znamená na každé cestě něco jiného:**

| Cesta | Význam |
|---|---|
| 1 MΩ + lineární DC OZ | **absolutní volty na vstupu** |
| 50 Ω přes ERA (AC vázané) | relativně ke střední hodnotě |
| přes ÷10 | **bez významu — nastav na střed a nezobrazuj** |

**Kalibrační tabulka je matice**, ne jedno číslo — pro každou kombinaci
cesty, útlumu a amplitudy vlastní koeficient.

**Dispersion 40 ps** je největší systematická chyba a mění se s amplitudou.
Koriguj podle `RF_Level`.

**Po přepnutí relé počkej ≥ 50 ms.** Vazební kondenzátory 10n s 1 MΩ dají
10 ms; relé samo 3–5 ms. To je desetkrát víc než všechno ostatní.

**Reference:** drž vždy jen jednu aktivní. `REF_100MHz` a `REF_10MHz` jsou
koherentní a jejich přeslech by se neprůměroval.

**`Ref_Ctrl` má pull-down 10k** — čítač musí startovat na interním OCXO,
dokud není nahraný bitstream.

---

# 7. Rozpočet chyb

| Zdroj | Hodnota | Typ |
|---|---|---|
| Bin carry chainu 50 ps → σ = bin/√12 | 14,4 ps/kanál | náhodný |
| Dva kanály v kvadratuře | **20,4 ps** | |
| `SY100ELT23L` jitter | 5 ps/kanál **[?]** | náhodný |
| **Single-shot TI celkem** | **~22 ps** | |
| Po průměrování 100 měření | 2,2 ps | |
| `MAX9601` jitter | 0,3 ps **[D]** | zanedbatelný |
| **Dispersion `MAX9601`** | **až 40 ps** **[D]** | systematický |
| Skew mezi kanály (jedno pouzdro) | 15–70 ps **[D]** | kalibrovatelný |
| Drift toho skew | 0,12 ps/°C **[D]** | zbytkový |
| Posuv od hystereze | ±hyst/2 | kalibrovatelný |

`σ_f/f ≈ σ_t · √(12/N) / T` → při σ_t = 22 ps, N = 1000, T = 1 s:
**1,8×10⁻¹²**, tedy pod stabilitou OCXO ✓

---

# 8. Otevřené otázky

| Co | Jak zjistit | Dopad |
|---|---|---|
| **Zisk lineárního DC zesilovače** | návrh spolu s JFET sledovačem a rozsahem prahu | blokuje návrh modulu |
| Bin carry chainu **[O]** | code-density test na osazené desce | výsledné rozlišení |
| Zda carry chain na Gowinu půjde **[?]** | zkusit; fallback = Gowin IP 833 ps | rozlišení, ne HW |
| Jitter `SY100ELT23L` **[?]** | datasheet; Micrel ho často neuvádí | rozpočet TI |
| Vazba mezi sousedními piny **[O]** | generátor na jeden pin, soused jako vstup | volba pinů |
| Vstupní impedance VC pinu OCXO **[?]** | datasheet `NVG47A1282` | ladicí rozsah GPSDO |
| Pinout `MIC920` **[?]** | datasheet (alternativní SOT-23-5 konvence) | funkce tvarovače |
| Skutečné `SCK` Tang Nano **[O]** | timing analýza SPI logiky | rychlost vyčítání |

---

# 9. Layout — poznámky k oběma deskám

**Impedance 50 Ω, JLCPCB 4 vrstvy:**

| Stackup | Prepreg TOP→IN1 | W pro 50 Ω |
|---|---|---|
| `JLC04161H-7628` (standard) | 0,2104 mm | ~0,33 mm |
| `JLC04161H-3313` (impedance control) | 0,0994 mm, εr ≈ 4,05 | ~0,17 mm |

Bez objednané řízené impedance JLCPCB tloušťku vrstev **negarantuje**.

Impedanci řeš jen na trasách delších než **λ/10 ≈ 16 mm** (efektivní
frekvence hrany 350 ps je ~1 GHz, rychlost ve FR4 165 mm/ns).

- Zem v IN1 **souvislá pod celou trasou**, žádné výřezy
- Zalitá měď v TOP: odstup **≥ 3 × W**, jinak je to coplanar waveguide
- Antipad v IN1 pod SMA padem (~1,5× pad), jinak impedance klesne k 35 Ω
- Žádné přechody TOP↔BOTTOM na časovacích trasách
- `CH_A` a `CH_B` **stejně dlouhé** — 1 mm = 6,7 ps
- Zemní návraty SMA do jednoho bodu u pinu 26

---

# 10. Co je hotové

Deska FPGA: schéma zafixované, přiřazení pinů ověřené měřením v Gowin IDE,
adresní mapa uzavřená, napájení z VBUS 12–24 V přes `2× LMR33630`
(děliče přepočítány: **5,016 V** a **3,315 V** ✓).

Zvaž zvětšení `F1`/`F2` z 1 A na **2 A** kvůli náběhu OCXO — topení bere
při startu dvojnásobek ustáleného.
