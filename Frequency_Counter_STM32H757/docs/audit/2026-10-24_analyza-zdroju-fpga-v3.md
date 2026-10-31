# Analýza zdrojů FPGA v3 a přesunu výpočtů do STM (FW 0x0506 → návrh 0x0507), 2026-10-24

Navazuje na `docs/audit/2026-10-23_studie-cls-v3.md`. Otázky: (1) dá se FPGA dál optimalizovat, (2) využíváme
všechny prostředky a nástroje GW1NR-9C, které by pomohly, (3) co se dá přesunout do matematiky STM.
Měřeno celým P&R (`tools/abl_cls.py`, Gowin V1.9.12, stejný `build_id.vh` pro všechny varianty); RTL v repu
se při měření NEMĚNILO (varianty jsou kopie v `abl/`). ⬜ Nic z návrhu zatím není implementováno ani simulováno.

## 1. Využití prostředků GW1NR-9C (FW 0x0506, P&R `v3/impl/pnr/Counter_FPGA_v3.rpt.txt`)

| prostředek | využito | poznámka |
|---|---|---|
| CLS | 3 809 / 4 320 (88 %) | brána 85 % = 3 672 |
| logika (LUT + ALU) | 5 495 / 8 640 (64 %) | 3 681 LUT, 1 814 ALU |
| registry | 4 450 / 6 771 | z toho v IOB jen **4 / 291** |
| **SSRAM (RAM16, LUT‑RAM)** | **0** | nevyužito |
| BSRAM | 16 / 26 | **10 volných** |
| DSP (ALU54D) | 4 / 10 | **6 volných**; dnes jen součty SX/ST |
| IOLOGIC | 2 / 97 | jen IDES8 1PPS |
| rPLL | 1 / 2 | 400 MHz pro IDES8 |
| CLKDIV / OSC | 1 / 8, 1 / 1 | |

Volby Gowinu (`v3/impl/Counter_FPGA_v3_process_config.json`) jsou už nastavené výhodně: `Ram_RW_Check = false`
(bez obtokové logiky kolem BSRAM), registry do IOB povolené (`PlaceInRegToIob`, `PlaceOutRegToIob`), `place_option 2`.
Studie 2026-10-23 ukázala, že `maxfan`, `replicate_resources`, `retiming`, `route_option` ani `timing_driven` CLS nesníží.

### Které nevyužité prostředky mohou pomoct

- **DSP (6 volných): ano, změřeno.** Dlouhé čítače jdou do ALU54D atributem `syn_dspstyle = "dsp"`.
  `tick_hi` (34 b) −54 CLS a časování se nezhoršilo (+0,006 ns); horní část čítače hran X obou kanálů (2 × 32 b)
  −22 CLS, ale rezerva −0,5 ns (DSP leží v řádku R19 daleko od TDC). Další kandidáti: `seg_idx` (32 b), `pwc`.
- **BSRAM (10 volných): pro CLS nepomůže.** Největší blok logiky je hlavičkový multiplexer (`u_app`, ~800 LUT).
  Přesun stavových polí do BSRAM by potřeboval stejný multiplexer na straně zápisu. Plocha klesá jen tím, že
  pole zmizí nebo se zúží (kap. 2).
- **SSRAM (LUT‑RAM): nevýhodné.** Mělké paměti v návrhu jsou FIFO 2 × 40 b na kanál (`seg_chan3` `fh`/`fs`).
  RAM16SDP4 zabírá celou část CFU, takže 40 b šířky = 10 bloků a úspora proti 80 FF je nejistá [HYPOTÉZA,
  neměřeno]. Lepší je FIFO zúžit (kap. 5).
- **Registry v IOB: zanedbatelné.** Vstupní piny kanálů budí řetěz TDC, čítač předděličky i synchronizátor
  zároveň, takže první FF do IOB nejde; 1PPS má IOLOGIC (IDES8). Jde o jednotky FF.
- **Druhá rPLL, IODELAY, uživatelská flash:** pro plochu nic. IODELAY by šlo použít k doladění zpoždění A/B,
  to je ale přesnost, ne CLS.
- **Sdílení `tr1/tr2` (tick − 1/−2) mezi kanály:** −2 CLS, registry beze změny. Gowin už stejné registry obou
  kanálů sloučil sám. Zbytečné.

## 2. Co STM nepotřebuje nebo může spočítat sám

