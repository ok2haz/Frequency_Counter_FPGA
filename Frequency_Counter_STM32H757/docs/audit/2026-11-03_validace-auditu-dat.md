# Validace auditu naměřených dat (2026-11-03) a souhrn rozborů TDC A/B

Vstup: `../exchange/PRO_DRUHOU_INSTANCI_audit_dat_2026-11-03.md` (dále „podklad“, druhá instance, data 7. 10. – 3. 11.).
Tento dokument (1) shrnuje rozbory z 31. 10. – 3. 11., (2) ověřuje tvrzení podkladu proti datům a kódu, (3) navrhuje
opravy podle priority. **Fáze auditu: kód se neměnil.** Skripty ověření: `noc_logs/_rozbor/val_hist.py`,
`val_lines.py`, `noc18_prof.py`, `noc18_sym.py`, `scan_eval.py` (adresář je v `.gitignore`, viz D3).

## 1. Souhrn rozborů (31. 10. – 3. 11.)

| datum | krok | výsledek |
|---|---|---|
| 31. 10. | FW 0x050A: `clr_d` lokální FF pro CE/RESET DSP (L-0252) | simulace PASS |
| 1.–2. 11. | noc 15,5 h S6 | **0 vadných segmentů z 2,2 mil. → oprava DSP B potvrzena na HW** (L-0252 ✅) |
| 1. 11. | „srovnání sestavení“ @3 / S2 / S6 / 0x0508, hypotéza o síti `ch_a_d` / poloze RO | **neplatné** — popisky kanálů prohozené (L-0253) a srovnávalo se δ₀ (viz níže) |
| 2. 11. | oprava nástrojů `ab_night.py`, `tdc_gensweep.py` (kanál podle `channel`, `333ed78`) | selftest PASS |
| 2. 11. | test vazby (vypínání generátorů, rozladění) | **chyba kanálu závisí na poloze hran druhého kanálu**; jen A 79, jen B 95 ps; oba 60–196 ps podle posunu |
| 2. 11. | prohozené kabely, 3× restart CLK4, `gen 6 off` | restart výstupu (OEB) posun **nemění**; 200 MHz vliv nemá |
| 2.–3. 11. | noc 17,5 h, posun A−B plynule (+0,001 Hz), koše 1 ns | σ(A−B) **60–262 ps** podle posunu; profil opakovatelný přes kalibraci (r 0,85–0,96) a nezávislý na tom, který generátor se hýbe; dva hrby na kanál 36 / 64 ns od sebe |
| 3. 11. | validace podkladu (tento dokument) | viz oddíl 2 |

**Dnešní obraz TDC (S6, 0x050A, generátor ,041 Hz):**
- vlastní chyba kanálu (bez vazby) **A 75–84 ps, B 95–115 ps** (složka 10 ns), opakovatelná, patří **kanálu**, ne výstupu generátoru;
- vazba A↔B přidává podle posunu hran δ úzké hrby (6–10 ns, až +130 ps); posun se mění **jen rozladěním nebo power-cyklem**;
- po odečtení deterministické části zbývá **4–5 ps šumu na kanál** a 5–9 ps jemné struktury (harmonické k 150–190 vlastní fáze);
- **soustavná chyba #348** (±2,4·10⁻¹²) = periodická porucha s periodou 1 s zamčená na mřížku oken (oddíl 2.2).

## 2. Ověření podkladu

Značky: ✅ potvrzeno vlastním výpočtem/kódem, ⚠️ částečně / nutná oprava podkladu, ❌ neplatí, ⏳ zastaralé (data se
mezitím změnila), — neověřeno (převzato s jistotou podkladu).

### 2.1 Pasti v datech (podklad § 0)
| tvrzení | stav | důkaz |
|---|---|---|
| L-0253 prohozené popisky, seznam běhů | ✅ | `channel` = 1 v `ab_20261030_2138..2158`, všech `ab_20261101_*`, `ab_20261102_1642..1704` |
| `seq` +2 za libovolné přerušení | ✅ | `fpga_freq.c:1181` `s3_wseq += s3_brk ? 2u : 1u` |
| `ab_20261103_0344` neúplná (1,9 h) | ⏳ | v repu 79 088 oken = 5,5 h; podklad měl kopii pořízenou za běhu |
| `ab_20261102_1756` prázdný | ✅ | příčina: CHAN B + vypnutý B → web neposílá ani A (opraveno etapou 4b) |
| chybí `vazba_ab/souhrn.txt` | ⚠️ | soubor existuje, ale `noc_logs/` je v `.gitignore` → MERENI odkazuje na data mimo git (D3) |
| noc 2./3. 11. chybí v MERENI | ⏳ | zapsáno v `8ea90aa` (15:01), podklad je ze 17:03, ale ze starší kopie |

