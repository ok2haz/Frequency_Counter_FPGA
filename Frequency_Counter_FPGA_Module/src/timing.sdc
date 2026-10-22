// ============================================================
// File: timing.sdc
// Timing constraints for Counter_FPGA (GW1NR-9C / Tang Nano 9K)
// Nová deska — dva symetrické kanály, JEDNA 100 MHz referenční hodina.
//
// 🔴 ZMĚNA PROTI STARÉ DESCE: clk_p2p5/p5/p7p5_100m (4fázový vernier ze
// Si5356) KONČÍ — nová deska dává jen JEDEN REF_100MHz. Cross-fázové
// cesty (dřívější s1/s2/s3 -> os_r1 v phase_oversameru, rozpočty
// 7,5/5/2,5 ns) tím padají — coarse_edge_detect je JEDNOHODINOVÝ modul
// (jen clk_p0_100m), žádné waveform posuny navíc nepotřebuje.
//
// 2 externí hodiny:
//   clk_ref_10m  = 10 MHz  (perioda 100 ns) - aplikace / SPI PHY / gate-window
//   clk_p0_100m  = 100 MHz (perioda 10 ns)  - hrubé čítání hran (10 ns/LSB)
//
// Asynchronní jsou: CH_A/CH_B vstupy (2FF synchronizer v coarse_edge_detect,
// stejný vzor jako dřívější oversampler), SPI vstupy (3FF sync v PHY) a
// CDC mezi clk_p0_100m <-> clk_ref_10m (toggle-handshake, viz top.v).
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

set_clock_groups -asynchronous -group [get_clocks {clk_ref_10m}] -group [get_clocks {clk_p0_100m}] -group [get_clocks {spi_sck}]

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
// FW 0x0426: delic regr_acc ODSTRANEN (deli STM, L-0187) -- vyjimky rem/nq/dsub a fin_q -> ym zrusene.

// FW 0x0421: casova znacka ts_ps (tdc_chan) se DRZI do dalsi platne znacky (>= 5 taktu) a regr_acc z ni odecita az v 2. taktu po
// samp_v (v0 -> dt_r/dx_r); t0/x0 se meni jen pri PRVNI znacce segmentu, jejiz rozdil se zahazuje (f1).
set_multicycle_path -setup -end 2 -from [get_regs {u_tdca/ts_ps* u_wra/sg_x* u_wra/rg.u_rg/t0_* u_wra/rg.u_rg/x0_*}] -to [get_regs {u_wra/rg.u_rg/dt_r_* u_wra/rg.u_rg/dx_r_*}]
set_multicycle_path -hold  -end 1 -from [get_regs {u_tdca/ts_ps* u_wra/sg_x* u_wra/rg.u_rg/t0_* u_wra/rg.u_rg/x0_*}] -to [get_regs {u_wra/rg.u_rg/dt_r_* u_wra/rg.u_rg/dx_r_*}]



// FW 0x0421: okno -- r_dt = ts_ps - ref_ts se zachyti o takt po ts_valid (tsc_q); ts_ps se drzi a ref_ts se meni jen pri zachyceni.
set_multicycle_path -setup -end 2 -from [get_regs {u_tdca/ts_ps* u_wra/ref_ts*}] -to [get_regs {u_wra/r_dt*}]
set_multicycle_path -hold  -end 1 -from [get_regs {u_tdca/ts_ps* u_wra/ref_ts*}] -to [get_regs {u_wra/r_dt*}]
set_multicycle_path -setup -end 2 -from [get_regs {u_tdcb/ts_ps* u_wrb/ref_ts*}] -to [get_regs {u_wrb/r_dt*}]
set_multicycle_path -hold  -end 1 -from [get_regs {u_tdcb/ts_ps* u_wrb/ref_ts*}] -to [get_regs {u_wrb/r_dt*}]


// FW 0x0426: vystup xm = x0*4096 + xq uz ve FPGA neni (deli STM) -- vyjimka x0/xq -> xm zrusena.

// FW 0x0420 (2026-10-10): ref_guard bezi z vnitrniho oscilatoru (OSC, nezavisly na vnejsich hodinach). Vsechny prechody mezi nim
// a ostatnimi domenami jsou 2FF synchronizatory kvazistatickych signalu (pozadavek na referenci, stav externi/vybrane/vypadla),
// neni co casovat. Tool pro OSC predpoklada 250 MHz (bez FREQ_DIV) a hlasil by tyto cesty jako porusene.
set_false_path -from [get_regs {u_app/ref_sel_r*}] -to [get_regs {u_refg/rq_*}]
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
// prvni = nejhorsi cesty celeho navrhu (vlastni report_timing jinak vychozi seznam nahradi)
report_timing -setup -max_paths 50 -max_common_paths 1
report_timing -setup -max_paths 300 -max_common_paths 1 -from [get_regs {u_tdca/u_chain/q_r*}]
report_timing -setup -max_paths 300 -max_common_paths 1 -from [get_regs {u_tdcb/u_chain/q_r*}]
