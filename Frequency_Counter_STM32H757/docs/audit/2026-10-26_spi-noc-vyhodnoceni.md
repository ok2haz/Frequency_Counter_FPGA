# Noční test SPI v3 (2026-10-25 21:06 → 2026-10-26 12:43, 9 větví, 15,6 h)

Nástroje: STM v0.40.7 (`spi err`, `spi poll`, `spi dma`), FPGA bitstream `28c71c9`, `tools/spi_night.py` (záznam), `tools/spi_night_report.py`
(vyhodnocení). Data: `noc_logs/20261025_2106/` (mimo git; po skončení zkopírovat jinam). Deska běžela od power-cyklu bez restartu, GPSDO
`LOCK` ve všech 483 cyklech, `spi_ok` vždy True, ztráta signálu 0.

## 1. Souhrn

| | doba | transakcí | chyb | chyb/h | **chyb / milion rámců** | p proti A |
|---|---|---|---|---|---|---|
| A základ (25 MHz, polling 40 ms, bez DMA) | 7,53 h | 541 248 | 49 | 6,51 | **90,5** | – |
| B `spi poll 10` (≈ 5× víc rámců) | 2,01 h | 723 119 | 67 | 33,27 | **92,7** | 0,93 |
| C `spi dma on` | 4,01 h | 288 623 | 24 | 5,98 | **83,2** | 0,81 |
| E SCK 6,25 MHz | 2,01 h | 141 482 | 15 | 7,47 | **106,0** | 0,64 |
| celkem | 15,56 h | 1 694 472 | 155 | | 91,5 | |

**Chyba je událost na rámec (≈ 1 na 11 000 rámců), nezávisí na čase, kmitočtu SCK (25 i 6,25 MHz), DMA/CPU ani počtu rámců za sekundu.** Větev B má
4,99× víc rámců za hodinu a 5,11× víc chyb za hodinu. Tím jsou vyřazeny hypotézy „šum úměrný času na lince", „CPU/přerušení při čtení po SPI"
a „malá rezerva časování při 25 MHz".

## 2. Druhy chyb

- Druh 1 (CRC bloku 0): 92, druh 2 (CRC bloků 1–3): 43, **druh 3 (MAGIC/VER špatně): 20**. Druh 3 dosud `CRC err` nepočítal (L-0241), tj. 13 % skutečných chyb bylo neviditelných.
- V **112 z 155** chybných rámců (72 %) jsou špatné všechny čtyři bloky; první vadný blok: 0 → 112, 1 → 18, 2 → 11, 3 → 14. Blok 3 je vadný ve **všech** chybách,
  tj. chyba začne v nějakém bodě a trvá do konce rámce.
- Žádné dva po sobě jdoucí rámce nejsou vadné (155 izolovaných, nejdelší série 1).
- Fáze vůči PPS (`since_pps_ms`): chi² = 17,1, p = 0,048 (n = 150), tedy bez zřetelné závislosti. SEQ a číslo rámce modulo 2^k: bez závislosti (p ≥ 0,067).
- Rozptyl počtů chyb po 20 min ve větvích A je 2,0× Poissonův, takže mírné shlukování (43 z 154 sousedních chyb ve stejném 120s cyklu, většinou ve větvi B).

## 3. Mechanismus: vysílání FPGA se uprostřed transakce restartuje od bajtu 0

**Pozorování**
1. **U všech 109 vadných bloků 0 sedí všech 144 konstantních bitů hlavičky rámce** (MAGIC, VER, TYPE, FRAME_LEN, NBLK, git, FW, CLK, SEG_LEN, CAPS) na některé bitové pozici q: u 89 po q = 2…116 bitech (začátek rámce se znovu objeví uprostřed bloku), u 20 v neposunuté poloze (restart až později).
2. **Pozice q jsou ve bitech stejné pro 25 i 6,25 MHz** (A, B, C: 6 (12×), 15 (5×), 34–45, 78 (8×), 86 (9×), 97 (12×), 99, 107–116; E: 2, 5, 7, 14, 38, 41, 77, 85, 106). Tedy je to vázané na počet hran SCK od začátku transakce, ne na čas.
3. **FPGA počítadlo `RX_LEN_ERR` vzrostlo o 323 (11 → 334) při 155 chybách na STM, tj. 2,08 na chybu** (2,06 / 2,06 / 2,08 / 2,27 ve větvích A / B / C / E). V tabulce cyklů dominuje „+2 délka ↔ 1 chyba STM" (76 cyklů). `RX_CRC_ERR` 1 → 2.
4. Zbytek bloků 1–3 (43 chyb) má správnou značku bloku `{k, SEQ[7:0]}` a často i platná data, ale 2 bajty CRC nesedí; stejná hodnota CRC se objevuje u různých dat (`A0B4` 5×, `A034`, `A539`, `28CD` po 2×), tj. CRC registr neodpovídá datům.

