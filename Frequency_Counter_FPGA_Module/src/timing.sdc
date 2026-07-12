// ============================================================
// File: timing.sdc
// Timing constraints for Counter_FPGA (GW1NR-9C / Tang Nano 9K)
//
// 5 externich hodin:
//   clk_ref_10m  = 10 MHz  (perioda 100 ns) - aplikace / SPI PHY
//   clk_p0..p7p5 = 100 MHz (perioda 10 ns)  - TDC + citani udalosti
//
// Ctyri faze jsou JEDEN hodinovy system (Si5356, 0/2,5/5/7,5 ns) -
// waveform posuny zajisti, ze STA hlida realne cross-fazove cesty
// s1/s2/s3 -> os_r1 v oversampleru (rozpocty 7,5/5/2,5 ns).
// Asynchronni je jen 10MHz domena (toggle-handshake CDC) a SPI vstupy
// (vzorkovane 3FF synchronizery -> nejsou hodiny).
// ============================================================

create_clock -name clk_ref_10m   -period 100.0 -waveform {0 50.0}   [get_ports {clk_ref_10m}]

create_clock -name clk_p0_100m   -period 10.0  -waveform {0 5.0}    [get_ports {clk_p0_100m}]
create_clock -name clk_p2p5_100m -period 10.0  -waveform {2.5 7.5}  [get_ports {clk_p2p5_100m}]
create_clock -name clk_p5_100m   -period 10.0  -waveform {5.0 10.0} [get_ports {clk_p5_100m}]
create_clock -name clk_p7p5_100m -period 10.0  -waveform {7.5 12.5} [get_ports {clk_p7p5_100m}]

// Parove (viceclockova skupina v jednom -group nebyla Gowin STA respektovana:
// p0 -> ref_10m cesty se timovaly jako related s 10ns vztahem)
set_clock_groups -asynchronous -group [get_clocks {clk_ref_10m}] -group [get_clocks {clk_p0_100m}]
set_clock_groups -asynchronous -group [get_clocks {clk_ref_10m}] -group [get_clocks {clk_p2p5_100m}]
set_clock_groups -asynchronous -group [get_clocks {clk_ref_10m}] -group [get_clocks {clk_p5_100m}]
set_clock_groups -asynchronous -group [get_clocks {clk_ref_10m}] -group [get_clocks {clk_p7p5_100m}]

// cal_mode mux (calm_s -> sig4_eff -> vzorkovace vsech fazi) je kvazistaticky
// ridici signal - prepina se jen povelem, okna kolem prepnuti STM zahodi.
set_false_path -from [get_regs {calm_s_1_s0}]

// phase_check toggly (u_pc/t1..t3 -> q1..q3) jsou CDC-safe (2FF sync +
// edge detect) -> nevynucovat na nich 2,5/5/7,5 ns setup. Post-syntezni
// jmena registru maji suffix _s0. Skutecne TDC cesty (u_os*/s1..s3 ->
// os_r1) zustavaji hlidane waveform posuny.
set_false_path -from [get_regs {u_pc/t1_s0}]
set_false_path -from [get_regs {u_pc/t2_s0}]
set_false_path -from [get_regs {u_pc/t3_s0}]