Projito proti `CM7/Core/Src/fpga_v3.c` (`fpga_v3_parse`, ř. 135–178) a jeho uživatelům v `fpga_freq.c`.

| bajty | pole | stav ve STM | návrh |
|---|---|---|---|
| 28..31 | `TS10M` (čítač 10 MHz při skládání) | parsuje se do `h->ts10m`, **nikde se nepoužije** | zrušit (= 0) |
| 56..59 | `RX_LAST` | `h->rx_last`, **nikde se nepoužije** | zrušit (= 0) |
| 84..95 | předchozí 1PPS (PSEQ, PTS, PFLAGS, d) | `fpga_freq.c:1191` (párování hrany s 1PPS: poslední, jinak předchozí) | STM si drží historii posledních 2 záznamů 1PPS sám (každou hranu vidí, rámců je ≫ 1/s) |
| 32..35 | `REC_HEAD − 1` | `fpga_v3.c:315` | posílat `REC_HEAD` (bez odčítačky), STM odečte |
| 42..43 | `RING_FILL` | `fpga_v3.c:149`, jen výpis | zrušit, STM = HEAD − FIRST |
| 32..39 | `REC_HEAD`, `REC_ACK` 32 b | 32b porovnání mezer | **16 b** (kruh má 512 záznamů), STM rozbalí na 32 b |
| 4..7 | `SEQ` rámce 32 b | `fpga_freq.c:2748` jen dolních 8 b | **16 b**, STM rozbalí |
| 68..71 | `PSEQ` 32 b | `fpga_freq.c:1072` 8 b, `:1194` 32 b | **16 b**, STM rozbalí (1PPS 1/s → přetečení po 18 h) |
| 112..115 | počty bublin / značek při CS v posledním segmentu | `h->bub`/`h->ovl`, jen výpis | zrušit; tatáž informace je v FLAGS každého záznamu (bit2/bit3), STM spočítá |
| 79 | `d` 1PPS (jemný − hrubý) z 34b rozdílu | diagnostika | rozdíl jen z 12 b (|d| < 2048 taktů; saturace ±127 zůstává) |

Přesun nic neztrácí. Všechny 16bitové hodnoty se mezi dvěma po sobě jdoucími rámci změní o víc než o polovinu
rozsahu jen při výpadku STM delším než ~18 h (PSEQ), ~13,6 min (záznamy: 80/s při L = 25 ms, 2 na segment) nebo 65 536 rámců. Při takovém výpadku
STM stejně začíná znovu (přetečení kruhu je hlášené FLAGS bit1).

**Matematika, která ve FPGA zůstat MUSÍ:** součty segmentu (N, X0, T0, SX, ST). Jsou to minimální postačující
statistiky. Posílat jednotlivé značky by při 20 MHz znamenalo 2 × 20 M značek/s, což SPI nepřenese. SX se nedá
odvodit z N, protože při f > 20 MHz se časuje jen každá k‑tá hrana s proměnným k. Ve FPGA zůstává i skládání
času 1PPS, kalibrační histogram, sonda předděličky a samokontroly (kontrolní počítání hran, hlídač hodin
a koherence). Všechny pracují s hardwarovým časováním, které STM nevidí.

## 3. Ostatní úspory ve FPGA (bez změny protokolu)

- **`tmr`: dlouhé časovače na společnou časovou základnu 1 ms.** Časová základna je okno sondy `pwend`, které
  už existuje. Týká se těchto časovačů:

  | časovač | dnes | nově |
  |---|---|---|
  | náběh `boot_cnt` | 24 b | 8 b |
  | timeout kalibrace `cal_tmo` | 29 b | 13 b |
  | prodleva opakování `rt_dly` | 20 b | 4 b |
  | `coh_hold` | 24 b | 8 b |

  Funkčně stejné, časy se změní nejvýš o 1 ms: 167 ms, 5,37 s, 10 ms a 168 ms. V simulaci (`SIM_TOP`) je ale
  potřeba kratší okno (dnes 1 ms = 100 000 taktů), jinak náběh trvá 167 ms simulace.
- **`pnz`**: v `top_v3.v` se ve stavu 1PPS porovnává `pps_seq != 0` ve 32 b, přestože existuje registr `pps_nz`.
- **`dTick`**: `tick_hi` do DSP (kap. 1).

## 4. Měření (celé P&R, varianty v `tools/abl_cls.py`)

