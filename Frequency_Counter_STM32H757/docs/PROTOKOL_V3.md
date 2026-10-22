# Protokol SPI v3 – SPECIFIKACE (STM32H757 ↔ FPGA GW1NR-9), rámec 1024 B

> **Stav: SPECIFIKACE k implementaci, 2026-10-22. Nic z toho zatím není v kódu ani v bitstreamu.**
> Dnes běží **v2** (rámec 128 B, FW 0x0427 / STM v0.33.x) – popis bajt po bajtu v `HW_REFERENCE.md`
> oddíl „FW 0x0425 … 0x0427". Tento dokument je **závazný pro implementaci v3 na obou stranách**;
> při rozporu s návrhem platí tento dokument.
>
> **Zdroje:** návrh `CLI_other_instance_exchange/E0_FW0425/2026-10-20_v3_navrh_a_analyzy.md` (architektura,
> segmenty, rozpočet zdrojů, kritická analýza) a rozhodnutí uživatele 2026-10-22:
> (1) **číslování příkazů z FW 0x0427 zůstává** (`TYPE 0x0A LUT_WRITE`, `SET_CONFIG 0x06` = tabulka TDC, `0xA0` = CAL),
> (2) SPI se připravuje na **25 MHz**, (3) rychlost pinů a DMA na STM se řeší **regen-safe v kódu**, ne v `.ioc`.
> **Nahrazuje** starší návrh `../FPGA_PROTOCOL_V3_NAVRH.md` (2026-08-30: krátký rámec STAV + dlouhá transakce BULK) –
> ten zůstává jen jako historie.

---

## 1. Co se proti v2 mění

| | v2 (dnes, FW 0x0427) | v3 |
|---|---|---|
| délka transakce | 128 B | **1024 B = 4 bloky × 256 B** |
| CRC | CRC-16/CCITT-FALSE přes 0..125, LE na 126..127 | **totéž po blocích**: přes bajty 0..253 bloku, LE na 254..255 |
| `VER` (bajt 1) | 0x02 | **0x03** |
| data měření | jedno okno (hradlo), regrese 2 segmentů | **proud záznamů spojitých segmentů** (L = 25 ms, oba kanály vždy) |
| okno, hradlo, volba kanálu | `SET_CONFIG 0x01`, `0x03` | **končí** (okna skládá STM ze segmentů) |
| čtení histogramu TDC | 1 adresa na rámec (`0xA0`, ~512 rámců) | **stránky**: 252 položek na rámec |
| zápis tabulky TDC | `0x0A`, 1 položka na rámec | **`0x0A`, až 252 položek na rámec**, stejné kódování položky, banky A/B |
| hlídač ztráty signálu | FPGA, 2,5 s | **STM** (3× očekávaná perioda, min. 2,5 s) |
| SCK | 12,5 MHz (ověřeno na desce) | **cíl 25 MHz**, FPGA musí zvládnout 30 MHz (SDC), strop 40 MHz |

## 2. Fyzická vrstva

- 4 vodiče (SCK, MOSI, MISO, CS) beze změny, mode 0, MSB first, 8 bitů, CS ruční GPIO PB12 (idle HIGH).
- **SCK: cíl 25 MHz** (kernel SPI2 200 MHz / 8). FPGA: `create_clock spi_sck` ≥ 30 MHz s rezervou, STA ověřit
  pro výstupní zpoždění MISO proti setupu STM a délce propoje J3. STM: piny SCK/MOSI/MISO na rychlost ≥ MEDIUM,
  nastavit **regen-safe ve `fpga_freq_init`** (CubeMX má `GPIO_SPEED_FREQ_LOW`, `spi.c:94,101`).
- Jedna transakce = přesně 1024 B mezi CS↓ a CS↑. **FPGA kratší i delší transakci zahodí** (žádný příkaz se neprovede),
  počítá ji v `RX_LEN_ERR`.
