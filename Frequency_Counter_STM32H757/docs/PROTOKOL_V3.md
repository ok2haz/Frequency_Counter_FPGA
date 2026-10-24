# Protokol SPI v3 – SPECIFIKACE (STM32H757 ↔ FPGA GW1NR-9), rámec 1024 B

> **Stav 2026-10-22: IMPLEMENTOVÁNO a ověřeno SIMULACÍ** – FPGA FW 0x0500 (`top_v3`, kap. 10), STM v0.35.0 (ovladač v3,
> emulátor `fpgasim v3`). ⬜ **P&R (Windows) a deska neověřeny.** Bitstream v2 (FW 0x0427) zůstává sestavitelný beze změny
> a STM s ním pracuje dál (detekce verze, kap. 6.2). Upřesnění z implementace jsou v textu označena **(FW 0x0500)**.
> **2026-10-23: FW 0x0501 + STM v0.36.0 – jednorázové děje** (kap. 10.5): `SET_CONFIG 0x09` zdroj kanálu B (šířka
> pulzu, kalibrace zpoždění A/B), RAW_CAPTURE ověřen; v STM časy hran, interval A→B, šířka pulzu. ⬜ P&R a deska.
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
| 24 | 4 | CAPS (u32): bity 0..15 = význam jako v2 tam, kde funkce trvá; **bit16 = v3 záznamy**, bit17 = 1PPS přes IDES8, bit18 = předdělička `pin_cnt`, bit19 = banky tabulky TDC, **bit20 = zdroj kanálu B (`SET_CONFIG 0x09`, FW 0x0501)**, **bit21 = hluboký RAW do PSRAM (FW 0x0505; od FW 0x0506 jen při sestavení `-DDEEP_RAW`, výchozí bitstream má 0 a T_RAW režimy 1/2 odmítá bez potvrzení v bajtu 127)** | |
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
| 50 | 1 | PPS_STATUS: bit0 PCLK běží (rPLL zamčena a uvolněna), bit1 platné, bit2 poslední hrana při zamčené PLL bez jemného času, bity 7:4 **epocha** (uvolnění PCLK mod 16; změna = jiná osa jemného času) | **(FW 0x0502)**; do 0x0501 jen bit1 |
| 51 | 1 | CAL_WAIT: bit0/bit1 = kanál A/B čeká na tabulku od STM (jako v2 abs 112 bit0/1) | |
| 52 | 2 | RX_CRC_ERR (u16, saturuje) | |
| 54 | 2 | RX_LEN_ERR (u16, saturuje) | nové |
| 56 | 4 | RX_LAST: [7:0] TYPE posledního provedeného příkazu, [31:8] dolních 24 b jeho SEQ | |
| 60 | 2 | **LUT_CNT** (u16, [13:0]) – přijatých položek tabulky od `SET_CONFIG 0x06 = 0` | jako v2 abs 112..113 |
| 62 | 2 | **LUT_SUM** (u16) – Σ `({kanál, adresa[8:0], 5'b0} ^ neg)` | **stejný vzorec jako v2** (abs 114..115) |
| 64 | 1 | PRESC A: [3:0] log2 P, bit7 = změna čeká na hranici segmentu | |
| 65 | 1 | PRESC B | |
| 66 | 1 | CFG echo: bit0 `mask_cs`, bit1 `led_hb`, bit2 reference (0 OCXO, 1 ext.), bit3..4 zdroj kanálu B (`SET_CONFIG 0x09` p1[1:0]) | bit3..4 **(FW 0x0501)** |
| 67 | 1 | rezerva | |
| 68 | 16 | 1PPS poslední: PSEQ (u32), PTS (u48, T/16384), PFLAGS (u8: bit0 platné, bit1 jemný čas), **d** (i8: jemný − hrubý [takty]), rezerva 4 | (FW 0x0500/0x0501) PTS = hrubý čas × 16384; **(FW 0x0502)** PTS z IDES8 (krok 1,25 ns = 2048 jednotek, kap. 10.6), PSEQ/PTS/hrubý čas zveřejněny najednou |
| 84 | 16 | 1PPS předchozí (totéž) | |
| 100 | 8 | hrubý čas 1PPS (u32 takty 100 MHz) poslední, předchozí – kontrola | jako v2 `pps_ts`; **(FW 0x0506) = 0** (redundantní: hrubý čas je `PTS >> 14`, když PFLAGS bit1 = 0; STM tak čte od 0x0506 i ze starších FPGA) |
| 108 | 4 | PROBE A, PROBE B (u16) – sonda ÷16 za **1 ms** (f ≈ PROBE × 16 kHz, do ~800 MHz) | **(FW 0x0503)** okno 1 ms místo segmentu (u16 by při 25 ms přetekl nad 42 MHz); čte se živě → STM bere shodu dvou rámců |
| 112 | 4 | BUB A, BUB B, OVL A, OVL B posledního segmentu | jako v2 abs 57..59 |
| 116 | 6 | jen TYPE 0xA0: druh (u8), číslo stránky (u16), počet položek (u16), rezerva | kap. 5 |
| 122 | 2 | **LUT_CNT_B** (u16, [13:0]) – přijatých položek tabulky s bitem kanálu B od `SET_CONFIG 0x06 = 0` | **(FW 0x0500)**, viz 6.3 |
| 124 | 3 | DEEP_N (u24): značek v PSRAM (hluboký RAW) | **(FW 0x0505)** |
| 127 | 1 | DEEP_ACK: bity 3:0 počet přijatých příkazů T_RAW (Grayův kód, mod 16), bit7 kanál posledního přijatého záznamu | **(FW 0x0505, audit F-0266)**; odmítnutý příkaz počet nezmění (STM tak pozná stará data) |
| 128 | 1 | DEEP_ST: bit0 PSRAM připravena (kalibrace zápisu prošla), bit1 kalibrace selhala, bit2 záznam běží, bit3 FIFO přeteklo (značky zahozeny), bit4 načítá se okno, bit5 okno načteno, **bit6 v záznamu jsou mezery** (kanál neznačkoval: hold, maska při CS), **bit7 okno s chybou čtení** (0 značek) | **(FW 0x0505)**; stav v rámci je o transakci starší (rámec se skládá po předchozí transakci) |
| 129 | 1 | bity 4:0 latence zápisu PSRAM: W0 + n bajtů (W0 = 4, n 0..31, nalezeno při startu), bit5 fáze vzorkování čtení (0 náběžná, 1 sestupná hrana `clk`) | **(FW 0x0505, audit F-0263/F-0264)** |
| 130 | 2 | DEEP_WIN (u16): poslední (načítané) okno | **(FW 0x0505)** |
| 132 | 122 | rezerva = 0 | |
| 254 | 2 | CRC16 bloku 0 (LE) | |

