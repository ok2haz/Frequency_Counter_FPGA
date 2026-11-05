// ============================================================
// File: timing_v3.sdc -- casova omezeni bitstreamu PROTOKOLU v3 (top_v3.v, FW 0x0500), 2026-10-22.
// Odvozeno z timing.sdc (v2): stejne hodiny, nejistota 0,9 ns, I/O SPI, TDC (u_tdca/u_tdcb), ref_guard, LED.
// ODSTRANENO: multicycle vyjimky oken a regrese (u_wra/u_wrb) -- v top_v3 nejsou (odkaz na neexistujici objekt
// je v Gowin chyba TA2003). Jadro v3 (seg_chan3, v3_ring) zadnou vyjimku nepotrebuje: kazda cesta ma <= 2 urovne
// LUT (rozdily 40 b ve dvou stupnich, soucty s registrovanym prenosem, slovo zaznamu ve dvou stupnich).
// ⬜ Overit P&R (Windows): build_v3.tcl, pak sim/check_tdc_placement.py a brana fpga_release.py.
// ============================================================

create_clock -name clk_ref_10m   -period 100.0 -waveform {0 50.0}  [get_ports {clk_ref_10m}]
// Referencni hodiny FPGA = 100 MHz (Si5356 CLK0/1, perioda 10 ns). FW 0x0421/0x0422 bezely na 125 MHz (8 ns); FW 0x0423
// (2026-10-11) se vraci na 100 MHz (na 125 MHz chyba casove znacky T/2 a T podle faze hrany, L-0152). Multicycle vyjimky
// nize oznacene FW 0x0421 popisuji strukturu RTL (pocet taktu mezi zmenou a pouzitim) a plati na kazdem kmitoctu.
create_clock -name clk_p0_100m   -period 10.0  -waveform {0 5.0}   [get_ports {clk_p0_100m}]

// 🔴 2026-10-04 (#264): SPI PHY je taktovana primo SCK (spi_sck, pin 55).
// Cil 30 MHz (perioda 33,3 ns) = rezerva pro zrychleni SPI (dnes 5 MHz).
// V SCK domene jsou i cesty nabezna -> sestupna hrana (pul periody, 16,7 ns):
// tx_rdata (posedge) -> cur (negedge), tcnt (negedge) -> adresa TX RAM (posedge).
// Prechody do ostatnich domen: dvouportova blokova RAM (TX) a staticke registry
// po CS nahoru (RX vysledek, diagnostika) -> asynchronni skupiny.
create_clock -name spi_sck      -period 33.3  -waveform {0 16.65} [get_ports {spi_sck}]

// FW 0x0503 (E5): predelicka pin_cnt3 je taktovana PRIMO vstupnimi piny. Cil 300 MHz (3,33 ns) = strop vstupniho bufferu
// LVCMOS33 (Gowin 150/300 MHz; vstup citace je pritom DC-200 MHz, ELT23L). Do 2026-10-27 tu bylo 400 MHz (2,5 ns): 4bitovy citac
// u_pcb/c / u_pca/c pak selhaval v 2 z 8 sestaveni az o -1,1 ns, s 3,33 ns v 7 z 8 prosel (median +0,225 ns, min -0,010) a
// jina systematicka cesta nezbyla (pnr_seeds, varianta pin33). Skutecny strop overit generatorem. Pin -> retez TDC zustava
// false_path pres sig_eff; c[3] -> sonda v clk pres 3 FF (asynchronni skupiny).
create_clock -name pin_a -period 3.33 -waveform {0 1.665} [get_ports {ch_a}]
create_clock -name pin_b -period 3.33 -waveform {0 1.665} [get_ports {ch_b}]
set_clock_groups -asynchronous -group [get_clocks {clk_ref_10m}] -group [get_clocks {clk_p0_100m}] -group [get_clocks {spi_sck}] -group [get_clocks {pin_a}] -group [get_clocks {pin_b}]