- **CRC bloků MISO počítá PHY za letu** z vysílaných bajtů a vkládá ho na 254/255 (`src/spi_slave_phy_v3.v`); obsah TX RAM
  na těchto pozicích se nevysílá. Skladač rámce CRC neřeší. RX: CRC každého bloku za letu (`rx_blk_ok[3:0]`), celý rámec
  do RX RAM (2 × 1024 B střídavě).
- **Časování:** CS↓ → první SCK ≥ 1 µs, poslední SCK → CS↑ ≥ 1 µs, **mezera mezi transakcemi ≥ 150 µs**
  (FPGA skládá další rámec; když nestihne, pošle předchozí obsah s nezměněným `SEQ` = „nic nového“).
- Doba přenosu: 1024 B × 8 / 25 MHz = **328 µs** (12,5 MHz: 655 µs).

## 3. Rámec MISO (FPGA → STM)

### 3.1 Blok 0 – hlavička a stav (bajty 0..255)

| off | délka | pole | pozn. |
|---|---|---|---|
| 0 | 1 | MAGIC = 0xA5 | |
| 1 | 1 | VER = 0x03 | |
| 2 | 1 | TYPE: **0x80 DATA** (bloky 1–3 = záznamy), **0xA0 STRÁNKA** (bloky 1–3 = stránka, §5) | 0xA0 = číslo CAL reportu z v2 |
| 3 | 1 | FLAGS: bit0 = nové záznamy, bit1 = přetečení kruhu (od posledního ACK), bit2 = kalibrace běží, bit3 = rychlá pojistka rozsahu (§6.4), bit4 = RX chyba v poslední transakci, bit5 = poslední příkaz proveden | |
| 4 | 4 | SEQ (u32) – roste o 1 s každým nově složeným rámcem | |
| 8 | 2 | FRAME_LEN = 1024 | |
| 10 | 1 | NBLK = 4 | |
| 11 | 1 | rezerva = 0 | |
| 12 | 4 | BUILD_TIME (u32, unix s) | jako v2 abs 12 |
| 16 | 4 | BUILD_GIT (u32, [27:0] hash, [31] neuložené změny) | jako v2 abs 16 |
| 20 | 2 | FW_VERSION (u16) | |
| 22 | 1 | CLK_MHZ (100) | jako v2 abs 99 |
| 23 | 1 | SEG_LEN_MS (25 nebo 10) | |
| 24 | 4 | CAPS (u32): bity 0..15 = význam jako v2 tam, kde funkce trvá; **bit16 = v3 záznamy**, bit17 = 1PPS přes IDES8, bit18 = předdělička `pin_cnt`, bit19 = banky tabulky TDC | |
| 28 | 4 | TS10M (u32) – čas rámce v taktech 10 MHz | |
| 32 | 4 | **REC_HEAD** (u32) – pořadí nejnovějšího uzavřeného záznamu v kruhu | |
| 36 | 4 | **REC_FIRST** (u32) – pořadí prvního záznamu v blocích 1–3 | |
| 40 | 1 | NREC – počet záznamů v blocích 1–3 (0..24) | |
| 41 | 1 | rezerva | |
| 42 | 2 | RING_FILL – nepotvrzených záznamů v kruhu | |
| 44 | 1 | CLK_STATUS (jako v2 abs 64) | |
| 45 | 1 | CLK_LOSS (jako v2 abs 65) | |
| 46 | 1 | COH_ST (jako v2 abs 10) | |
| 47 | 1 | COH_CNT (jako v2 abs 11) | |
| 48 | 1 | TDC_STATUS A: bit0 kalibrace platná, bit1 selhala, bit2 sběr histogramu běží, bit3 řetěz krátký, bit4 aktivní banka | |
| 49 | 1 | TDC_STATUS B (totéž) | |
| 50 | 1 | PPS_STATUS: bit0 LOCK rPLL, bit1 platné, bit2 nesoulad s hrubým časem | |
| 51 | 1 | CAL_WAIT: bit0/bit1 = kanál A/B čeká na tabulku od STM (jako v2 abs 112 bit0/1) | |
| 52 | 2 | RX_CRC_ERR (u16, saturuje) | |
| 54 | 2 | RX_LEN_ERR (u16, saturuje) | nové |
| 56 | 4 | RX_LAST: [7:0] TYPE posledního provedeného příkazu, [31:8] dolních 24 b jeho SEQ | |
| 60 | 2 | **LUT_CNT** (u16, [13:0]) – přijatých položek tabulky od `SET_CONFIG 0x06 = 0` | jako v2 abs 112..113 |
| 62 | 2 | **LUT_SUM** (u16) – Σ `({kanál, adresa[8:0], 5'b0} ^ neg)` | **stejný vzorec jako v2** (abs 114..115) |
| 64 | 1 | PRESC A: [3:0] log2 P, bit7 = změna čeká na hranici segmentu | |
| 65 | 1 | PRESC B | |
| 66 | 1 | CFG echo: bit0 `mask_cs`, bit1 `led_hb`, bit2 reference (0 OCXO, 1 ext.) | |
| 67 | 1 | rezerva | |
| 68 | 16 | 1PPS poslední: PSEQ (u32), PTS (u48, T/16384), PFLAGS (u8), rezerva 3 | |
| 84 | 16 | 1PPS předchozí (totéž) | |
| 100 | 8 | hrubý čas 1PPS (u32 takty 100 MHz) poslední, předchozí – kontrola | jako v2 `pps_ts` |
| 108 | 4 | PROBE A, PROBE B (u16) – sonda ÷16 za poslední segment | |
| 112 | 4 | BUB A, BUB B, OVL A, OVL B posledního segmentu | jako v2 abs 57..59 |
| 116 | 138 | rezerva = 0 | |
| 254 | 2 | CRC16 bloku 0 (LE) | |