### 3.1b Úzká hlavička (CAPS bit22, FW 0x0507 – návrh; STM v0.40.0 ji umí)

Úspora CLS ve FPGA (`docs/audit/2026-10-24_analyza-zdroju-fpga-v3.md`). Pole, která STM nepoužívá nebo si spočítá
samo, FPGA nuluje nebo zužuje; offsety se nemění. **Bez bitu 22 platí tabulka 3.1 beze změny.**

| off | 0x0506 (tab. 3.1) | úzká hlavička (CAPS bit22) | STM (`fpga_v3_parse` + `fpga_v3_unwrap`) |
|---|---|---|---|
| 4 | SEQ u32 | **SEQ u16**, 6..7 = 0 | rozbalí na 32 b |
| 28 | TS10M u32 | 0 | nečte se (nepoužívalo se nikdy) |
| 32 | REC_HEAD u32 = **poslední** záznam | **REC_HEAD u16 = počet** zapsaných záznamů (bez odečtu), 34..35 = 0 | rozbalí; dál jako 0x0506 (počet − 1) |
| 36 | REC_FIRST u32 | **u16**, 38..39 = 0 | rozbalí |
| 42 | RING_FILL u16 | 0 | = REC_HEAD − REC_FIRST (16 b) |
| 56 | RX_LAST u32 | 0 | nečte se |
| 68 | PSEQ u32 | **u16**, 70..71 = 0 | rozbalí |
| 84 | předchozí 1PPS (16 B) | 0 | historie ve STM: předchozí zveřejněná hrana, platí jen když STM viděl pseq − 1 |
| 112 | BUB A/B, OVL A/B (počty) | 0 | příznaky 0/1 z FLAGS nejnovějšího záznamu kanálu v rámci (bit2, bit3) |

- **Rozbalení 16 → 32 b** k nejbližší hodnotě kolem předchozí (posun −32 768..+32 767). Postačí, když STM přečte rámec
  aspoň jednou za ~13 min (záznamy 80/s); kruh 512 záznamů přeteče stejně dřív.
- **Restart FPGA:** SEQ se mezi dvěma přečtenými rámci posunul mimo 0..16 384 (FPGA skládá rámec po každém segmentu
  i bez čtení, ~40/s ⇒ ~6,8 min) → horní bity 0, historie 1PPS pryč → `fpga_v3_link_take` uvidí resynchronizaci.
  Restart, po kterém SEQ náhodou padne do okna (~25 %), vypadá jako mezera (okna se přeruší, data zůstanou správná).
- **ACK:** STM dál posílá REC_ACK 32 b; FPGA bere dolních 16 b a posun počítá modulo 2¹⁶ (emulátor totéž).
- Emulátor: `fpgasim v3 on <Hz_A> [Hz_B] [fáze_B_ns] uzka` (čítače startují těsně pod 2¹⁶, přetečou do ~10 s);
  `fpga v3` vypíše druh hlavičky a počet restartů.

### 3.2 Bloky 1–3 při TYPE 0x80 – záznamy segmentů

Blok k (k = 1..3): **2 B značka** {k, SEQ[7:0]} + **8 záznamů × 31 B** + **4 B rezerva** + **2 B CRC** = 256 B.
⚠️ Návrh z 2026-10-20 uvádí „6 B rezerva + CRC“ = 258 B; správně jsou 4 B rezervy.
Záznam i v bloku m (m = 0..7) má pořadí `REC_FIRST + 8·(k−1) + m`; platné jsou jen záznamy s pořadím < `REC_FIRST + NREC`,
ostatní jsou nulové.

