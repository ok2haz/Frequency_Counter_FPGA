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

set_clock_groups -asynchronous -group [get_clocks {clk_ref_10m}] -group [get_clocks {clk_p0_100m}]

// 🔴 cal_mode mux (calm_s -> sig4_eff, false_path) byl v pravodobem top.v
// staveny desky — NOVE top.v (dva symetricke kanaly, hrube citani) uz
// cal_mode/ring_osc vubec nema, takze tenhle false_path odstranen (Gowin
// TA2003: registr calm_s_1_s0 neexistuje -> chyba synteze).
