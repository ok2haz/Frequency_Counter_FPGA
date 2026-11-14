# v3 — návrh (spojité segmenty 25 ms, automatický rozsah 0,1 Hz – ~400 MHz, rámec 1024 B) a analýzy — 2026-10-20

Fáze: **návrh**. Nahrazuje `2026-10-20_v3_idealni-stav.md` tam, kde se liší: okna + dva segmenty → **spojité segmenty**,
1PPS → **IDES8 + rPLL**, **žádné nastavování kmitočtu**. Předchází E0 (`2026-10-20_E0_implementace_zadani_CLI.md`).
Štítky: **[kód] [HW] [P&R] [D]** datasheet · **[ÚVAHA] [HYP] [?]** neověřeno.

---

## 1. Požadavky (uživatel 2026-10-20)

1. Vstup CH_A a CH_B: **cokoli z 0,1 Hz až maximum**, uvažovat 400 MHz, aspoň 200 MHz.
2. **Nikde se nenastavuje**, jaký kmitočet se měří → rozsah volí přístroj sám.
3. Spojité segmenty 25 ms.
4. Maskování značek při SPI zapnuté, s možností vypnout.
5. Co největší přesnost a rozlišení; omezení vypsat.

---

## 2. Architektura FPGA v3

```
pin ─┬─────────────────────────────┐
     └─ pin_cnt (÷1…÷16, takt pinem) ┴─► tdc_chan (N1 oprava) ─► ts, kód, bublina ─► seg_acc ─► zápis záznamu ─► kruh BRAM ─┐
                └─ sonda ÷16 ───────────────────────────────────────────────────────────────────────► (do záznamu)        │
absolutní čítač period (40 b, × P) ─► x_abs ──────────────────────────────────────────────────────────────────────────────┤
časovač segmentů (2 500 000 taktů 100 MHz, společný A/B) ─► seg_tick                                                         ├─► skladač rámce 1024 B ─► PHY
ides_pps (rPLL 400 MHz + CLKDIV + IDES8 na pinu 33) ─► záznam 1PPS ───────────────────────────────────────────────────────┤
hlídače: hodiny 640 ± 4, coh_mon, ref_guard ─► stav ───────────────────────────────────────────────────────────────────────┘
kalibrace: kruhový oscilátor → histogram (BRAM) → STM počítá tabulku → LUT_WRITE do neaktivní banky → LUT_SWAP
```

| blok | soubor (návrh) | poznámka |
|---|---|---|
| čítač taktovaný pinem + sonda | `v3_navrh/pin_cnt.v` | P = 1, 2, 4, 8, 16; sonda ÷16 počítaná vždy |
| spojitý akumulátor segmentu | `v3_navrh/seg_acc.v` | n, x0, t0, Σ(x−x0), Σ(t−t0), bubliny, CS; mezera 16 taktů po hranici |
| 1PPS 1,25 ns | `v3_navrh/ides_pps.v` | výstup v jednotkách T/16384 (1,25 ns = 2048) |
| automatický rozsah (STM) | `v3_navrh/fpga_range.c/.h` | selftest prošel (gcc) |
| odstraněno proti 0x0425 | — | `win_recip` (okna, hradlo, `snap`, `r_*`), `regr_acc` (dělička i segmenty), `gate_div`, 64b `timestamp`, `*_lat`, `h_*`, multiplexer 128:1, výpočet tabulky ve FPGA, `d_*`, ladicí RX čítače, `pps_cap` (zůstane jen jako kontrola, volitelně) |

### 2.1 Automatický rozsah (bez nastavování)

- STM každý segment spočte odhad f: sonda (f = 16·probe/L, rozlišení 640 Hz) nebo vlastní měření (pod ~1 MHz).
- `fpga_range_select`: nejmenší P (mocnina 2) s f/P ≤ 33 MHz; dolů až při f/(P/2) < 27 MHz (hystereze).
  200 MHz → P = 8, 400 MHz → P = 16 (25 MHz do řetězu).
