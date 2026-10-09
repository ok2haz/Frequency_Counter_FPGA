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
set_clock_uncertainty -setup -from [get_clocks {clk_p0_100m}] -to [get_clocks {clk_p0_100m}] 0.5

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

// Diagnostika kalibrace (tdc.v, faze ph[5] pruchodu tabulkou): scan_k se meni
// jen jednou za 10 taktu (ph[9]) a hcur v ph[1] -> do zapisu d_* v ph[5] maji
// >= 4 takty. Bez tohoto omezeni byla tahle diagnostika nejtesnejsi cestou
// celeho navrhu (rezerva 0,06 ns) a ubirala misto skutecnym cestam.
set_multicycle_path -setup -end 4 -from [get_regs {u_tdca/scan_k*}] -to [get_regs {u_tdca/d_*}]
set_multicycle_path -hold  -end 3 -from [get_regs {u_tdca/scan_k*}] -to [get_regs {u_tdca/d_*}]
set_multicycle_path -setup -end 4 -from [get_regs {u_tdca/hcur*}] -to [get_regs {u_tdca/d_*}]
set_multicycle_path -hold  -end 3 -from [get_regs {u_tdca/hcur*}] -to [get_regs {u_tdca/d_*}]
set_multicycle_path -setup -end 4 -from [get_regs {u_tdcb/scan_k*}] -to [get_regs {u_tdcb/d_*}]
set_multicycle_path -hold  -end 3 -from [get_regs {u_tdcb/scan_k*}] -to [get_regs {u_tdcb/d_*}]
set_multicycle_path -setup -end 4 -from [get_regs {u_tdcb/hcur*}] -to [get_regs {u_tdcb/d_*}]
set_multicycle_path -hold  -end 3 -from [get_regs {u_tdcb/hcur*}] -to [get_regs {u_tdcb/d_*}]
