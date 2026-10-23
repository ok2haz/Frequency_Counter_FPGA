# pnr_sweep.tcl -- sweep voleb P&R bez zmeny RTL (audit rozlozeni 2026-10-23; O3 z docs/audit/2026-10-22_optimalizace-bitstreamu.md).
# Jedna synteza, pak P&R pro kazdou kombinaci place_option x route_option x replicate_resources. Z kazdeho behu se schova
# jen textovy timing report a report vyuziti (.tr, .rpt.txt) do impl/sweep/<p_r_x>/; bitstream NE. Na konci se volby vrati
# na hodnoty z build.tcl / build_v3.tcl (Gowin si je pamatuje v projektu) a bitstream posledniho behu se smaze -- k nahrani
# na desku se sestavuje VZDY build.tcl / build_v3.tcl (pres tools/fpga_release.py), nikdy vystup sweepu.
# Spusteni (Windows, cmd):
#   set SWEEP=v3                (v2 = Counter_FPGA.gprj / top, v3 = v3/Counter_FPGA_v3.gprj / top_v3; vychozi v3)
#   gw_sh C:/GitHub/Frequency_Counter_FPGA/Frequency_Counter_FPGA_Module/tools/pnr_sweep.tcl
#   python tools/pnr_sweep_report.py v3/impl/sweep          (resp. impl/sweep; tabulka + mapa nejlepsiho behu)
# Doba: ~30 s na beh (P&R 0x0427: 27 s), vychozi mrizka 4 x 3 x 2 = 24 behu.
# Volba, kterou tahle verze Gowinu nezna (napr. replicate_resources), se zapise do sweep.log a kombinace se preskoci.
# NEOVERENO: napsano na macOS bez Gowinu; prvni beh zkontrolovat v sweep.log.

set root "C:/GitHub/Frequency_Counter_FPGA/Frequency_Counter_FPGA_Module"
set which v3
if {[info exists ::env(SWEEP)]} { set which $::env(SWEEP) }
if {$which eq "v2"} {
    set gprj "$root/Counter_FPGA.gprj"; set top top; set impl "$root/impl"
} elseif {$which eq "v3"} {
    set gprj "$root/v3/Counter_FPGA_v3.gprj"; set top top_v3; set impl "$root/v3/impl"
} else {
    puts "SWEEP musi byt v2 nebo v3 (je '$which')"; exit 1
}
set pnr "$impl/pnr"
set out "$impl/sweep"
# mrizka (build.tcl / build_v3.tcl: place 2, route 1; FW 0x0421 merilo place 1 a 2, route 2 bez zlepseni)
set places {0 1 2 3}
set routes {0 1 2}
set repls  {0 1}

open_project $gprj
set_option -top_module $top
# piny a konfigurace jako build.tcl (Gowin si volby pamatuje -> vyslovne)
set_option -use_sspi_as_gpio 1
set_option -use_mspi_as_gpio 1
set_option -use_cpu_as_gpio 0
set_option -use_mode_as_gpio 0
set_option -use_ready_as_gpio 0
set_option -use_done_as_gpio 0
set_option -use_reconfign_as_gpio 0
set_option -use_i2c_as_gpio 0
set_option -gen_text_timing_rpt 1

# identita (top.v / top_v3.v vkladaji src/build_id.vh): stejne jako build.tcl, s priznakem neulozenych zmen
set bld_time [clock seconds]
set bld_hash 0
set bld_dirty 0
if {[catch {exec -ignorestderr git -C $root rev-parse --short=7 HEAD} h] == 0} { scan [string trim $h] %x bld_hash }
if {[catch {exec -ignorestderr git -C $root status --porcelain -- src build.tcl v3} st] == 0} {
    if {[string length [string trim $st]] > 0} { set bld_dirty 1 }
}
set bld_git [expr {($bld_hash & 0x0FFFFFFF) | ($bld_dirty << 31)}]
set fh [open "$root/src/build_id.vh" w]
puts $fh "// GENEROVANO build.tcl -- neupravovat, neverzovat. Cas sestaveni a git zdroju (viz top.v)."
puts $fh [format "localparam \[31:0\] BLD_TIME = 32'h%08X;   // %s (pnr_sweep.tcl)" $bld_time [clock format $bld_time -format "%Y-%m-%d %H:%M:%S" -gmt 1]]
puts $fh [format "localparam \[31:0\] BLD_GIT  = 32'h%08X;   // git %07x%s" $bld_git $bld_hash [expr {$bld_dirty ? " + neulozene zmeny" : ""}]]
close $fh

file mkdir $out
set log [open "$out/sweep.log" w]
proc say {msg} { global log; puts $msg; puts $log $msg; flush $log }
say [format "sweep %s: %s, top %s, git %07x%s, %s" $which $gprj $top $bld_hash [expr {$bld_dirty ? "+zmeny" : ""}] [clock format [clock seconds] -format "%Y-%m-%d %H:%M:%S"]]

# replicate_resources (zdvojeni registru s velkym fan-out, napr. u_tdca/lut_q3[15] -> 34 cilu): kdyz ji Gowin nezna, jen x0
if {[catch {set_option -replicate_resources 0} err]} { say "replicate_resources nepodporovana ($err) -> jen x0"; set repls {0}; set has_x 0 } else { set has_x 1 }

if {[catch {run syn} err]} { say "SYNTEZA SELHALA: $err"; close $log; exit 1 }

foreach x $repls {
    foreach p $places {
        foreach r $routes {
            set tag "p${p}_r${r}_x${x}"
            foreach f {Counter_FPGA.fs Counter_FPGA.tr Counter_FPGA.rpt.txt Counter_FPGA_tr_content.html} { file delete -force "$pnr/$f" }
            if {[catch {
                set_option -place_option $p
                set_option -route_option $r
                if {$has_x} { set_option -replicate_resources $x }
            } err]} { say "$tag: volba odmitnuta ($err) -> preskoceno"; continue }
            set t0 [clock seconds]
            set rc [catch {run pnr} err]
            set dt [expr {[clock seconds] - $t0}]
            file mkdir "$out/$tag"
            foreach f {Counter_FPGA.tr Counter_FPGA.rpt.txt} {
                if {[file exists "$pnr/$f"]} { file copy -force "$pnr/$f" "$out/$tag/$f" }
            }
            set fo [open "$out/$tag/sweep.txt" w]
            puts $fo "place_option $p route_option $r replicate_resources $x pnr_s $dt rc $rc"
            close $fo
            if {$rc} { say "$tag: P&R selhal za $dt s ($err)" } else { say "$tag: hotovo za $dt s" }
        }
    }
}

# volby zpet jako build.tcl / build_v3.tcl; bitstream sweepu pryc (L-0132: nic, co by slo omylem nahrat)
set_option -place_option 2
set_option -route_option 1
if {$has_x} { set_option -replicate_resources 0 }
file delete -force "$pnr/Counter_FPGA.fs"
say "konec; vyhodnoceni: python tools/pnr_sweep_report.py [string range $out [expr {[string length $root] + 1}] end]"
close $log