| off | délka | pole záznamu |
|---|---|---|
| +0 | 2 | SEG_IDX (dolních 16 b čísla segmentu) |
| +2 | 1 | CH: bit0 kanál (0 = A, 1 = B), **bity 6:4 = log2 P segmentu** (FW 0x0503; X0, N, SX počítají DĚLENÉ hrany → periody = hrany × P) |
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
| **0x0B RAW_CAPTURE** | p0 bit0 = kanál, **bity 2:1 = režim** (FW 0x0505): 0 = 512 po sobě jdoucích značek do BSRAM (čtení stránkou 3), 1 = hluboký záznam p1..p3 = N značek (u24, nejvýš 699 050) do PSRAM, 2 = načíst okno p1..p2 = w (značky w·512 .. w·512+511) z PSRAM do BSRAM → stránky 3. **Audit F-0266:** režim 0 se během hlubokého záznamu / načítání okna odmítne, režim 1 a 2 ukončí režim 0, **režim 1 při běžícím záznamu = STOP** (zbytek FIFO se dopíše); každý přijatý příkaz zvýší DEEP_ACK (bajt 127) | nové; **(FW 0x0501 ověřeno)** jen časované značky (≥ 50 ns od sebe) od žádosti, počet = souhrnná stránka položka 5, položka 2k = bity 23:0 a 2k+1 = bity 47:24 značky k |

**SET_CONFIG (p0):**

| id | význam | stav |
|---|---|---|
| 0x01 | základní okno | **v3 bez významu** – FPGA ignoruje, STM neposílá |
| 0x02 | kalibrace: p1 bit0 = 1 start sběru histogramu, p1 bity 2:1 = maska kanálů (0 = oba střídavě, §6.3), p2 = log2 počtu událostí | rozšířeno; **(FW 0x0500) p2 se ignoruje, pevně 2^20 jako v2** |
| 0x03 | volba kanálu | **v3 bez významu** (oba kanály vždy) |
| 0x04 | reference: p1 bit0 0 = OCXO, 1 = externí 10 MHz | beze změny |
| 0x05 | diagnostika: p1 bit0 `mask_cs`, bit1 `led_hb` | beze změny |
| **0x06** | **tabulka TDC: p1 = 0 začátek zápisu (nuluje `LUT_CNT`/`LUT_CNT_B`/`LUT_SUM`), p1 = 1 potvrzení = přepnutí na zapsanou banku na nejbližší hranici segmentu; p2 = maska kanálů potvrzení (bit0 A, bit1 B, 0 = oba)** | beze změny číslování; „potvrzení" = přepnutí banky; **(FW 0x0500) maska p2**: při střídavé kalibraci smí čekat oba kanály, potvrzení A nesmí aktivovat B |
| 0x07 | předdělička: p1 = kanál (0 A, 1 B), p2 = log2 P (0..4, víc = 4); FPGA přepne na hranici segmentu, 2 segmenty kanálu neplatné (FLAGS bit0; první s novým P má bit1) | **(FW 0x0503)** CAPS bit18, kap. 10.7; do 0x0502 neimplementováno |
| 0x08 | délka segmentu: p1 = L [ms] (10 nebo 25) | nové |
| **0x09** | **zdroj kanálu B**: p1 bit0 = pin A místo pinu B, bit1 = invertovat (řetěz časuje jen náběžné hrany → invertovaný = sestupné): 0 pin B, 1 pin A (kalibrace zpoždění A/B), 3 pin A invertovaný (šířka pulzu); po změně záznamy B 2 hranice segmentu neplatné (FLAGS bit0); echo bajt 66 bit3..4 | **nové (FW 0x0501)**, CAPS bit20, kap. 10.5 |

## 5. Stránky (TYPE 0xA0 v MISO)

Blok 0 = stav jako u DATA, `TYPE = 0xA0`, bajty 116..121: druh (u8), číslo stránky (u16), počet položek (u16), rezerva.
Bloky 1–3: v každém 84 položek po 3 B (u24) → **252 položek na stránku**. **(FW 0x0500)** Rozložení bloku jako u DATA:
[0] = k, [1] = SEQ[7:0] (značka bloku), od [2] 84 položek LE, 254..255 CRC. Položka g stránky p = index p·252 + g;
za koncem druhu (počet v hlavičce 119..120) jsou nuly. Souhrn (druh 0, 8 položek): 0 {TDC_STATUS B, A}, 1 maxtap A,
2 maxtap B, 3 konfigurace TDC ([4:0] log2 adres, [5] DUAL, [6] STRIDE 1, [15:7] tapy), 4 počet opakování kalibrace,
5 počet značek RAW_CAPTURE, 6 CAL_LOG2, 7 CAL_WAIT. FPGA stránku pošle v PŘÍŠTÍ transakci po žádosti (pak zase DATA).

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
- **(FW 0x0500) Překryv rámců je normální:** FPGA skládá další rámec hned po zpracování transakce, tedy PŘED ACK z té
  následující → každý záznam přijde zpravidla DVAKRÁT (`REC_FIRST` < očekávaný = duplicity přeskočit, ne chyba).
  Propustnost: 24 záznamů na rámec při 80 záznamech/s a dvojím doručení → STM musí číst **≥ 7× za s** (ovladač čte
  20× za s). Reset FPGA (nové nahrání) STM pozná podle `REC_HEAD` + 1 < očekávaný → resynchronizace
  (`fpga_v3_link_take`). Rezerva kruhu 4 záznamy: přetečení nastane při > 508 nepotvrzených (zapisovač nikdy
  nepřepíše záznam, který skladač právě kopíruje).

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
- **(FW 0x0500)** Sběr histogramu kanálu (~0,1–0,5 s) měření TOHO kanálu přeruší (řetěz krmí ring oscilátor, záznamy
  FLAGS bit0); po sběru kanál měří dál se STAROU bankou, dokud STM novou nezapíše a nepotvrdí. Při prvním sběru po
  zapnutí platná tabulka není → kanál neměří do prvního potvrzení. Potvrzení nese masku kanálu (p2). Během sběru se
  hrany signálu NEPOČÍTAJÍ (řetěz krmí ring oscilátor) → absolutní X0 dalších záznamů se proti skutečnému počtu hran
  posune; STM okna přes neplatné záznamy přeruší, na měření to vliv nemá (rozdíly X jen uvnitř souvislého úseku).