- Změna P: `SET_CONFIG 0x06`, FPGA přepne v hold na hranici segmentu, segment označí `PRESC_CHANGE`.
  STM přeruší fázový úsek (pevné zpoždění čítače se mění). Pro statistiku jeden zahozený segment = 25 ms.
- Pod ~40 Hz jsou segmenty z většiny prázdné nebo s jednou značkou → STM pracuje s jednotlivými značkami.
  Dolní mez dává jen trpělivost STM (48b značky rozbalené přes souvislé záznamy).

### 2.2 Proč spojité segmenty

| vlastnost | okna + 2 segmenty (dnes) | spojité segmenty 25 ms |
|---|---|---|
| využité značky | 25 % (preset 0), 64 % (preset 1) | ~100 % |
| koeficient šumu regrese (bílý PM) | 5,33 / 3,85 | ~3,47 (plná LSQ přes 10 těžišť / 0,25 s) |
| fázové body na 0,25 s | 2 | 10 |
| dvoubodový odhad | uzavírací hrany | první značky segmentů (sdílené → teleskopuje) |
| A–B fáze / interval | ne | souběžné segmenty |
| FPGA logika | okna, hradlo, dělička | jen akumulace (jednodušší) |
| SPI | klidová zóna nutná | maskování CS (zóna zbytečná) |

---

## 3. Rámec SPI v3 (1024 B, 4 bloky × 256 B, CRC-16/CCITT-FALSE na blok v bajtech 254–255)

### 3.1 Blok 0 — hlavička, stav, 1PPS

| off | délka | pole |
|---|---|---|
| 0 | 1 | MAGIC 0xA5 |
| 1 | 1 | VER 0x03 |
| 2 | 1 | TYPE: 0x80 DATA, 0xA1 BULK |
| 3 | 1 | FLAGS: bit0 data, bit1 přetečení kruhu, bit2 CAL_BUSY, bit3 HOLD, bit4 bulk v blocích 1–3 |
| 4 | 4 | SEQ u32 |
| 8 | 2 | FRAME_LEN = 1024 |
| 10 | 1 | NBLK = 4 |
| 11 | 1 | rez. |
| 12 | 4 | BUILD_TIME |
| 16 | 4 | BUILD_GIT |
| 20 | 2 | FW_VERSION |
| 22 | 1 | CLK_MHZ |
| 23 | 1 | SEG_LEN_MS (25) |
| 24 | 4 | CAPS u32 |
| 28 | 4 | TS10M u32 |
| 32 | 4 | SEG_HEAD u32 — index nejnovějšího uzavřeného segmentu |
| 36 | 4 | SEG_FIRST u32 — index prvního záznamu v tomto rámci |
| 40 | 1 | NREC — počet záznamů v blocích 1–3 (0–24) |
| 41 | 1 | rez. |
| 42 | 2 | RING_FILL — záznamů čekajících v kruhu |
| 44 | 1 | CLK_STATUS |
| 45 | 1 | CLK_LOSS |
| 46 | 1 | COH_ST |
| 47 | 1 | COH_CNT |
| 48 | 1 | TDC_STATUS A |
| 49 | 1 | TDC_STATUS B |
| 50 | 1 | PPS_STATUS: bit0 LOCK rPLL, bit1 platné, bit2 nesoulad s hrubým časem |
| 51 | 1 | CAL_STATE |
| 52 | 2 | RX_CRC_ERR |
| 54 | 2 | RX_ACK (SEQ posledního provedeného příkazu) |
| 56 | 2 | LUT_ID A |
| 58 | 2 | LUT_ID B |
| 60 | 1 | PRESC A (log2 P; bit7 změna čeká) |
| 61 | 1 | PRESC B |
| 62 | 1 | CFG echo: bit0 mask_cs, bit1 led_hb |
| 63 | 1 | rez. |
| 64 | 16 | 1PPS poslední: PSEQ u32, PTS u48 (T/16384, osa PCLK), PFLAGS u8, rez. 5 |
| 80 | 16 | 1PPS předchozí |
| 96 | 8 | hrubý čas 1PPS (kontrola) ×2, u32 |
| 104 | 4 | PROBE A posledního segmentu |
| 108 | 4 | PROBE B |
| 112 | 4 | BUB A, BUB B, OVL A, OVL B posledního segmentu |
| 116 | 138 | rezerva |
| 254 | 2 | CRC |

