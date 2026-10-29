# 2026-10-29 – kanál B FPGA v3: index značky s vynulovanými bity 6, 7, 32, 33

Zdroj dat: noční test `tools/ab_night.py`, 2026-10-28 21:26 – 2026-10-29 13:26 (15,92 h), CM7 v0.42.10, FPGA FW 0x0508
bitstream `ab_test/FW_0x0508_v3_20261027-1514_796c009.fs` (md5 `5482e338…`), oba vstupy na tomtéž zdroji 10 MHz,
hradlo 0,25 s. Adresář `noc_logs/ab_20261028_2126/`. Souvisí s L-0247.

## Nálezy

### N1 – X0 kanálu B: vynulované bity 6, 7, 32, 33 (měřeno)

`x0_velke.csv` (993 odchylek X0 ≥ 8 hran) – pro každou odchylku: správné X0 = přečtené X0 + D (D z `dx` proti
dalšímu záznamu, perioda přesně 250 000 hran), vynulované bity = správné AND NOT přečtené.

| vynulované bity | počet |
|---|---|
| {32}, {33}, {32, 33} (X0 o −1, −2, −3·2³²) | 497 (z toho 37× jen bit 32 při správných bitech 33:32 = 11) |
| {6}, {7}, {6, 7} (X0 o −64, −128, −192) | 155 |
| kombinace {6/7} + {32/33} | 341 |
| jiné bity / nastavený bit | **0** |

Žádný jiný bit se nikdy nezměnil a žádný bit se nikdy nenastavil. U skupiny {6, 7} je dolních 6 bitů vždy správných
(155/155). Skripty: `docs/audit/data/2026-10-29_kanal_b/` (`x0bits.py` = tabulka výše, `sxsnap.py` = N2,
`korel.py` a `ocas.py` = N4; spouštět `python -I <skript> <adresar testu>` nebo `<x0_velke.csv>` / `<uart.log>`).

### N2 – SX kanálu B: tentýž podpis (měřeno)

`uart.log` (`fpga v3 seg`, 766 snímků segmentů): normální segmenty (N = 249 997, bez příznaku CS) mají správné
SX = `0x7_4697CE8E`. Vadné snímky: horní část menší o k·2³² při **přesných spodních 32 b** (k = 1 … tisíce, mod 2¹⁶),
nebo spodní část menší o −64, −192, −256, −448, −640, −704 (násobky 2⁶). SX = Σ(xm − x0) se sčítá v DSP
(`v3_core.v:232`, operand A = `xm`), X0 je registr `x0` ve fabric (`v3_core.v:155`, `x0 <= xm`). Obě struktury nesou
vadu v týchž bitech → vzniká v `xm` nebo před ním (FIFO `fh`/`fs` `v3_core.v:93-104`, čtení čítače X `v3_core.v:65-74`).

Ze snímků mimo meze je 747 z 913 opravitelných z mezí SX (spodní 32 b přesné, šířka mezí < 2³²).

### N3 – netlist a časování nic nevysvětlují (měřeno)

`v3/impl/gwsynthesis/Counter_FPGA_v3.vg`, modul `seg_chan3_0` (kanál B): `fh_6..8`, `fh_30..34` = DFFE + LUT3
`INIT 8'hCA/8'hAC` (mux `fs`/`xlo`/`xhi` podle `n230_7`), `fs_*` = DFFE z `xlo`/`xhi`, `xm_*` = DFFE z `fh` s CE `take`.
Bity 6, 7, 32, 33 jsou zapojené stejně jako sousední, bez resetu. `Counter_FPGA_v3.timing_paths`: nejhorší setup
cest `u_sb/*` +1,525 ns (kanál A +0,887 ns), nejhorší hold v návrhu `u_sb/xm → u_sb/g_dsp.u_sx` +0,453 ns (CE DSP je
v taktu změny `xm` neaktivní).

### N4 – A−B ocas (měřeno, příčina částečně)

|A−B| > 100 / 200 / 500 ps / 1 ns / 1 µs: 14 660 / 7 836 / 873 / 186 / 0 z 229 152 oken; σ(MAD) 14,8 ps.
Vynulovaná skupina {6, 7} jedné značky posune těžiště segmentu o 25,6 ps·k; desítky postižených značek dají segment
0,5–2 ns, který mez 2 ns propustí (B 706 takových, A 6) → vysvětluje populaci 100 ps – 2 ns. Osamocená okna
3–14,7 ns (B) zůstávají nevysvětlená; `ab.csv` nenese příznak LSQ/dvoubodové na okno.

## HYPOTÉZA a ověření

Fyzická vada daná rozmístěním kanálu B (logika `seg_chan3` B blízko řetězu TDC B; L-0247: vadné segmenty mají hranu
do ±0,5 ns od hrany taktu, záměna kabelů A↔B vadu nepřesunula). Ověření bez změny zdrojů: nové sestavení
`python tools/fpga_release.py --v3 --build` (jiné `BLD_TIME` = jiné rozmístění, L-0222), 1–2 h `fpga v3 x0` +
`fpga v3 seg`, porovnat množinu vynulovaných bitů (`x0bits.py` nad `x0_velke.csv`). Přesune-li se,
oprava = omezení umístění (`seg_chan3` B dál od řetězu B) a výběr sestavení s čistým během.

Zkušební bitstream (2026-10-29, F0a): `ab_test/FW_0x0508_v3_20261029-1801_b3fbc32.fs` (md5 `7fe3a885…`), zdroje FPGA
shodné s `796c009`, 3. sestavení přes bránu (setup +0,279 ns, Fmax 102,9 MHz, CLS 79 %). Rozmístění jiné: nejhorší
hold kanálu B je nově `u_sb/tm → u_sb/g_dsp.u_st` místo `u_sb/xm → u_sb/g_dsp.u_sx`. ⬜ na desce neměřeno.

## Obejití v STM (v0.42.11, `070b864`)

`sx_repair` (`fpga_v3.c`): kandidáti SX z mezí a podle opraveného X0, platí jen s kontrolou těžiště; mez těžiště
800 ps. ⬜ neověřeno na HW.