- **(FW 0x0500) `LUT_CNT_B`:** `LUT_SUM` (vzorec v2) chybu bitu kanálu u celé tabulky NEPOZNÁ – bit kanálu mění člen
  součtu o 2^14 a 512 × 2^14 ≡ 0 (mod 2^16). STM proto ověřuje i počet položek s bitem B (L-0196).

### 6.4 Rozsah
- STM volí P podle sondy (`fpga_v3_range_select`: nahoru f/P > 33 MHz, dolů f/(P/2) < 27 MHz, strop ÷16); příkaz až po
  shodě dvou rámců a až když předchozí změna platí (PRESC bit7 = 0).
- **(FW 0x0503) Pojistka je v STM, ne ve FPGA:** okno se zamítne (`FPGA_CHK_FMAX`), když sonda / P > 40 MHz – dělený
  signál pak podvzorkuje přímé i kontrolní počítání stejně (shodný, ale špatný počet). Přepnutí ve FPGA samo (FLAGS
  bit3, záznamy bit6) se neimplementovalo: kolidovalo by s příkazy STM a reakce STM (~80 ms) stačí.

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
| 4 | STM | parser v3 + **emulátor v3** + vrstva segmenty → okno, selftest – ✅ `CM7/Core/Src/fpga_v3.c` (v0.34.0 `08bd986`, nízké kmitočty v0.34.1 `0d98123`; nezapojeno), UART `fpga v3 test`, host test `tests/host/t_fpga_v3.c`, 15/15 mutací (`tests/host/mut_fpga_v3.py`); ✅ **4c** v0.35.0: detekce `VER`, čtení 1024 B, ACK, příjem (`fpga_v3_link_take`: překryv, mezera, reset), `fpga_v3_win_t` → `fpga_meas_t`, fázový záznam, `fpgasim v3`, UART `fpga v3` | build + audit ✅; host test se zpožděným ACK ✅; emulátor na desce ⬜ |
| 5 | FPGA | `seg_acc` ×2, absolutní čítače, časovač segmentů, kruh, skladač, ACK; odstranit okna/regresi/zámky/mux 128:1 – ✅ FW 0x0500 `top_v3` (kap. 10), `tb_top_v3` PASS ve 3 režimech, mutace `sim/mut_top_v3.py` | ⬜ **zkušební P&R** (Windows, `v3/build_v3.tcl`) |
| 6 | obě | stránky + `LUT_WRITE` bulk + banky – ✅ FPGA (`tdc.v` BANKS, `app_v3`), STM (`tdccal_run_v3`) | ✅ `tb_top_v3` režim 2 (výrobní tok + rekalibrace jen A); deska ⬜ |
| 7 | STM | SPI DMA regen-safe (`HAL_DMA_Init` + `__HAL_LINKDMA` + NVIC v USER CODE, buffery mimo DTCM, údržba cache), HW CRC volitelně – ✅ v0.35.0 `fpga_spi_dma.c` (DMA1 Stream0/1, NVIC 6, obsluhy a callbacky), zapíná se za běhu `spi dma on` (výchozí vypnuto) | testy D3–D8 na desce ⬜ |
| 8 | FPGA | `ides_pps` (E4) – ✅ FW 0x0502 v simulaci (kap. 10.6, CAPS bit17); `pin_cnt` + rozsah (E5) – ✅ FW 0x0503 v simulaci (kap. 10.7, CAPS bit18) | P&R, generátor |

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

## 10. Implementace (FW 0x0500, STM v0.35.0) – 2026-10-22

### 10.1 FPGA (`Frequency_Counter_FPGA_Module`)

| soubor | obsah |
|---|---|
| `src/top_v3.v` | top bitstreamu v3: TDC ×2 (`tdc_chan` BANKS = 1, jména `u_tdca/u_tdcb` jako v2 → `pins.cst` beze změny), hlídač 100 MHz, náběh, opakování kalibrace, `ref_guard`, koherence, LED – převzato z `top.v`; nové jádro a PHY v3 |
| `src/v3_core.v` | `seg_chan3` (čítač hran X 40 b, FIFO indexů pro párování se značkou, součty segmentu, kontrolní počítání), `v3_ring` (časovač segmentu, kruh 512 × 32 B v BSRAM, zápis A+B), `pps_cap3`, `cal_seq3` (kalibrace střídavě), `raw_cap3` |
| `src/app_v3.v` | 10 MHz: příkazy (kontrola délky, CRC bloků), ACK/REC_ACK/přetečení, skladač rámce (hlavička, záznamy z kruhu, stránky), LUT_WRITE dávkou |
| `src/spi_slave_phy_v3.v` | PHY 1024 B (krok 3) |
| `src/tdc.v` | parametr `BANKS` (v2: 0 = beze změny, regrese v2 29/29 PASS); `TSPIPE` (2026-10-23, v3 = 1: značka přes registr navíc, `ts_valid` T+10 – rozložení, `docs/audit/2026-10-23_rozlozeni-fpga.md`) |
| `src/timing_v3.sdc` | z `timing.sdc` bez výjimek oken/regrese |
| `v3/Counter_FPGA_v3.gprj`, `v3/build_v3.tcl` | projekt Gowin v3 (výstupy `v3/impl/`, v2 se nepřepíše) |