### 3.2 Bloky 1–3 — proud záznamů segmentů

Každý blok: značka (číslo bloku, SEQ[7:0]) 2 B + **8 záznamů × 31 B** + 6 B rezerva + CRC → 24 záznamů na rámec.

| off | délka | pole záznamu |
|---|---|---|
| +0 | 2 | SEG_IDX (dolních 16 b) |
| +2 | 1 | CH (0 A, 1 B) |
| +3 | 1 | FLAGS: bit0 neplatný (hold), bit1 změna P, bit2 bublina, bit3 CS, bit4 ccd ≠ 0, bit5 saturace n |
| +4 | 3 | N u24 |
| +7 | 5 | X0 u40 (absolutní index hrany, × P) |
| +12 | 6 | T0 u48 (absolutní značka) |
| +18 | 6 | SX u48 = Σ(x − x0) |
| +24 | 7 | ST u56 = Σ(t − t0) |

**Tok:**
- 80 záznamů/s (2 kanály × 40 segmentů); při 4 čteních/s 20 na čtení (max 24).
- **Kruh v BRAM: 16 kB = 8 × BSRAM v režimu 4096 × 4 b** (šířkově dělené, 8 bloků dá 32b slovo → žádný výstupní multiplexer),
  záznam zarovnaný na 32 B → **512 záznamů = 6,4 s při L = 25 ms / 2,6 s při L = 10 ms**. BSRAM v3 celkem ~15 z 26.
- STM potvrzuje `ACK` posledním převzatým SEG_IDX. FPGA posílá od nejstaršího nepotvrzeného.
- Přetečení kruhu → FLAGS bit1 a mezera v SEG_IDX → STM přeruší souvislost.

### 3.3 MOSI (STM → FPGA), 1024 B

| TYPE | význam |
|---|---|
| 0x06 ACK | potvrzení SEQ + SEG_IDX |
| 0x01 SET_CONFIG | 0x04 reference, 0x05 diag (mask_cs, led_hb), **0x06 PRESC A/B (jen z automatiky STM)**, 0x07 délka segmentu |
| 0x02 CAL_START | sběr histogramu (maska kanálů, log2 počtu) |
| 0x03 PAGE_REQ | stránka bulk (histogram, tabulka, surové značky) |
| 0x04 LUT_WRITE | zápis neaktivní banky (data v blocích 1–3, 378 položek) |
| 0x05 LUT_SWAP | přepnutí banky na hranici segmentu |
| 0x07 RAW_CAPTURE | 512 po sobě jdoucích značek do BRAM |

Bulk: rámec TYPE 0xA1 nese stránku místo záznamů. Kruh 0,8 s → bulk střídat s daty (nejvýš 1 bulk na 2 datové rámce).

---

## 4. Zpracování v STM

| krok | výpočet |
|---|---|
| rozbalení | SEG_IDX 16 → 32 b, T0 48 → 64 b, X0 40 → 64 b (souvislé záznamy) |
| těžiště | t_c = T0 + ST/N [T/16384], x_c = X0 + SX/N [hrany]; N = 1: samotná značka; N = 0: prázdný |
| kmitočet za 0,25 s (displej) | nejmenší čtverce přes 10 těžišť skupiny (váha N) → f; kompatibilní vstup statistiky: (cykly, hradlo) mezi prvními těžišti skupin → teleskopuje |
| dvoubodový | první značky (T0, X0) segmentů — kontrola |
| fáze, ADEV/MDEV/TDEV | z těžišť (`fpga_phrec` rozšířit na segmenty) |
| interval / fáze A–B | těžiště souběžných segmentů; P = 1 nutné pro jednoznačnost |
| 1PPS → GPSDO | e = ΔPTS − n·1,6384·10¹² [T/16384] → ps; σ_x za běhu (`gpsdo_pm_ns`) |
| rozsah | `fpga_range_select` z PROBE / f |
| kalibrace | histogram → tabulka (stejný vzorec jako FPGA, součet více sběrů) → LUT_WRITE / SWAP |
| zatížení CM7 | ~4 × 1024 B/s blokujícího SPI ≈ 2,6 ms/s (0,26 %); zpracování 80 záznamů/s ≈ 0,1 ms/s; s DMA < 0,1 % |