### 2.2 Periodická porucha a #348 (podklad § 1.2) — ✅ do čísel
Noc `ab_20261101_2106`, nejdelší souvislý úsek 48 128 oken (3,3 h), model vlastní fáze P 100 ns, K 130 (`val_lines.py`):
- střední chyba **A −2,37·10⁻¹², B +2,45·10⁻¹²** (podklad −2,36 / +2,46);
- třída okna mod 4: **A +0,35 +0,40 −2,73 −0,36, B −0,18 −0,39 +2,46 +0,40 ps** (podklad −2,74 / +2,51);
- čáry na mřížce j/128: 1 s (j 32) **1,59 / 1,38 ps**, 0,5 s (j 64) 1,21 / 1,14, 0,8 s (j 40) 0,73 / 1,17 — **A a B v protifázi**
  (169–180°); liché harmonické 16 s (j 10, 30, 50) **ve fázi** (+8…+10°). Přesně podle podkladu.
- **Doplněk:** nejsilnější čáry mimo mřížku (0,0525 / 0,155 / 0,2575 / 0,36 / 0,4625 cyklu/okno) jsou přesně harmonické
  k = 190 / 180 / 170 / 160 / 150 vlastní fáze 100 ns (posun 1025 ps/okno) → je to jemná struktura TDC nad K 130
  (podklad § 1.4 „m 15–30“), ne další porucha.
- **Nové zjištění k mechanismu #348:** střední chybu dělá **jen rodina 1 s** (perioda 4 okna → třída mod 4); rodina 8 / 16 s
  se ve třídě mod 4 vyprůměruje. Zdroj s periodou přesně 1 s, zamčený na mřížku oken (= OCXO), je při GPSDO LOCK
  **1PPS** (pin 33, banka 2 se společnou VCCIO s A a B, impulz 100 ms). Podklad 1PPS vylučuje jen pro 4–16 s, ne pro 1 s.
  HYPOTÉZA, rozhodne pokus P-A4 (oddíl 3).

### 2.3 Kalibrace TDC (podklad § 1.5)
| tvrzení | stav | důkaz (`val_hist.py`) |
|---|---|---|
| noční `hist_0` nesedí (součet ≠ 2²⁰, kód 144 B) | ✅ | A 1 048 585, B 1 052 754; kód 144 B **10 631** proti 2 517–2 691 |
| kalibrace téhož zapnutí se liší o 5–9 (A) / 7–18 (B) ps | ✅ | párově bez `hist_0`: A 7,4–9,2 ps, B 8,0–18,6 ps (bez střední hodnoty); Poissonova mez 5,6 ps |
| „24–27 ps“ v MERENI je artefakt `hist_cmp.py` | ✅ | srovnání proti průměru obsahujícímu vadný `hist_0` (A `hist_0` proti ostatním 73–75 ps) |
| orientace E(φ) = −ε(c−φ), `tdc_rawpin.py` bez zrcadlení | — | neověřeno |

### 2.4 Vazba A↔B (podklad § 1.1)
| tvrzení | stav | důkaz |
|---|---|---|
| chyba = deterministická funkce δ, opakovatelná | ✅ | noc 17,5 h: F1 vs F5 r 0,85–0,96; F3 (druhý generátor) po posunu osy r 0,78–0,86 |
| vlastní chyba patří kanálu, ne výstupu CLK2/CLK4; „horší je kanál z CLK4“ neplatí | ✅ | jen A: 79 ps (CLK2) / 75 ps (CLK4); jen B: 95 ps (CLK4) / 97 ps (CLK2). Můj výrok v MERENI (`3213d6e`) vyvozen ze stavů s vazbou → **chybný** |
| posun A−B nemění restart generátoru, jen rozladění / power-cyklus | ✅ | 3× `gen 4 off/on` beze změny; STATUS #347 tvrdí opak → oprava D1 |
| sudý díl s periodou 50 ns 61–82 %, lichý 18–39 % | ⚠️ | moje profily: dva hrby na kanál **36 / 64 ns** od sebe, ne 50 ns → spíš střída druhého signálu ≠ 50 % (nebo dvě místa zpracování); měřit `fpga pw` (P-A7) |
| δ₀ předpovídá chybu i mezi zapnutími a bitstreamy | — | v souladu s mými daty, `align.py` jsem nespouštěl |
| přesnost 12–16 ps / tažení kmitočtu ±20–40 % | — | neověřeno |
| kde vazba vzniká | NEROZHODNUTO | shoda s podkladem |

### 2.5 Kód (podklad § 2 B)
| místo | stav |
|---|---|
| `freertos_task_fpga.c:281` `now_ms >= tdc_cal_next_ms` (přetečení tiků po 49,7 d) | ✅ skutečná vada (zablokování i předčasné spuštění) |
| `si5356.c:230–234` `gen` vypne výstup (OEB) během zápisu → skutečný výpadek | ✅ |
| `fpga_v3.c:639` `per_ref` se aktualizuje i z vadného páru X0 | ✅ |
| `fpga_v3.c:749` `tc0 = (double)t + st/n` s absolutním t | ✅ kód; dopad (2,8 ps po 57 ks) neověřen |
| `gps.c:448` 1PPS 100 ms | ✅ |
| `top_v3.v:374` maskování první značky po CS | — neotevřeno |