- **Hodinové domény:** jádro (TDC, součty, zápis do kruhu) 100 MHz; aplikace a skladač 10 MHz jako v2; kruh v BSRAM se
  dvěma hodinami; počet zapsaných dvojic do 10 MHz přepínacím bitem (záznamy po 12,5 ms).
- **Časování:** každá cesta jádra ≤ ~2 úrovně LUT (rozdíly 40 b ve dvou stupních po 20 b, součty s registrovaným přenosem,
  slovo záznamu ve dvou stupních) → v SDC žádná multicycle výjimka jádra.
- **Hranice segmentu:** ~22 taktů (220 ns) se značky nesčítají (zapisovač čte součty) → ztráta ≤ ~3 značek kanálu z
  segmentu; čítač hran X běží dál, takže X0/T0 i dvoubodový kmitočet jsou přesné. 🔴 **Oprava 2026-10-23 (L-0198):**
  `v3_ring` shodí `freeze` v témže taktu, kdy dá `clear` – značka právě v taktu `clear` se dřív přičetla se starým X0/T0
  do vynulovaných součtů nového segmentu (N ji nepočítal: SX = N(N−1)/2 + N_předchozí − 1; kmitočet nezkreslený, člen leží
  na přímce). Teď se zahodí jako značky během `freeze` (`take … & ~clear`).
- **Párování indexu a značky:** index spuštěné hrany se zachytí 3 takty po `trig_ack` a čeká ve FIFO (2) na `ts_valid`
  (9 taktů; s `TSPIPE` 10, FIFO snese do 12). Ověřeno režimem 0 s kanálem B 17,3 MHz (ve FIFO dvě značky) a mutací
  „FIFO hloubky 1“; konstantní posun
  indexu (např. zachycení o takt dřív) je ekvivalentní – ve všech rozdílech (SX, dvoubodově, LSQ, fáze) se vyruší.
  ⚠️ v2 (`win_recip`/`regr_acc`) podle ROZBORU KÓDU (ne testem) při spouštění hrana za hranou (> ~11 MHz) páruje značku
  s indexem následující spuštěné hrany (`sg_x` se přepíše dřív, než přijde `ts_valid`) – při stálém kroku spouštění
  posun konstantní a sklon regrese správně; ve v3 FIFO.
- **Zdroje:** odhad bez P&R (Gowin jen Windows) – BSRAM kruh 8 + PHY 2 + TDC 4 (tabulka 2 banky, histogram) + RAW 2
  ≈ 16 / 26; CLS by mělo klesnout proti 79 % v2 (odpadá mux 128:1, zámky, okna, regrese) – rozhodne `build_v3.tcl`.

### 10.2 Simulace (Icarus 13, `sim/tb_top_v3.sv`, `sim/run.ps1` testy `topv3`, `topv3ovf`, `topv3cal`, `topv3pw`)

Celý `top_v3` se skutečným modelem TDC (268 tapů), segment 250 µs (SIM_TOP), STM = BFM po SPI 1024 B / 25 MHz:

| režim | ověřuje | výsledek |
|---|---|---|
| 0 (B 16,100161 MHz; do 2026-10-23 17,3 MHz) | CRC 4 bloků, hlavička, značky bloků, REC_FIRST/NREC, překryv (duplicity), záznamy: SEG_IDX/CH, N ≈ f·L, SX = N(N−1)/2 (každá hrana časovaná), sklon ST/SX = perioda, dvoubodově ze sousedních segmentů, **T0 = skutečný čas hrany X0**, 1PPS, RX_LEN_ERR, RX_CRC_ERR, SET_CONFIG 0x05/0x08, stránky histogramu = `hist` v BSRAM, maskování při CS | PASS (T0 proti hraně max 181 ps A, 77 ps B); **FW 0x0501:** RAW_CAPTURE kanálu A 512 značek, 511/511 po sobě, max \|rozdíl − k·P\| 40 ps |
| 1 | přetečení kruhu (16 záznamů): FLAGS bit1, skok REC_FIRST, zhasnutí po ACK, platné záznamy po obnově | PASS |
| 2 | výrobní kalibrace obou kanálů (stránky, výpočet, LUT_WRITE 3 rámce, LUT_CNT/LUT_SUM/LUT_CNT_B, potvrzení s maskou, banka 1), rekalibrace jen A (B měří, A po sběru měří se starou bankou, nová tabulka v bance 0) | PASS |
| 3 (A 5 MHz, pulz 37,3 ns; FW 0x0501) | `SET_CONFIG 0x09`: B = pin A invertovaný → T0_B − T0_A (mod P) = šířka pulzu; B = pin A → zpoždění A/B; návrat na pin B (B zase platný); echo bajt 66 bit3..4; po přepnutí 2 segmenty B neplatné | PASS (šířka 12 dvojic max \|chyba\| 22 ps, zpoždění 13 dvojic max 22 ps) |