---

## 5. Rozpočet zdrojů (odhad ±30 %, rozhodne P&R)

| | 0x0424 [P&R] | v3 [ÚVAHA] |
|---|---|---|
| FF | 4 336 | ~3 200 |
| logika (LUT+ALU) | 4 703 | ~4 100 |
| CLS | 3 639 (85 %) | ~2 950 (~68 %) |
| BSRAM | 5 | ~15 (TX 2 KB, RX, **kruh 16 kB = 8 bloků**, 2 histogramy, 2 tabulky po 2 bankách, surové značky) |
| DSP | 4 | 4–8 (ST do ALU54D, je-li třeba) |
| PLL | 0 | 1 |
| PRIMARY | 6/8 | 7/8 (PCLK z IDES; hodiny `pin_cnt` NESMÍ jít na globální síť [?]) |
| LW | 8/8 | ~5/8 |

---

## 6. Analýza optimality

| rozhodnutí | proč je optimální | alternativy a proč ne |
|---|---|---|
| akumulace ve FPGA, výpočet v STM | přenést lze jen součty (značky 120 MB/s ≫ SPI 1,5 MB/s); dělení a proložení v `double`/int64 je přesné a pružné | dělení ve FPGA: zaokrouhlení, ~500 FF, časování |
| segmenty 25 ms | 10 těžišť / 0,25 s ≈ plná LSQ (3,47 proti optimu 3,46); Σ(t−t0) ~1e16 < 2⁵⁴; 80 záznamů/s se vejde do 4 čtení/s | 10 ms: 2,5× víc přenosu, zisk ~0; 50+ ms: méně fázových bodů |
| počítání každé periody pinem | N přesně, značka beze změny, strop jen elektrický | IDES na vstupu: PLL, víc logiky, nic navíc |
| P s f/P ≤ 33 MHz | TDC značkuje 10–20 Mzn/s, pod přímým stropem 40 MHz | P = 16 vždy: pod 30 MHz zbytečně méně značek (regrese až 4× horší) |
| IDES8 pro 1PPS | 0,37 ns ≪ ~1,5 ns NEO-7M; řada CFU zůstane volná | plný TDC: +650 FF/+760 logiky, zisk 0,04 ns neviditelný |
| maskování CS | rušení SPI se nedostane do součtů; cena ~0,2 % značek | klidová zóna: se spojitými segmenty nejde |
| tabulka v STM | součet více sběrů (INL 4,9 → 2,4 ps), teplotní tabulky, přepočet dat | ve FPGA: ~200 FF + ~200 logiky, jedna kalibrace |
| hlídač koherence | jediný levný způsob, jak poznat pulz navíc/chybějící (validace: 100 %; fázový skok spolehlivě až od 10 ns) | jen 640 ± 4: nepozná jednotlivý pulz |

---

## 7. Kritická analýza