// 🔴 2026-10-08: NEJISTOTA HODIN. Bez ni STA predpoklada idealni 100 MHz (nulovy jitter, presna strida).
// Zmereno na desce: buildy regresniho bloku s nejtesnejsi rezervou 0,15 / 0,017 / -0,19 ns POCITALY CHYBNE
// (vysledky zavisle na rozmisteni), build se stejnou logikou a rezervou >= 0,58 ns pocital presne
// (docs/TDC_MATEMATIKA.md kap. 8.3). Realna rezerva tedy chybi ~0,3 ns: jitter Si5356 + vstupni buffer hodin.
// Uncertainty 0,5 ns nuti P&R tuto rezervu dodrzet, misto aby se spolehalo na stesti pri rozmisteni.
// 🔴 2026-10-09 (FW 0x041D): 0,5 NESTACI. Build se skutecnou rezervou 0,545 ns (slack 0,045 + 0,5) meril
// STABILNE a deterministicky spatne (1 578 628 Hz misto 10 000 008 Hz, tj. ~1/6 hran), stejny zdroj s 0,9 ns
// (rezerva >= 0,98 ns) meri spravne a 0x041A (rezerva 0,64) taky. Mez lezi mezi 0,55 a 0,64 ns -> 0,9 dava
// rezervu nad ni. Nejtesnejsi cesty jsou citani hran (rise_q -> cy_q/snap) a regr_acc (L-0141).
set_clock_uncertainty -setup -from [get_clocks {clk_p0_100m}] -to [get_clocks {clk_p0_100m}] 0.9

// I/O SPI vuci SCK (rozhoduje o skutecnem stropu SPI, ne vnitrni Fmax):
//  MISO: PHY ho meni sestupnou hranou SCK, STM vzorkuje nabeznou -> cesta
//        SCK pin -> registr -> MISO pin musi stihnout pul periody minus 5 ns
//        (setup STM + spoje, odhad).
//  MOSI: STM ho meni sestupnou hranou, FPGA vzorkuje nabeznou -> vstupni zpozdeni
//        az 5 ns po sestupne hrane (odhad).
set_output_delay -clock spi_sck -max 5.0 [get_ports {spi_miso}]
set_output_delay -clock spi_sck -min 0.0 [get_ports {spi_miso}]
set_input_delay  -clock spi_sck -clock_fall -max 5.0 [get_ports {spi_mosi}]
set_input_delay  -clock spi_sck -clock_fall -min 0.0 [get_ports {spi_mosi}]

// 🔴 cal_mode mux (calm_s -> sig4_eff, false_path) byl v pravodobem top.v
// staveny desky — NOVE top.v (dva symetricke kanaly, hrube citani) uz
// cal_mode/ring_osc vubec nema, takze tenhle false_path odstranen (Gowin
// TA2003: registr calm_s_1_s0 neexistuje -> chyba synteze).

// ============================================================
// TDC (tdc.v). Vstup retezu (net `sig_eff`) je z definice ASYNCHRONNI k clk_p0
// (vstupni pin, ring oscilator, kvazistaticky vyber zdroje) a koncí v tap FF.
// Presne to se meri, ne casova cesta k uzavreni => false path pres tento net.
// Cesta CE (zmrazeni) a vse ostatni zustava casovane.
// ============================================================
set_false_path -through [get_nets {u_tdca/sig_eff}]
// FW 0x0506: cp1 (kontrolni pocitani hran, prvni synchronizacni FF) vzorkuje asynchronni vstup sig_pin; volba zdroje (bs2) a
// predelicky (pr_a/pr_b) se meni jen na hranici segmentu -> cesta pr_* / bs2 -> cp1 neni casovana
set_false_path -to [get_regs {u_sa/cp1* u_sb/cp1*}]
set_false_path -through [get_nets {u_tdcb/sig_eff}]