Kanál B v režimu 0 běží na 17,3 MHz (perioda 5,78 taktu): časuje se každá hrana a ve FIFO indexů bývají dvě značky.
Od 2026-10-23 **16,100161 MHz** (perioda 6,21 taktu): (a) na segment není celý počet period (16,1 MHz × 250 µs = 4025),
fáze značek vůči hranici segmentu se posouvá ~0,25 taktu za segment a test projde i značku v taktu `clear` (počítadlo
pokrytí, chk > 0; s celým počtem period se hranice zkoušela stále ve stejné fázi a chyba L-0198 prošla); (b) rozestup hran
6/7 taktů: při `ts_valid` T+10 (TSPIPE) jsou dvě rozpracované značky jen u rozestupu 6, takže FIFO hloubky 1 se rozejde –
při 17,3 MHz (rozestup 5/6) by posun byl u všech hran stejný a mutace „FIFO hloubky 1“ ekvivalentní (L-0199).
Mez sklonu ST/SX 2 × 100 ps / rozpětí (dřív 2 × 60, L-0198).
Mutace `sim/mut_top_v3.py` (17, paralelně; FW 0x0501 + zdroj B ignorován, RAW s prohozenými polovinami; 2026-10-23 rozložení + značka v taktu clear se přičte, `ts_valid` o takt dřív): FIFO hloubky 1 (konstantní posun indexu je ekvivalentní mutant – vyruší se
ve všech rozdílech), rozdíl bez výpůjčky, slovo záznamu, freeze, kontrolní
počítání, slot kanálu, ACK, CRC/délka, stránka, přetečení, potvrzení bez masky, zápis do aktivní banky.

### 10.3 STM (v0.35.0)

- `fpga_freq.c`: detekce verze (kap. 6.2) – v2 beze změny, v3: `poll_v3` (ACK, `fpga_v3_link_take`, okna
  `fpga_v3_grp_feed`, okno vybraného kanálu → `fpga_meas_t`, druhý kanál = sekundární slot, těžiště prvního segmentu →
  `rg_xm/rg_ym[0]` pro fázový záznam), příkazy (`v3_cfg`), stránky (`v3_page`), kalibrace (`tdccal_run_v3`, ~100 ms místo
  ~0,6 s), CAL report ze souhrnné stránky; kanál a hradlo volí STM lokálně; ztráta signálu = žádné okno 2,5 s + hradlo.
- **Spojité čtení:** ve v3 FPGA nic nepřepisuje (kruh 6,4 s), STM dostane každý segment a okna skládá bez mrtvé doby
  (konec okna = začátek dalšího, dvoubodově teleskopuje). Poll vydá jedno okno na volání z fronty 8 oken; SPI transakce
  nejdřív po 40 ms (v0.35.1, 25/s ≥ 7/s z kap. 6.1). Model smyčky FpgaTasku (`t_fpga_v3.c`): hradlo 50 ms → 1198/1198
  oken za 60 s souvisle, 22,7 transakce/s (bez meze 90,9/s se stejným výsledkem).
- `fpgasim v3 on [Hz_A] [Hz_B]`: emulátor v3 se zpožděným ACK jako RTL, 1PPS z emulátoru v2 (smyčka GPSDO).
- `fpga_spi_dma.c` (krok 7): `spi dma on|off`, výchozí vypnuto.
- UART: `fpga v3` (stav příjmu), `fpga v3 test` (selftest), `spi dma`.

### 10.4 Na desce (pořadí)

1. Windows: `gw_sh .../v3/build_v3.tcl` → kontrola rezervy časování (nejistota 0,9 ns), `sim/check_tdc_placement.py`, CLS.
2. Flash STM v0.35.0 s **bitstreamem v2** → `fpga v3` musí ukázat `protokol: v2`, měření beze změny (regrese ovladače).
3. Flash bitstreamu v3 + power-cyklus → `fpga v3`: v3, záznamy rostou, duplicit ≈ záznamů, mezer 0; `status` kmitočet,
   ADEV (fázový záznam); kalibrace TDC po startu (`tdc`, CAL_WAIT → tabulky); `spi 25`; `spi dma on` (testy D3–D8).
4. Bez FPGA: `fpgasim v3 on 10000000 3300000` → okna, statistika, GPSDO.
5. Jednorázové děje (FW 0x0501, kap. 10.5): generátor pulzů na A → `fpga pw cal` (zpoždění B − A ~ jednotky ps až stovky
   ps, σ ~ 40 ps), `fpga pw on` → šířka proti generátoru; A a B ze dvou výstupů se známým zpožděním → `fpga ab on`;
   `fpga raw a` (10 MHz: rozdíly = 100 ns, σ ~ 40 ps); `fpga ev on` s 1PPS GPS → čas hran proti 1PPS.
   Bez FPGA: `fpgasim v3 on 7 7 1234000` → `fpga ab on`, `fpga ab 10` (1,234 ms).

### 10.5 Jednorázové děje (FW 0x0501, STM v0.36.0) – 2026-10-23

Ze záznamů segmentů (kap. 3.2) bez dalších dat z FPGA: **časové značky hran**, **interval A→B jednorázově**, **šířka pulzu**;
delší sled hran **RAW_CAPTURE**. ⬜ Ověřeno simulací (FPGA) a host testem (STM), ne na desce.

**Co záznam nese (meze):**

- Přesný čas má **první časovaná hrana segmentu** (T0); při N = 2 i druhá (T0 + ST, index X0 + SX); při N ≥ 3 jen souhrn
  (počet, střední perioda ST/SX). Přesně tedy **1–2 hrany na segment (25 ms)**, další hrany segmentu jen v souhrnu.
- Časují se jen hrany **≥ 5 taktů (50 ns)** od předchozí časované; čítač hran X počítá všechny.
- Delší sled: **RAW_CAPTURE** = 512 po sobě jdoucích časovaných značek jednoho kanálu (≥ 50 ns od sebe) od žádosti;
  pod ~100 Hz se za 5 s nezaplní (STM vrátí, co je).