**Soulad s RTL** (`spi_slave_phy_v3.v`): CS prochází 3 klopnými obvody při 100 MHz; `clr <= cs_fall` je **asynchronní reset** čítačů TX i RX v doméně SCK. Každý pulz na CS delší než jeden takt (10 ns)
dá `cs_rise` (vyhodnotí délku `rcnt ≠ 8192` → `RX_LEN_ERR` +1), hned `cs_fall` (restart vysílání od MAGIC, reset CRC) a na skutečném konci transakce další `RX_LEN_ERR` +1. To odpovídá bodům 1–3 včetně „+2".

**Co nevíme (HYPOTÉZA):** proč CS krátce zakmitne. Kandidáti:
(a) přeslech nebo zakmitnutí na vedení CS (vstup FPGA `LVCMOS33`, `PULL_MODE=UP`, bez hystereze, vstupní filtr žádný), závislé na datovém vzoru (shluky q = 6, 78, 86, 97 odpovídají přechodům v hlavičce MOSI/MISO);
(b) zem/napájení FPGA při přepínání výstupů; (c) software na STM (zápis do `GPIOB` jiným jádrem nebo hlídač GPIO) – proti tomu mluví nezávislost na čase a počtu rámců, ale není vyloučeno.

## 4. Dopad na měření

Žádný zjistitelný: GPSDO `LOCK` 483 / 483 cyklů, `spi_ok` False 0×, `signal_lost` 0×, SEQ díry 3 → 3 (beze změny), zamítnutá okna (citani / hodiny / délka) **0 / 0 / 0**. Chybné rámce se opakují a záznamy se po ACK pošlou znovu.
**Oprava bit4 je potvrzena na desce:** před opravou 7,6 zamítnutých oken „citani" za hodinu (v 16 h by to bylo ≈ 120), po opravě 0 za 16 h.
σy@1s (median po větvích, 10⁻¹⁵): A1 83 882, B1 45 641, A2 141 723, C1 121 091, A3 112 503, E1 89 954, B2 109 971, C2 162 160, A4 58 515. Kolísání je v čase, ne po větvích (zdroj je externí GPSDO, nehodnoceno).
Teploty OCXO 47,4–48,5 °C, deska 30,0–31,9 °C, Vc 1 919–1 920 mV.

## 5. Další nálezy

- `tools/raw_fetch.py` skončil v 05:33 na jediné neodpovězené žádosti (`deska neodpovida`), nemá opakování → `raw.csv` pokrývá jen 21:07–05:33 (30 465 řádků, gate 1 s).
- Web (CM4/ETH) neodpovídal: 05:01:37 (5 s), **05:33:22–05:36:42 (3 min 20 s)**, 06:35:27–06:35:52; UART, SPI a GPSDO přitom běžely. Příčina neznámá (CM4/ETH nebo síť PC); `status` neukládal stav CM4.
- Přepnutí `spi dma` / `spi <MHz>` za běhu pokazí jednu transakci (zachyceno při zkoušce).
- FPGA `RX_LEN_ERR` základ pro další měření: 11 (z přepínání při zkouškách), 334 na konci noci.

## 6. Doporučené kroky (pořadí podle ceny)

1. **FPGA: filtr CS** (odmítnout pulzy kratší než např. 8–16 taktů 100 MHz; STM drží CS ≥ 2 µs před i po SCK, takže zpoždění 160 ns nic nemění) a **čítače pulzů CS v hlavičce** (rezerva bajtů 132+): počet a šířka (1, 2, 3–4, 5–8 taktů). Je to oprava i měření: čítač řekne, jak často a jak široké pulzy na CS přicházejí.
2. **STM: `spi pins <low|medium|high|very>`** za běhu (rychlost hran SCK, MOSI, CS; dnes SCK/MOSI = MEDIUM, CS = HIGH) a opakovat noční větve; levný test hypotézy přeslechu.
3. **FPGA/HW:** `HYSTERESIS` na vstupech CS/SCK (`pins.cst`), nižší `DRIVE` a pomalejší hrana MISO; osciloskop nebo logický analyzátor s triggerem na pulz CS < 10 µs při aktivním SCK, na konektoru FPGA.
4. **Nástroje:** opakování žádostí v `raw_fetch.py`; do `spi_night.py` přidat `status` řádek CM4 (stall, ETH) a záhlaví CSV zapisovat okamžitě.

## 7. Omezení vyhodnocení

Pozice restartu byla zjištěna jen v blocích 0 (109 z 109 případů); u 43 chyb v blocích 1–3 je vysvětlení jen částečné (restart těsně u CRC bajtů a konstantní CRC). Mutace a rozbor fyziky vedení (osciloskop) chybí; závěr „CS pulz" je odvozen z RTL a z `RX_LEN_ERR` +2, ne z přímého měření.