| riziko / slabé místo | dopad | opatření |
|---|---|---|
| **400 MHz přes LVCMOS33 nejspíš nepůjde** (Gowin Fmax 150/300 MHz, údaj pro výstup) | strop 200–300 MHz | T8: LVDS25 vstup (napětí 0–2,15 V!) nebo dělička na vstupní desce; ověřit STA + generátorem |
| promoce hodin `pin_cnt` na globální síť | PRIMARY 9/8 → nesestaví se | omezení v cst [?]; zrušit odvozené hodiny `u_app/n1054_3` |
| IDES8/HCLK na IOB23A neověřené | E4 padá | zkušební P&R (F5); záloha IDDR (1,44 ns) nebo IDES4 |
| fáze PCLK po novém zamčení PLL | skok 1PPS o k × 2,5 ns | CLKDIV reset synchronně; GPSDO zahodí kotvu při LOCK = 0 |
| kruh 6,4 s (L = 25 ms) / 2,6 s (10 ms) | výpadek čtení delší → mezera | ACK-protokol, příznak přetečení; STM přeruší úsek |
| velká změna STM (segmenty místo oken) | riziko chyb SW | vrstva „skupina segmentů → okno“, emulátor v3, selftesty, přechodné porovnání |
| O4 (~105 ps) zatím nevysvětleno | parametry níže platí s podmínkou | E0 diagnostika rozhodne |
| teze o ADEV z fáze (bílý PM) | zisk na dlouhých τ může být menší | E1 offline ověření |
| odhady zdrojů ±30 % | těsný rozpočet | P&R po každé etapě |
| `seg_acc`: mezera 16 taktů na hranici | ztráta ~3 značek z ~500 000 | zanedbatelné; první značka po mezeře = x0/t0 |
| časování 100 MHz ve fabricu GW1N (vedení 2–3 ns/skok) | nové zapisovače mohou být kritické | registrované cesty; skutečná rezerva ≥ 0,64 ns |

---

## 8. Závěrečná analýza

- **Hotové (E0):** dvě skutečné chyby RTL opravené (ověřeno modely), diagnostika O4, hlídač koherence, adaptivní mez
  regrese, odmítání oken nad přímým stropem, GPSDO se změřeným šumem 1PPS a historií v ps, fázový záznam (selftest OK).
  Čeká na simulaci, P&R a desku (CLI).
- **v3 je technicky konzistentní:** spojité segmenty zjednoduší FPGA a přidají přesnost i funkce. Automatický rozsah splní
  „nic nenastavovat“ do 200 MHz s velkou pravděpodobností, do 400 MHz jen s úpravou vstupu (HW). IDES8 dá 1PPS na úroveň
  pod šumem GPS. Rozpočet ~68 % CLS dává rezervu i pro časování.
- **Největší neznámá je O4.** Dokud se nevysvětlí ~105 ps šumu značky, jsou cílové parametry podmíněné; E0 proto jde první.
- **Kritické předpoklady k ověření:** IDES8/HCLK (F5), strop pinu (T8), promoce hodin, teze ADEV z fáze (E1).

---

## 9. Zamýšlené dosažené parametry

| parametr | dnes 0x0424 | po E0 | cíl v3 | podmínka / omezení |
|---|---|---|---|---|
| rozsah kmitočtu | 33 MHz (nad tím chyba bez příznaku), dole ~0,4 Hz (displej) | 40 MHz, nad tím odmítnuto | **0,1 Hz – 200 MHz** jistě [ÚVAHA], **400 MHz** s HW úpravou | T8; hlídač ztráty signálu podle očekávané periody |
| nastavování | hradlo, kanál | totéž | **žádné** (automatický rozsah) | 1 zahozený segment při změně P |
| kvantizace značky | 27–31 ps [HW] | totéž | totéž | popcount dekodér ~15 ps (mimo plán) |
| šum značky | ~105 ps [HW] | měřeno, rušení SPI maskováno | podlaha ~36 ps | podle O4 |
| rozlišení okna 0,25 s, dvoubodově | 5,9·10⁻¹⁰ | totéž | 2,0·10⁻¹⁰ | při podlaze 36 ps |
| regrese 0,25 s (bílý šum, 10 MHz) | teorie 1·10⁻¹², změřeno 2,9·10⁻¹¹ | totéž, bez chyb 17–25 MHz | teorie 3·10⁻¹³ | korelovaný šum rozhodne |
| ADEV z fáze | — | E1: ~6·10⁻¹² @ 1 s, ~6·10⁻¹⁴ @ 100 s | totéž s 10 body / 0,25 s | předpoklad bílého PM těžišť |
| 1PPS σ | 2,9 ns | 2,9 ns (σ_x se měří) | **0,37 ns** | IDES8 ověřit; GPS ~1,5 ns |
| detekce vadného pulzu hodin | ne | ano, 102,4 µs | ano | chybějící/navíc pulz a skok ≥ 10 ns; menší skoky a jitter ne |
| absolutní nejistota | reference ~1·10⁻¹¹ (LOCK) | totéž | totéž | čítač ≪ reference |
| zatížení CM7 | 0,22 % | 0,22 % | ~0,3 % (DMA < 0,1 %) | |
| CLS | 85 % | ~86 % (E0 +~120 FF) | ~68 % | P&R |

