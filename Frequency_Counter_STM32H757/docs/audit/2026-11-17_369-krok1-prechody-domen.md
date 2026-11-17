# #369 krok 1: přechody hodinových domén FPGA v3 bez změny RTL (2026-11-17)

Fáze: ANALÝZA (plán `docs/audit/2026-11-15_plan-369-sum-znacky.md`, krok 1 a–d). RTL se nemění; změněn je jen testbench
(`sim/tb_top_v3.sv`: parametr fáze `TB_PH_PS`, výchozí hodnota = dosavadní stav) a přibyl skript `sim/sweep_phase_v3.py`.
Otázka: rozbíjí interval A→B u sestavení bez APP_OSC (t386/t387: σ 3,7–56 ns, skoky o ~10 period během SPI/CS) křížení
domén s koherentními hodinami 10/100 MHz (HYPOTÉZA plánu, kap. 2), nebo něco jiného?

## a) Zdroje podle domén (`tools/syn_hier.py`, sestavení `abl/cdc10` = base bez APP_OSC)

| doména | moduly | registry | LUT | ~CLS |
|---|---|---|---|---|
| `clk_app` (10 MHz z pinu) | `u_app` + synchronizátory stavu v `top_v3` | 494 + ~60 | 1 081 | ~670 (~30 %) |
| SCK / 100 MHz / `clk_app` | `u_phy` | 232 | 194 | 137 |
| `clk_p0_100m` | TDC A/B (`u_tdca/b` vč. řetězů a dekodérů), součty `u_sa/u_sb`, `u_ring`, `u_raw`, 1PPS, předděličky, `u_coh` | zbytek | | ~1 400 |

## b) Seznam přechodů (`Frequency_Counter_FPGA_Module/src/`)

**Data měření A/B** (jediná cesta, kterou jdou záznamy segmentů):
- `v3_core.v:382` `rec_tgl` se přepne až po zápisu obou záznamů → `app_v3.v:318` `rt_s` 3 FF + hrana; data čte aplikace
  z dvouportové BSRAM (`v3_core.v:410` port B na `rclk`, `top_v3.v:390-394`). Zápis a čtení nikdy nejdou na tutéž adresu
  (aplikace čte až dokončenou dvojici, další se zapisuje za 25 ms). **Správně pro asynchronní i koherentní hodiny.**

**Povely aplikace → 100 MHz** (všechny: data nastavena 3 takty `clk_app` = 300 ns PŘED přepínacím bitem, `app_v3.v:305-315`):
- LUT_WRITE `v3_core.v:680-708` (`lut_wr_sync3`: přepínací bit 3 FF, data 2 FF), kalibrace `top_v3.v:294` (`ct_s` 3 FF,
  maska a zdroj 2 FF), RAW `v3_core.v:1102` (3 FF), L 10 ms `v3_core.v:329` (2 FF, převezme se na hranici segmentu).
- Kvazistatické úrovně 2 FF: zdroj B `top_v3.v:231`, předdělička `top_v3.v:240-243` (platí od hranice segmentu, změna označí
  2 segmenty neplatné `top_v3.v:341-345`), maska potvrzení `top_v3.v:265`, maskování CS `top_v3.v:369-372`.

**Stav 100 MHz → aplikace:**
- 2 FF jednobitové úrovně (`top_v3.v:439-448`, `458`, `461`, `571-574` stav koherence), Gray + 2 FF (`top_v3.v:489-497`
  čítače pulzů CS, `571-578` chyby koherence), přepínací bit + 3 FF (`top_v3.v:499` konec rámce SPI).
- ⚠️ `rt_tot` 2 b binárně přes 2 FF (`top_v3.v:447`): přechod 1→2 může dát na 1 takt nesmyslnou hodnotu (jen diagnostika).
- ⚠️ ŽIVĚ bez synchronizace (vícebitové): čas 1PPS `pps_*` (`top_v3.v:515-517`), sonda `probe_*` (`app_v3.v:255`), `maxtap`
  (`app_v3.v:278`), bubliny / přetečení posledního segmentu `lb_* / lo_*` (`top_v3.v:398`), počet RAW `raw_n`. 1PPS a sondu
  STM přijímá až při shodě dvou rámců; ostatní je diagnostika. Výstup histogramu `hq_*` čte aplikace 3 takty po změně
  adresy (`app_v3.v:465`).