- A, B i 1PPS jsou na **jedné ose FPGA** → T0 se rozbaluje **společně** pro oba kanály (rozdíl proti poslední značce
  libovolného kanálu se znaménkem, 48 b, a počet přetečení 2^48 = 171,8 s z uplynulého času STM; X po kanálech).
  1PPS: PTS (FW 0x0500) = hrubý čas → čas hrany proti 1PPS s rozlišením **10 ns**.
- **Maskování při CS** (výchozí zapnuto, `fpga diag mask`): hrana během CS (~330 µs na transakci, ≤ 25 transakcí/s ≈ 0,8 %
  času) se nečasuje, záznam má FLAGS bit3. Pro jednorázové děje buď `fpga diag mask off` (riziko rušení značek při CS),
  nebo počítat se ztrátou (párování takový záznam zahodí, `fpga ev` ho označí `[CS]`).

**Párování start/stop (`fpga_v3_ab_feed`, `fpga_v3_pr_rec`):** start = první přesně známá hrana A po předchozím stopu,
stop = první hrana B po startu (čas > start). FPGA posílá segment jako A, pak B; události A čekají na záznam B téhož
segmentu a předají se spolu (seřazené v čase). Start se zahodí (`zahozenych startu`), když: (a) do 10 s nepřišel stop;
(b) souhrn B (N ≥ 3) začal před startem a trvá přes něj – první hrana B po startu je neznámá; (c) záznam je neplatný
(FLAGS bit0) nebo se značkami při CS (bit3) – hrana mohla chybět; (d) mezera v příjmu. Po přepnutí zdroje B se páruje
až za záznamem B s držením (starší záznamy v kruhu jsou z původního zdroje).

**Šířka pulzu (`SET_CONFIG 0x09`, CAPS bit20):** B = pin A invertovaný (p1 = 3) → kanál A časuje náběžné, kanál B
sestupné hrany téhož pinu (řetěz TDC časuje jen náběžné) → interval A→B = šířka úrovně H. Kalibrace p1 = 1 (B = pin A):
zpoždění B − A týchž hran (cesty k řetězům, mux) se odečte. Po změně zdroje jsou záznamy B **2 hranice segmentu
neplatné** (rozpracovaný segment měří jiný signál). ⚠️ Rozdíl zpoždění náběžné a sestupné hrany v cestě B (XOR v LUT)
kalibrace nezachytí – ⬜ změřit na desce generátorem se známou šířkou.

**UART (STM v0.36.0):**

| příkaz | co dělá |
|---|---|
| `fpga ev on\|off\|<n>` | kruh 256 událostí (zapnutí = vynulování); `<n>` = posledních n: kanál, hrana / souhrn (N, perioda), SEG_IDX, čas na ose FPGA [s, 1 ps], čas od 1PPS #PSEQ |
| `fpga ab on\|off\|<n>` | interval A→B: statistika (počet, střední, σ, min, max), zahozené starty, posledních n (≤ 64) |
| `fpga pw cal` / `on` / `off` / `<n>` | `0x09 = 1` kalibrace zpoždění B − A, `0x09 = 3` šířka pulzu (interval − zpoždění), `0x09 = 0` zpět na pin B |
| `fpga raw a\|b [dump]` | RAW_CAPTURE: 512 značek, rozdíly (střední, σ, min, max), kmitočet z rozpětí; `dump` = všechny značky (blokuje UartTask ≤ 5 s + 5 stránek) |
| `fpgasim v3 on <Hz_A> [Hz_B] [faze_B_ns]` | emulátor: B = A + fáze (bez `Hz_B` = kmitočet A); zdroj B (`0x09`) a RAW se neemulují |

**Ověření:**

- FPGA `tb_top_v3` režim 3 (`sim/run.ps1 topv3pw`, A 5 MHz, pulz 37,3 ns): šířka 12 dvojic max |chyba| 22 ps, zpoždění
  A/B 13 dvojic max 22 ps, echo bajt 66 bit3..4, návrat na pin B; režim 0: RAW_CAPTURE 511/511 po sobě, max |rozdíl − k·P|
  40 ps. Mutace 15/15 (nové: zdroj B ignorován, RAW s prohozenými polovinami).
- STM host `t_fpga_v3.c` (emulátor se zpožděným ACK): 7 Hz B = A + 1,234 ms (280 intervalů, σ 43 ps), 1 kHz souhrny
  B = A + 123,456 µs (796, σ 43 ps), B = A (zpoždění 396 × 1,6 ps; interval se souhrnem B přes start zahozen), 2 Hz
  s 200 s bez signálu (T0 přeteče, čas navazuje < 1 ns), 7 Hz B = A + 140 ms (B před dalším A v témže segmentu); kroky se
  známými časy (řazení, stop jen po startu, souhrn B přes start, CS, přepnutí zdroje B). Mutace 24/24. Selftest krok 11.
- ⬜ P&R (mux zdroje B před řetězem `u_tdcb`, `sig_eff` false_path beze změny), deska.

### 10.6 Jemný čas 1PPS přes IDES8 (FW 0x0502, STM v0.37.0) – 2026-10-23