// Dekoder (od 2026-10-07 architektura qd): vzorky q bezi VOLNE (kratke cesty), kompaktni kopie qd je po
// spusteni ZMRAZENA (clock enable) >= 5 taktu a code_r / hicode_r se berou az na konci (fz == 4) =>
// kombinacni cesta qd -> code_r ma 5 taktu. `qd_*` zahrnuje zmrazene qd_r, qd_f i volbu sady qd_src
// (DUAL; nastavi se pri spusteni a od te doby je staticka). Vsechny maji prefix qd_ a lezi v u_chain,
// takze vzor nachazi VZDY neco (u DUAL=0 aspon qd_r) -- odkaz na neexistujici objekt je v Gowin CHYBA (TA2003).
set_multicycle_path -setup -end 5 -from [get_regs {u_tdca/u_chain/qd_*}] -to [get_regs {u_tdca/code_r*}]
set_multicycle_path -hold  -end 4 -from [get_regs {u_tdca/u_chain/qd_*}] -to [get_regs {u_tdca/code_r*}]
set_multicycle_path -setup -end 5 -from [get_regs {u_tdcb/u_chain/qd_*}] -to [get_regs {u_tdcb/code_r*}]
set_multicycle_path -hold  -end 4 -from [get_regs {u_tdcb/u_chain/qd_*}] -to [get_regs {u_tdcb/code_r*}]
set_multicycle_path -setup -end 5 -from [get_regs {u_tdca/u_chain/qd_*}] -to [get_regs {u_tdca/hicode_r*}]
set_multicycle_path -hold  -end 4 -from [get_regs {u_tdca/u_chain/qd_*}] -to [get_regs {u_tdca/hicode_r*}]
set_multicycle_path -setup -end 5 -from [get_regs {u_tdcb/u_chain/qd_*}] -to [get_regs {u_tdcb/hicode_r*}]
set_multicycle_path -hold  -end 4 -from [get_regs {u_tdcb/u_chain/qd_*}] -to [get_regs {u_tdcb/hicode_r*}]

// FW 0x0427: vypocet kalibracni tabulky TDC (cum, xv, hcur, scan_k, d_*) je v STM -> vyjimky na tyto registry odstraneny (L-0188).
// v3: okna a regrese (win_recip, regr_acc) ve FPGA nejsou -- jejich vyjimky z timing.sdc (v2) sem nepatri.

// FW 0x0420 (2026-10-10): ref_guard bezi z vnitrniho oscilatoru (OSC, nezavisly na vnejsich hodinach). Vsechny prechody mezi nim
// a ostatnimi domenami jsou 2FF synchronizatory kvazistatickych signalu (pozadavek na referenci, stav externi/vybrane/vypadla),
// neni co casovat. Tool pro OSC predpoklada 250 MHz (bez FREQ_DIV) a hlasil by tyto cesty jako porusene.
set_false_path -from [get_regs {u_app/ref_sel*}] -to [get_regs {u_refg/rq_*}]
set_false_path -from [get_regs {u_refg/ext_present*}] -to [get_regs {rp_s_*}]
set_false_path -from [get_regs {u_refg/ext_lost*}] -to [get_regs {rl_s_*}]
set_false_path -from [get_regs {u_refg/sel*}] -to [get_regs {ra_s_*}]
// FW 0x0422: LED FPGA_OK v domene vnitrniho oscilatoru -- synchronizatory stavu z domeny clk_ref_10m (asynchronni prechod)
set_false_path -to [get_regs {t10_s_* good_s_*}]
// FW 0x0422: pocet opakovani kalibrace (quasi-staticky diagnosticky udaj) do domeny 10 MHz
set_false_path -to [get_regs {rt1_*}]
// FW 0x0425: CS SPI je v domene 100 MHz jen uroven pro diagnostiku/maskovani (2 FF synchronizator csa_s), cesta z pinu se nezkouma
set_false_path -from [get_ports {spi_cs_n}] -to [get_regs {csa_s*}]
// FW 0x0425: stav reference (ref_guard.sel, domena vnitrniho oscilatoru) do 100 MHz pro vypinani hlidace koherence -- 2FF synchronizator
// rx_s kvazistatickeho signalu, stejne jako ra_s vyse (P&R 2026-10-20: bez vyjimky -1,73 ns na asynchronnim prechodu)
set_false_path -from [get_regs {u_refg/sel*}] -to [get_regs {rx_s_*}]