### 3.2 Bloky 1–3 při TYPE 0x80 – záznamy segmentů

Blok k (k = 1..3): **2 B značka** {k, SEQ[7:0]} + **8 záznamů × 31 B** + **4 B rezerva** + **2 B CRC** = 256 B.
⚠️ Návrh z 2026-10-20 uvádí „6 B rezerva + CRC“ = 258 B; správně jsou 4 B rezervy.
Záznam i v bloku m (m = 0..7) má pořadí `REC_FIRST + 8·(k−1) + m`; platné jsou jen záznamy s pořadím < `REC_FIRST + NREC`,
ostatní jsou nulové.

| off | délka | pole záznamu |
|---|---|---|
| +0 | 2 | SEG_IDX (dolních 16 b čísla segmentu) |
| +2 | 1 | CH (0 = A, 1 = B) |
| +3 | 1 | FLAGS: bit0 neplatný (kalibrace / hold), bit1 změna P v tomto segmentu, bit2 bublina, bit3 značky při CS, bit4 kontrolní počítání ≠ 0, bit5 saturace N, bit6 rychlá pojistka rozsahu |
| +4 | 3 | N (u24) – počet časovaných hran |
| +7 | 5 | X0 (u40) – absolutní index první hrany (× P) |
| +12 | 6 | T0 (u48) – absolutní značka první hrany [T/16384] |
| +18 | 6 | SX (u48) = Σ(x − x0) |
| +24 | 7 | ST (u56) = Σ(t − t0) [T/16384] |

- Každý segment dá **dva záznamy po sobě (A, pak B)**, i když kanál nemá signál (N = 0) – souvislost času.
- Kruh v BSRAM: 512 záznamů = 256 segmentů = **6,4 s** při L = 25 ms.
- Tok: 80 záznamů/s; 24 na rámec → STM čte **≥ 4× za s** (při 25 MHz bez potíží i 10× za s).

## 4. Rámec MOSI (STM → FPGA)

Hlavička MOSI = v2: 0 MAGIC, 1 VER = 0x03, 2 TYPE, 3 rezerva, 4..7 SEQ (STM), 8..9 FRAME_LEN = 1024, 10 NBLK = 4,
11 rezerva, **12 p0, 13 p1, 14 p2**, … ; CRC po blocích jako MISO. **FPGA ověří vždy blok 0; bloky 1–3 jen u příkazu,
který je používá** (`LUT_WRITE`). Vadný použitý blok → celý příkaz se zahodí (FLAGS bit4, `RX_CRC_ERR`).