| varianta | CLS | registry | logika | ALU | DSP | setup [ns] |
|---|---|---|---|---|---|---|
| základ (FW 0x0506) | 3 809 | 4 450 | 5 495 | 1 814 | 4 | −0,378 |
| hPPSp (předchozí 1PPS z hlavičky) | 3 751 | 4 403 | 5 339 | 1 814 | 4 | −0,207 |
| hTS + hRXL + hFill + pnz | 3 727 | 4 331 | 5 327 | 1 767 | 4 | −0,913 |
| hBub | 3 761 | 4 406 | 5 357 | 1 799 | 4 | +0,068 |
| dd12 | 3 761 | 4 428 | 5 415 | 1 793 | 4 | −0,028 |
| pseq16 + seq16 | 3 748 | 4 418 | 5 402 | 1 783 | 4 | −0,284 |
| p16 | 3 749 | 4 410 | 5 263 | 1 735 | 4 | +0,028 |
| tmr | 3 762 | 4 386 | 5 391 | 1 739 | 4 | −0,635 |
| dTick | 3 755 | 4 403 | 5 444 | 1 781 | 5 | +0,006 |
| dX | 3 787 | 4 370 | 5 390 | 1 751 | 6 | −0,508 |
| shTR | 3 807 | 4 450 | 5 496 | 1 815 | 4 | −0,907 |
| **vše (bez dX, bez shTR)** | **3 635 (84,1 %)** | **4 035** | **4 788** | 1 523 | 5 | **−0,081** |
| vše + dX | 3 568 (82,6 %) | 3 971 | 4 719 | 1 459 | 7 | −0,746 |

Čtení tabulky:
- Jednotlivé rozdíly kolem −50 CLS jsou zčásti šum rozmístění. Stejný zdroj dával s jiným `BLD_TIME`
  3 834–3 862 CLS (L‑0222).
- Spolehlivé jsou souhrnné řádky: **−415 registrů, −707 buněk logiky, −174 CLS.**
- Časování je v jednom sestavení. Platí L‑0222: bránu je nutné spustit opakovaně.

## 5. Další kandidáti (neměřeno)

- FIFO značek v `seg_chan3` (`fh`/`fs`, 2 × 40 b na kanál): plných 40 b je potřeba jen pro první značku segmentu
  (X0). Ostatní stačí 26 b, nebo X0 počítat jako snímek X na hranici segmentu + posun. Odhad −50 registrů.
- `seg_idx` (32 b) a okno sondy do DSP (zbudou 3 volné ALU54D).
- `RX_CRC_ERR`, `RX_LEN_ERR` 16 → 8 b (STM rozbalí), drobnost.
- T0 v záznamu jen 36 b (STM zná začátek segmentu z mřížky `seg_idx × L`). Mění formát záznamu (31 B), proto až
  s další revizí protokolu.

## 6. Návrh: FW 0x0507 + STM v0.40.0 (čeká na schválení)

1. **FPGA** (`app_v3.v`, `v3_core.v`, `top_v3.v`):
   - Úpravy podle kap. 2 a 3 v rozsahu řádku „vše (bez dX)“.
   - `CAPS` nový bit 22 = „úzká hlavička v3.1“: 16b SEQ, PSEQ a ukazatele, zrušená pole.
   - `FW_VERSION = 0x0507`.
2. **STM** (`fpga_v3.c`, `fpga_freq.c`):
   - Při CAPS bit22 rozbalit SEQ, PSEQ, REC_HEAD a REC_ACK na 32 b a zapisovat REC_ACK jen dolních 16 b.
   - Historie 1PPS ve STM nahradí `pps[1]`; počty bublin a CS spočítat ze záznamů.
   - `ts10m` a `rx_last` z parseru vyhodit.
   - Starší FPGA (bez bitu 22) obsloužit beze změny.
3. **Simulace:**
   - `tb_top_v3.sv`: kontroly bajtů 84..95 a 112..115 a rozbalení 16b ukazatelů.
   - `SIM_TOP` okno 1 ms pro `tmr`.
   - Nové mutace na rozbalení a na časovače.
   - Host testy `tests/host/t_fpga_v3.c` na rozbalení přes přetečení.
4. **Brána:** opakovaná sestavení. Při dnešním šumu časování (±1,5 ns) je čekat, že rezerva bude kolem nuly jako
   u 0x0506; CLS by ale mělo projít.