// Rozmisteni retezu TDC (2026-10-09): report cest Z KAZDEHO vzorkovaciho FF -> textovy report
// (build.tcl: -gen_text_timing_rpt) nese polohu kazdeho q_r[k] = polohu ALU tapu k.
// Vyhodnocuje sim/check_tdc_placement.py (selze, kdyz retez neni v jednom radku / FF mimo slot ALU).
// FW 0x0502 (E4): jemny cas 1PPS. Hodiny rPLL (FCLK 400 MHz) a CLKDIV (PCLK 100 MHz) odvodi Gowin z clk_p0_100m sam
// [OVERIT v .tr po prvnim P&R; kdyby ne: create_generated_clock na u_ipps/u_pll/CLKOUT (-multiply_by 4) a
// u_ipps/u_div/CLKOUT (-divide_by 4)]. Prechod PCLK -> clk: p_ts drzeno ~1 s, prepinaci bit pres 3 FF; rdy uvolnuje
// reset PCLK pri stojicich hodinach (CLKDIV v resetu) -> neni co casovat. Totez wn (2026-10-24, oprava N1: pocet slov
// PCLK od uvolneni, maska prvnich 3 slov).
set_false_path -from [get_regs {u_ipps/p_ts*}] -to [get_regs {u_ipps/f_ts* u_ipps/hl*}]
set_false_path -from [get_regs {u_ipps/p_tgl*}] -to [get_regs {u_ipps/t_s*}]
set_false_path -from [get_regs {u_ipps/rdy*}] -to [get_regs {u_ipps/pcnt* u_ipps/lastw* u_ipps/p_tgl* u_ipps/wn*}]
// FW 0x0506 (prvni P&R v3): rdy -> RESET IDES8 / CLKDIV (hodiny PLL 400 MHz): rdy se meni jen po zamceni PLL (kvazistaticky), IDES8 je
// tou dobou v resetu a hrana 1PPS prijde nejdriv za sekundu -> zadna cesta z rdy neni casovana (rezerva IDES8 byla -1,66 ns;
// cil get_regs / get_clocks / get_cells na IDES8 a odvozene hodiny Gowin odmita: TA2003 / TA2004)
set_false_path -from [get_regs {u_ipps/rdy_h*}]
// prvni = nejhorsi cesty celeho navrhu (vlastni report_timing jinak vychozi seznam nahradi)
report_timing -setup -max_paths 50 -max_common_paths 1
report_timing -setup -max_paths 300 -max_common_paths 1 -from [get_regs {u_tdca/u_chain/q_r*}]
report_timing -setup -max_paths 300 -max_common_paths 1 -from [get_regs {u_tdcb/u_chain/q_r*}]
// FW 0x0504: soucty v DSP -- ZADNY report_timing na registry souctu ani jejich vstupy (audit 2026-10-23, F-0271):
// GowinSynthesis je s syn_dspstyle = "dsp" vstrci do ALU54D (vystupni / vstupni registry bloku), pak get_regs nic
// nenajde a odkaz na neexistujici objekt je v Gowin CHYBA TA2003 (beh skonci). Poloha DSP: zprava o zdrojich / .rpt
// (radek R19), ne mapa z timing reportu.
// FW 0x0503: predelicky u pinu kanalu -> poloha v mape rozlozeni
report_timing -setup -max_paths 10 -max_common_paths 1 -from [get_regs {u_pca/c* u_pcb/c*}]
// FW 0x0502: cesty logiky PCLK u pinu 1PPS -> poloha v mape rozlozeni (tools/floorplan_svg.py)
report_timing -setup -max_paths 20 -max_common_paths 1 -from [get_regs {u_ipps/pcnt*}]