`src/v3_core.v` `pps_ides3`: rPLL 100 → 400 MHz, CLKDIV /4 (PCLK), IDES8 na pinu 1PPS → 8 vzorků za takt, krok 1,25 ns
(σ kvantizace 0,36 ns proti 2,9 ns hrubého čítače). Čas `{pcnt, poloha, 11'b0}` + `t_rel` (tick při uvolnění CLKDIV)
= osa `tick_hi` až na pevný posun, který se mění jen při novém zamčení PLL (**epocha** v `PPS_STATUS` 7:4). `pps_cap3`
zveřejní PSEQ, PTS, hrubý čas a `d` najednou (jemný čas přijde ~4 takty po hrubém, čeká se nejvýš 31 taktů).
STM: `fpga_v3_pps_fine()` (jemný jen s CAPS bit17 + PCLK běží + PFLAGS bit1), `fpga_pps_t.frac/fine/epoch`, GPSDO
počítá interval se zlomkem a při změně osy (epocha, jemný ↔ hrubý) začne znovu; `fpga v3` vypíše stav 1PPS.
⚠️ Pro interval 1PPS ↔ kanály A/B (kap. 10.5) zůstává pevný posun osy (latence + fáze PLL) – kalibrovat na desce.
Rozmístění, ověření a body pro desku: `docs/audit/2026-10-23_funkce-fpga.md` (pořadí bitů IDES8 je PŘEDPOKLAD modelu).

### 10.7 Předdělička vstupů a automatický rozsah (FW 0x0503, STM v0.38.0) – 2026-10-23

`src/v3_core.v` `pin_cnt3` ×2: 4b čítač taktovaný **přímo pinem** (jediný stupeň na plném kmitočtu = c[0]), do řetězu TDC
pin (P = 1) nebo c[p−1]; sonda c[3] (÷16) se v doméně 100 MHz počítá vždy, okno 1 ms → PROBE A/B. `top_v3`: log2 P z
`SET_CONFIG 0x07` platí od hranice segmentu, 2 segmenty kanálu neplatné; zdroj B z pinu A (`0x09`) bere pin A přímo.
Záznam: log2 P v CH 6:4, FLAGS bit1 v prvním segmentu s novým P. Hlavička: PRESC A/B (bit7 čeká), PROBE A/B.
STM: okna v periodách (hrany × P, 2pt i LSQ, těžiště), přerušení okna i statistiky při změně P, mez přímého měření na
dělený kmitočet, pojistka rozsahu ze sondy (kap. 6.4), události BURST v periodách pinu; emulátor s předděličkou;
`fpga presc a|b auto|0..4`, `fpga v3` vypíše P a sondu. ⚠️ Strop vstupu určí buffer LVCMOS33 (Gowin 150/300 MHz) – ověřit
generátorem; 400 MHz nejspíš jen diferenciálně nebo děličem na desce. Rozmístění a ověření: `docs/audit/2026-10-23_funkce-fpga.md`.

### 10.8 Součty segmentů v DSP (FW 0x0504) – 2026-10-23

`seg_chan3` parametr `SUM_DSP` (top_v3 = 1): SX (48 b) a ST (56 b) jedním stupněm s atributem `syn_dspstyle = "dsp"` →
ALU54D v řádku DSP R19 místo řetězů ALU v logice; přičítání ve V+2, hodnoty i takt dokončení shodné s dvoustupňovými
součty (`SUM_DSP = 0`, simulace `-DSIM_SUM_FAB`). Rozhraní beze změny. ⬜ P&R: převzetí atributu, úspora CLS/FF.
`timing_v3.sdc` na registry součtů neodkazuje: v ALU54D by nemusely existovat a odkaz na neexistující registr je v Gowin
chyba TA2003 (audit F-0271).

### 10.9 Hluboký RAW záznam do PSRAM (FW 0x0505, STM v0.39.0) – 2026-10-23

`v3_core.v` `psram_hb3`: řadič vestavěné PSRAM GW1NR-9 (HyperBus x8, čip 0 = 4 MB) bez PLL – DDR takt = `clk` 100 MHz,
CK 50 MHz z FF na sestupné hraně (hrana uprostřed bajtu), ~100 MB/s v datech, dávky 128 B (CS# < 4 µs). Čtení se zarovná
podle RWDS (nezávisí na latenci; bajty až od RWDS = 0, protože při CA drží PSRAM RWDS jako příznak latence – audit
F-0262), latenci zápisu (4..35 bajtů) a fázi vzorkování čtení řadič při startu **najde sám** (zápis vzoru / čtení).
`raw_cap3` režim 1: FIFO 512 značek (BSRAM) → dávky do PSRAM (~80 MB/s ≈ 13 Mznaček/s plynule, jinak DEEP_ST bit3);
režim 2: okno 512 značek z PSRAM do BSRAM → stránky druhu 3. STM: `fpga_freq_v3_raw_deep` (start, čekání, okna, statistika
přes okna), `fpga raw a|b deep <N> [dump]`, `fpga v3` vypíše PSRAM. 🔴 Jména vývodů (`O_psram_*`, `IO_psram_*`), časování
a chování čipu jsou PŘEDPOKLAD (model `hb_psram_model` podle obecné specifikace HyperBus) – ověřit P&R a deskou.
**Audit 2026-10-23** (`docs/audit/2026-10-23_v3-funkce-audit.md`, STM v0.39.1): STM posílá T_RAW s potvrzením (bajt 127;
odmítnuto = `-5`), běžící záznam nejdřív zastaví, po timeoutu zastaví a přečte zapsané, chybu okna (DEEP_ST bit7) a mezery
(bit6) hlásí. Okno skončí i při chybě čtení (dřív se zaseklo).