---

## 10. Etapy po E0

| etapa | obsah | závisí na |
|---|---|---|
| E1 | STM: fázový záznam (hotový modul), `FPGA_STATS_USE_CC` po offline ověření | E0 na desce, F2, F3 |
| E2 | v3 jádro: `seg_acc`, absolutní čítače, časovač segmentů, kruh, skladač 1024 B, CRC bloků, ACK; odstranit okna, děličku, latche; STM parser v3 + emulátor + vrstva skupin | E0 |
| E3 | kalibrace v STM: histogram → tabulka → LUT_WRITE/SWAP | E2 |
| E4 | `ides_pps` + rPLL, GPSDO v ps | F5 |
| E5 | `pin_cnt` + automatický rozsah, STA domény pinu, generátor do stropu | E2, T8 pro 400 MHz |
| E6 | volitelné: ALU54D, Z10 (F1), IODELAY dither, sestupná hrana (šířka impulzu), události | data |

---

## 11. Kritická analýza 2 a optimalizace v3 (doplněno 2026-10-20)

### 11.1 Co v návrhu chybělo — doplněno

| # | problém | řešení v3 |
|---|---|---|
| V1 | náhlá změna signálu (např. z 10 na 300 MHz) při P = 1: do přepnutí STM (25 ms segment + ~250 ms čtení) řetěz vidí víc hran → nesmyslné značky | **rychlá pojistka ve FPGA**: když sonda ukáže f/P > 40 MHz ve 2 segmentech, FPGA samo přepne na ÷16 a segmenty označí; STM pak zvolí optimum. Pravidlo pro STM: segment s f_sonda/P > 40 MHz je vždy neplatný |
| V2 | **úzké impulzy** (vysoká úroveň < ~10,5 ns, např. impulzní signál na nízkém kmitočtu): v řetězu obě hrany → bubliny, chyby počítání | automatika rozsahu použije **P = 2 i na nízkém kmitočtu**, když bubliny/`ccd` ukazují problém: první KO překlopí každou náběžnou hranu, do řetězu jde střída 50 %; N zůstává přesné |
| V3 | kalibrace (histogram) přeruší měření OBOU kanálů | kalibrovat A a B **střídavě** (jeden kanál měří vždy); sdílený kruhový oscilátor to dovolí |
| V4 | teplotní rekalibrace přerušuje měření | STM drží tabulky pro více teplot a **interpoluje**; nová tabulka jde do neaktivní banky a přepne se na hranici segmentu → **bez přerušení**; sběr histogramu jen při výrazné změně |
| V5 | hlídač ztráty signálu FPGA (2,5 s) by pod 0,4 Hz hlásil ztrátu | v3 hlídač v STM: ztráta = žádná hrana po dobu 3× očekávané periody (podle sondy / posledního měření), min. 2,5 s |
| V6 | 400 MHz | LVCMOS33 pravděpodobně jen do 200–300 MHz; 400 MHz = diferenciální vstup nebo dělička na vstupní desce (TODO T8) |
| V7 | při P > 1 je interval/fáze A–B nejednoznačná modulo P period | STM pro interval A–B vynutí P = 1 (do ~33 MHz), jinak interval nehlásí |