| TYPE | význam | stav proti 0x0427 |
|---|---|---|
| **0x06 ACK** | p: **REC_ACK** (u32, bajty 12..15) = pořadí posledního souvisle převzatého záznamu + 1 | rozšířeno |
| **0x01 SET_CONFIG** | p0 = config_id, p1, p2 = hodnoty (tabulka níže) | beze změny |
| **0x0A LUT_WRITE** | bloky 1–3: v každém [0] = počet položek (0..84), od [1] položky po 3 B **kódované jako v 0x0427**: `p0` adresa[7:0], `p1` {kanál, adresa[8], neg[13:8]}, `p2` neg[7:0]; FPGA zapisuje do **neaktivní banky** a každou položku přičte do `LUT_CNT`/`LUT_SUM` | rozšířeno (víc položek) |
| **0xA0 STRÁNKA** | žádost o stránku: p0 = druh (0 souhrn kalibrace, 1 histogram A, 2 histogram B, 3 surové značky), p1..p2 = číslo stránky (u16) | rozšířeno (z CAL reportu) |
| 0x08 START / 0x09 STOP | souvislé měření zap/vyp | beze změny |
| **0x0B RAW_CAPTURE** | p0 = kanál; FPGA uloží 512 po sobě jdoucích značek do BSRAM (čtení stránkou 3) | nové |

**SET_CONFIG (p0):**

| id | význam | stav |
|---|---|---|
| 0x01 | základní okno | **v3 bez významu** – FPGA ignoruje, STM neposílá |
| 0x02 | kalibrace: p1 bit0 = 1 start sběru histogramu, p1 bity 2:1 = maska kanálů (0 = oba střídavě, §6.3), p2 = log2 počtu událostí (výchozí 15) | rozšířeno |
| 0x03 | volba kanálu | **v3 bez významu** (oba kanály vždy) |
| 0x04 | reference: p1 bit0 0 = OCXO, 1 = externí 10 MHz | beze změny |
| 0x05 | diagnostika: p1 bit0 `mask_cs`, bit1 `led_hb` | beze změny |
| **0x06** | **tabulka TDC: p1 = 0 začátek zápisu (nuluje `LUT_CNT`/`LUT_SUM`), p1 = 1 potvrzení = přepnutí na zapsanou banku na nejbližší hranici segmentu** | beze změny číslování; „potvrzení" = přepnutí banky |
| 0x07 | předdělička: p1 = kanál (0 A, 1 B), p2 = log2 P (0..4); FPGA přepne na hranici segmentu | nové |
| 0x08 | délka segmentu: p1 = L [ms] (10 nebo 25) | nové |

## 5. Stránky (TYPE 0xA0 v MISO)

Blok 0 = stav jako u DATA, `TYPE = 0xA0`, bajty 116..121: druh (u8), číslo stránky (u16), počet položek (u16), rezerva.
Bloky 1–3: v každém 84 položek po 3 B (u24) → **252 položek na stránku**.

| druh | obsah | počet stránek |
|---|---|---|
| 0 | souhrn kalibrace (stav, počty událostí obou kanálů) | 1 |
| 1 / 2 | histogram kanálu A / B (adresa 0..511) | 3 |
| 3 | surové značky RAW_CAPTURE (u48 → 2 položky na značku) | 5 |

Bulk se prokládá s daty: nejvýš 1 stránka na 2 datové rámce (kruh 6,4 s se mezitím neplní nad míru).

## 6. Chování

### 6.1 Potvrzování a ztráty
- STM po každém rámci DATA pošle v další transakci `ACK` s `REC_ACK` = první pořadí, které **ještě nemá souvisle převzaté**.
- FPGA posílá vždy od nejstaršího nepotvrzeného (`REC_FIRST` = `REC_ACK`), dokud kruh nepřeteče.
- **Vadný blok 1–3** (CRC): STM jeho záznamy nepřevezme a `REC_ACK` neposune za ně → FPGA je pošle znovu. Vadný blok 0 = celý rámec zahodit.
- Přetečení kruhu → FLAGS bit1 a skok `REC_FIRST` za `REC_ACK` → STM přeruší souvislý úsek (statistika, fáze).