**SPI PHY** (`spi_slave_phy_v3.v`): CS 2 FF + filtr 16 taktů (`:93-111`), přepnutí poloviny TX jen mimo CS (`:213-214`,
`half_s` 3 FF), data RX čte aplikace až po `frame_end_tgl` (`:213-221`) z RAM (`:168-171`). **Správně.**

## c) STA s analyzovanými přechody 10 ↔ 100 MHz (`abl/cdc10`: obě hodiny v jedné skupině, nulová fáze)

Sestavení 138 s, CLS 3 423, Fmax 100,685 MHz (vnitřní cesty 100 MHz beze změny, nejhorší +0,068 ns `u_ipps/f_ts`).
Porušení: setup 7 (nejhorší −5,731 ns), hold 41 (nejhorší −0,331 ns):
- **setup: všech 7 cest 100 MHz → `u_app/d1_*`** (skládání bajtu hlavičky rámce) z živě čtených hodnot: čas 1PPS `u_pps/pts_l_*`
  (−5,7 a −5,0 ns), `u_tdca/d_maxtap_*` (−1,2 / −0,9 ns), výstup histogramu `u_tdca/hist_*/DO` (−1,1 / −0,2 ns);
- **hold: první stupně synchronizátorů** (`csg_s1_*`, `cst_s1_*`, `u_app/prb_1_*`) a kvazistatické záchyty (`u_app/a_ptot_*`
  z `u_raw/n`, adresa RX RAM `u_phy/rx_rd_half` → `rxram/ADB`) — u záměrně asynchronních přechodů očekávané;
- **žádná porušená cesta nevede do dat segmentů A/B** (`u_sa/u_sb`, `u_ring`) ani z domény aplikace do 100 MHz.

→ STA hypotézu „koherentní fáze rozbíjí přechod, kterým jdou data A→B“ nepodporuje: takový přechod v návrhu není. Koherentní
pevná fáze může trvale poškozovat jen hlavičku (čas 1PPS, maxtap, histogram) — u 1PPS by to STM odhalila nesouladem dvou rámců.

## d) Simulace s pevnou fází 10/100 MHz (`sim/sweep_phase_v3.py`, test `topv3pw`: šířka pulzu a zpoždění A/B, bez APP_OSC)

Sweep `topv3pw` bez APP_OSC, fáze 100 MHz proti 10 MHz 0 / 2,5 / 5,0 / 7,5 ns (`python sim/sweep_phase_v3.py topv3pw 2500 4`, ~40 min): **všechny 4 fáze PASS**, chyba šířky pulzu 19 / 23 / 57 / 25 ps, rozdíl zpoždění A/B 32 / 34 / 24 / 7 ps, žádný skok o periodu. Nejhorší je fáze 0 ns (shodné hrany taktů, 57 ps). RTL simulace nemá metastabilitu ani zpoždění vedení, takže nevylučuje chybu na křemíku, ale funkční závislost na pevné fázi ukazuje nulovou. Hrubý krok 2,5 ns neprokazuje, že neexistuje úzké okno.

## Závěr kroku 1

1. **Přechody domén (b) a STA (c) hypotézu plánu (kap. 2) nepodporují:** data A/B jdou jen přes BSRAM + přepínací bit s 3 FF, povely mají data 300 ns před přepínacím bitem; porušené cesty STA jsou jen v hlavičce (čas 1PPS, maxtap, histogram) a v asynchronních synchronizátorech.
2. **Pravděpodobnější příčina rozbitého A→B bez APP_OSC je v obsluze souhrnu B, ne v hodinách:** v0.45.22 zahazoval každý start s předděličkou (deska 2026-11-17: 531 z 531), opraveno v v0.45.23 (L-0294). Zda stejná chyba vysvětluje i t386/t387 (tam B bez předděličky), nejde z dat říct — ověřit po flashi bitstreamu bez APP_OSC.
3. Sweep fáze (d) nenašel funkční závislost. Zbývá změřit interval A→B s v0.45.23 na APP_OSC i bez něj (bitstream bez APP_OSC do SRAM).