### 11.2 Parametry v kostce (cíl v3, podmínky viz kap. 9)

| | hodnota |
|---|---|
| rozsah | 0,1 Hz – 200 MHz bez nastavování; 400 MHz s úpravou vstupu; přímý režim jen pro impulzy ≥ 10,5 ns (jinak P ≥ 2) |
| kvantizace značky | 27–31 ps |
| šum značky | dnes 105 ps → podlaha ~36 ps (podle O4) |
| kmitočet 0,25 s dvoubodově | 2·10⁻¹⁰ (při 36 ps) |
| regrese 0,25 s (10 MHz, teorie) | ~3·10⁻¹³; skutečnost určí korelovaný šum (dnes 2,9·10⁻¹¹) |
| ADEV z fáze | ~6·10⁻¹² @ 1 s, ~6·10⁻¹⁴ @ 100 s (předpoklad bílého PM) |
| 1PPS | 0,37 ns (dnes 2,9 ns), omezení GPS ~1,5 ns |
| nejistota | dána referencí (~1·10⁻¹¹ zamčeno) |
| FPGA | ~68 % CLS (dnes 85 %) |
| CM7 | ~0,3 % (s DMA < 0,1 %) |

---

## 12. Délka segmentu a další optimalizace (doplněno 2026-10-20)

### 12.1 Délka segmentu L — pravidlo volby (žádné nastavování uživatelem)

Ztráta přesnosti proti proložení všech značek: σ/σ_opt = 1/√(1 − 1/K²), K = τ/L (počet segmentů v měřeném intervalu).

| obnova displeje / τ | L = 25 ms | L = 10 ms |
|---|---|---|
| 50 ms | K = 2 → 15 % | K = 5 → 2 % |
| 100 ms | 3 % | 0,5 % |
| ≥ 250 ms | ≤ 0,5 % | ≤ 0,1 % |

Pravidlo: **obnova ≥ 250 ms → L = 25 ms; obnova 50–100 ms → L = 10 ms** (STM odvodí z nastavení obnovy, ne z kmitočtu).
Σ(t − t0) se při 25 ms a 20 Mzn/s vejde do 54 b (ALU54D), při 10 ms s rezervou. Druhé momenty (Σx², Σxt), které by
odstranily závislost na L úplně, zamítnuty: Σxt ~71 b, zisk proti L = 10 ms ~2 %.

### 12.2 Další optimalizace

| oblast | optimalizace | přínos | stav |
|---|---|---|---|
| STM | **HW jednotka CRC STM32H7** pro rámec 1024 B (CCITT-FALSE je podporovaný polynom) | 0 % CPU na CRC | doporučeno pro v3 (vyžaduje CubeMX) |
| STM | **SPI přes DMA** + notifikace úlohy (buffery mimo DTCM, údržba cache) | zátěž CM7 z ~0,3 % na ~0,05 %, odstraní blokující čekání | doporučeno pro v3 |
| STM | CRC přes půlbajtovou tabulku (fallback bez HW CRC) | 1,6× (PC), odhad 2–3× (CM7) | **hotovo v E0** |
| STM | kruh fázového záznamu `float` místo `double` | 16 → 8 kB RAM | **hotovo v E0** (testy prošly) |
| FPGA | sdílené `tr1/tr2` (tick − 1, − 2) pro oba kanály | −68 FF | **hotovo v E0 jako volba** `define SHARE_TR` (pro případ, že E0 přeleze bránu CLS) |
| FPGA | kruh šířkově dělený (4096 × 4 b ×8) | žádný výstupní mux pro 16 kB | v3 |
| FPGA | Z10 prořezání nepoužitých tapů | ~190 FF + ~125 LUT na kanál | čeká na `tdc hist` |
| FPGA | mrtvé moduly `phase_oversampler`, `phase_check` ve `spi_app.v` | žádný zdroj (syntéza je zahodí), jen čistota kódu | TODO: přesunout do `legacy/` po kontrole testbenchů |