### 6.2 Přechod v2 → v3 (STM umí obojí)
- STM po startu čte **1024 B**. `VER = 0x02` a CRC 0..125 v pořádku → v2 (pracuje se 128 B); `VER = 0x03` a CRC bloku 0 → v3.
- ✅ **Ověřeno simulací 2026-10-22** (`sim/tb_phy_v2_long.sv`, SCK 25 MHz): PHY v2 při transakci 1024 B vrací na MISO svůj
  128B rámec osmkrát dokola (index TX RAM je 7bitový) – prvních 128 B je platný rámec v2; přijme jen bajty 0..127 (čítač
  hran saturuje na 1024), takže rámec v3 kvůli CRC odmítne a nic neprovede; následná transakce 128 B funguje beze změny.
  Negativní kontrola (bez saturace) test shodí. Detekce podle `VER` v prvních 128 B je tedy bezpečná.
- Flashovat obě strany spolu; STM v3 musí běžet i s FPGA v2 (zpětná kompatibilita), FPGA v3 bez STM v3 neměří.

### 6.3 Kalibrace
- Sběr histogramu kanálů **střídavě** (jeden kanál měří vždy); kalibrovaný kanál má záznamy s FLAGS bit0.
- STM přečte histogram (stránky 1/2), spočte tabulku stejným vzorcem jako dnes (`fpga_tdccal_lut`), pošle
  `SET_CONFIG 0x06 = 0`, `LUT_WRITE` (~3 rámce na kanál), ověří `LUT_CNT`/`LUT_SUM`, pošle `SET_CONFIG 0x06 = 1`;
  FPGA přepne banku na hranici segmentu (`TDC_STATUS` bit4). **Bez přerušení měření.**

### 6.4 Rozsah
- STM volí P podle sondy (`fpga_range_select`); FPGA navíc samo přepne na ÷16, když sonda ukáže f/P > 40 MHz
  ve 2 segmentech po sobě (FLAGS bit3, záznamy bit6) – ochrana do reakce STM.

## 7. Co STM přebírá od FPGA

| funkce | v2 | v3 |
|---|---|---|
| kmitočet okna | dvoubodově + regrese ze 2 segmentů | nejmenší čtverce přes těžiště segmentů (10 na 0,25 s) |
| ztráta signálu | FPGA 2,5 s | STM: mezera bez hrany > 3 očekávané periody, nejvýš 2,5 s (`max_gap_ms`) → rozpracované okno se zahodí |
| nízký kmitočet (perioda > L) | FPGA | STM: prázdné segmenty (N = 0) okno prodlouží, okno končí **první hranou po K segmentech** (reciproční čítač); LSQ jen při ≥ 2 segmentech s hranou |
| hradlo | FPGA | skupina segmentů v STM |
| volba kanálu | FPGA (`0x03`) | STM (oba kanály jsou vždy v záznamech) |

## 8. Implementace – co je potřeba a kde