### 2.6 Ostatní (GPSDO, SPI noci, 660 kHz, PI3, 1PPS F0a)
Neověřeno — přebírám s jistotou podkladu. Pozn.: „šum 1PPS ≤ 3 ns“ je v souladu s již zapsaným rozptylem druhých
diferencí 63 ns² po opravě sawtooth (σ ≈ √(63/6) ≈ 3,2 ns); údaj „~30 ns“ v CLAUDE.md je tedy nadsazený.

## 3. Návrh oprav a pokusů (podle priority; nic neprovedeno, čeká na souhlas)

### P1 — opravy firmwaru, malé a jednoznačné (STM)
1. **`tdc hist` / kalibrace: kontrola součtu 2²⁰** po sběru; při nesouhlasu kalibraci nepřijmout a zopakovat
   (noční `hist_0` je skutečný důkaz). Volitelně 2× kalibrace a přijetí při rozdílu < 20 ps rms.
2. **`tdc_cal_next_ms`**: porovnávat `(int32_t)(now_ms - tdc_cal_next_ms) >= 0`.
3. **`gen`** ať zneplatní okna obou kanálů na dobu zápisu (+ 1 okno); `per_ref` aktualizovat jen z platného páru.
4. **`sequence`**: díra = skutečný počet chybějících oken (z času HAL / čísla buňky), ne +2.

### P2 — metrologie (#348)
5. Vedle LSQ počítat **teleskopickou (dvoubodovou) délku okna** a střední kmitočet z ní (`freertos_task_fpga.c:191`,
   `fpga_freq.c:2056`). Nejdřív pokus P-A1 (podklad A1) — rozhodne, zda #348 je artefakt LSQ.

### Pokusy (podle informace na hodinu)
| # | pokus | kabely | rozhoduje |
|---|---|---|---|
| P-A4 | **zdroj čáry 1 s = 1PPS?** změnit délku impulzu TP5 (10 / 500 ms) nebo 1PPS odpojit; nebo běh bez fixu (TP 10 Hz) | ne / R50 | zmizí-li čára 1 s a třída mod 4 → #348 = vazba 1PPS v bance 2 |
| P-A7 | `fpga pw` (střída) obou vstupů; plynulý test na 5 MHz | ne | hrby 36/64 ns = hrany druhého signálu (škálují s periodou), nebo pevné zpoždění ve FPGA |
| P-A2 | noc s vypnutým datalogem W25Q | ne | rodina 8 / 16 s |
| P-A3 | jeden kanál z nezávislého zdroje (externí GPSDO) | ano | vazba v Si5356, nebo v čítači |
| P-A1 | 10 000 040 / 9 999 960 Hz, ±0,1 Hz, teleskopická délka | ne | #348 artefakt LSQ, nebo skutečná chyba |
| P-A6 | `tdc cal pin gen` po kanálech, druhý generátor vypnutý, 2× | ne | opakovatelná kalibrace vstupní cesty (vlastních 75–115 ps) |

### P3 — nástroje
6. `ab_night.py`: v každém cyklu zapisovat teploty, napětí, stav 1PPS / GPSDO, md5 bitstreamu; δ₀ (`fpga ab`, až bude ověřeno,
   že měření neruší).
7. `scan_eval.py` / `tdc_gensweep.py`: K volit automaticky pod slepou harmonickou (při 1025 ps/okno je slepá k 14 pro P 10 ns;
   v koších s K 20 fit divergoval až na 4,5 ns).
8. `hist_cmp.py`: párové srovnání + kontrola součtu; `tdc_geneval.py`: δ₀ a srovnání jen při stejném δ₀.
9. 21 reportů s prohozenými kanály přegenerovat (`ab_night.py --report`, čte už podle `channel`).

### P4 — dokumentace
- D1 STATUS #347: posun mění rozladění a power-cyklus, ne restart; `gen 6 off` proběhl (bez vlivu). **Provedeno tímto commitem.**
- D2 MERENI: „horší je kanál z CLK4“ a „24–27 ps“ označit za neplatné. **Provedeno tímto commitem.**
- D3 Data, na která se MERENI odvolává (`noc_logs/_rozbor/*/souhrn.txt`, profily), archivovat do gitu (malé textové
  soubory, např. `docs/audit/data/2026-11-0x/`).
- D4 Zbytek podkladu § 2 D (vyvrácené údaje SPI „2× Poisson“, poměry 1,06/1,07/0,96, „30 ns“ 1PPS, „25/s“, holdover,
  `warmup_ready`) — opravit po jednom, každý s odkazem na podklad.