| # | strana | úkol | ověření |
|---|---|---|---|
| 1 | obě | **tato specifikace** + `HW_REFERENCE.md` odkaz, STATUS | revize uživatelem |
| 2 | STM | piny SPI2 na rychlost ≥ MEDIUM regen-safe ve `fpga_freq_init`; zkouška **25 MHz s dnešním rámcem 128 B** (`spi 25`, ≥ 1 h, `CRC err`, díry SEQ) | deska, osciloskop na J3 |
| 3 | FPGA | PHY 1024 B: 10bitová adresa, TX 2 × 1024 B, RX 2 × 1024 B, CRC za letu po blocích, `rx_len_ok` – ✅ `src/spi_slave_phy_v3.v` (nezapojeno), `tb_phy_v3` PASS při 5/25/30 MHz, 6/6 mutací zachyceno (`sim/mut_phy_v3.py`); ⬜ syntéza + STA | při zapojení přepsat `txequiv`/`link` na v3 |
| 4 | STM | parser v3 + **emulátor v3** + vrstva segmenty → okno, selftest – ✅ `CM7/Core/Src/fpga_v3.c` (v0.34.0 `08bd986`, nízké kmitočty v0.34.1 `0d98123`; nezapojeno), UART `fpga v3 test`, host test `tests/host/t_fpga_v3.c`, 15/15 mutací (`tests/host/mut_fpga_v3.py`); ⬜ **4c** zapojení do `fpga_freq` (detekce `VER`, čtení 1024 B, ACK, `fpga_v3_win_t` → `fpga_meas_t`, `fpgasim` v3, fázový záznam) | build + audit ✅ (0 varování, 96/0/2); emulátor na desce bez FPGA ⬜ (po 4c) |
| 5 | FPGA | `seg_acc` ×2, absolutní čítače, časovač segmentů, kruh, skladač, ACK; odstranit okna/regresi/zámky/mux 128:1 | testbenche + **zkušební P&R** (Windows) |
| 6 | obě | stránky + `LUT_WRITE` bulk + banky | `tb_stmcal` v3, STM selftest |
| 7 | STM | SPI DMA regen-safe (`HAL_DMA_Init` + `__HAL_LINKDMA` + NVIC v USER CODE, buffery mimo DTCM, údržba cache), HW CRC volitelně | `fpga_spi_dma_stats`, testy D1–D8 |
| 8 | FPGA | volitelně `ides_pps` (E4), `pin_cnt` + rozsah (E5) | P&R, generátor |

⚠️ Pravidla, která platí i pro v3: změna rámce = obě strany + `STATUS.md`; emulátor skládá rámec tak, jak ho skládá RTL
(L-0099); každý nový kanál STM → FPGA má počitadlo a kontrolní součet vrácené v rámci (L-0188).

## 9. Zkouška SCK 25 MHz s dnešním rámcem 128 B (krok 2 z kap. 8)

Předpoklad: FPGA FW 0x0427 (přestavěný z commitnutých zdrojů, #328) + **STM ≥ v0.33.2** (piny SCK/MOSI na MEDIUM;
v0.34.x mění jen nezapojený modul `fpga_v3`, cesta v2 je stejná).
`spi <MHz>` se nepersistuje – po resetu platí zase 12,5 MHz.

1. Flash obou stran, **power-cyklus**, pak `spi` → musí vypsat `SPI2: piny SCK MEDIUM, MOSI MEDIUM` a `SCK 12500000 Hz`.
2. **Kontrola 30 min při 12,5 MHz:** zapsat výchozí hodnoty z `status` – `CRC err`, `SEQ FPGA: diry`, `FPGA SAMOKONTROLA`
   (zamítnutá okna po rozběhu), `FPGA DIAG` (OVL, bubliny), `REGR` (podíl zamítnutí); u vlastní reference ADEV (0,25 s).
3. `spi 25` → `SCK 25000000 Hz`, **60 min** bez dalších zásahů, pak tytéž hodnoty.
4. **Osciloskop** (sonda na konci u FPGA, pin 55 SCK, pin 57 MISO; zem co nejkratší): náběh a překmit SCK, zvonění,
   poloha změny MISO proti náběžné hraně SCK na straně STM (rezerva pro setup).
5. **Kritéria přijetí:** přírůstek `CRC err` = 0, díry SEQ = 0, žádný nárůst zamítnutých oken, OVL ani podílu zamítnuté
   regrese proti kroku 2, ADEV vlastní reference beze změny v rámci rozptylu (rušení SPI do časovacích vstupů, O4).
6. Při selhání: nejdřív signál (osciloskop), pak sériový odpor 22–33 Ω u zdroje SCK/MOSI (HW); rychlost pinů **zvyšovat
   až jako poslední** (strmější hrany = víc rušení). Výsledek zapsat do `MERENI_A_KALIBRACE.md` a `STATUS.md` #330.
