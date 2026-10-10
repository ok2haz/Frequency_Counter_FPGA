open_project "C:/GitHub/Frequency_Counter_FPGA/Frequency_Counter_FPGA_Module/Counter_FPGA.gprj"
# 🔴 Bez explicitniho top modulu si Gowin vybral "phase_check" (osirely modul
# ve spi_app.v, ktery top.v uz nevola po prechodu na jednu referencni hodinu) -
# viz docs/audit nalez pri bring-upu nove desky. top.v je JEDINY modul, ktery
# nikdo jiny neinstanciuje, ale auto-detekce to spletla s dalsim osirelym
# modulem. Explicitni nastaveni je bezpecnejsi nez spolehat na auto-detekci.
set_option -top_module top
# Piny 54-57 jsou dedikované SSPI/MSPI konfigurační piny -> povol jako běžné GPIO
set_option -use_sspi_as_gpio 1
set_option -use_mspi_as_gpio 1
# 2026-10-03: MOSI (pin 54, CFG "DIN/CLKHOLD_N") patri do skupiny SSPI (UG290
# tab. 4-2) -> staci use_sspi_as_gpio. Docasne uvolnene vsechny skupiny (cpu/mode/
# ready/done/reconfign/i2c) NEPOMOHLY: pricinou byl vadny vstup pinu 54 na kusu
# Tang Nano (po vymene modulu MOSI funguje). Proto vraceno na minimum.
# Gowin si volby pamatuje v projektu -> vratit VYSLOVNE (jinak zustanou z drivejsiho behu).
set_option -use_cpu_as_gpio 0
set_option -use_mode_as_gpio 0
set_option -use_ready_as_gpio 0
set_option -use_done_as_gpio 0
set_option -use_reconfign_as_gpio 0
set_option -use_i2c_as_gpio 0
# Vyssi usili placeru/routeru: cross-fazove cesty TDC (2,5/5/7,5 ns rozpocty)
set_option -place_option 1
set_option -route_option 1
# Textovy timing report: nese polohu vzorkovacich FF TDC (sim/check_tdc_placement.py)
set_option -gen_text_timing_rpt 1

# 2026-10-10 (FW 0x041F, STATUS #283): IDENTITA BITSTREAMU. FW_VERSION se meni jen rucne, takze dva ruzne
# bitstreamy hlasily totez (0x041D radek vs. unc09) a z desky neslo poznat, ktery bezi. Pri kazdem sestaveni
# se do src/build_id.vh zapise cas (unix s) a git hash zdroju ([31] = neulozene zmeny v src/ nebo build.tcl);
# top.v ho vlozi a ramec ho nese v bajtech 12..19 (caps bit10). Soubor se neverzuje (.gitignore).
# Sestaveni v Gowin IDE tenhle skript nespusti -> bitstream ponese identitu POSLEDNIHO sestaveni skriptem.
set fpga_root "C:/GitHub/Frequency_Counter_FPGA/Frequency_Counter_FPGA_Module"
set bld_time [clock seconds]
set bld_hash 0
set bld_dirty 0
if {[catch {exec -ignorestderr git -C $fpga_root rev-parse --short=7 HEAD} h] == 0} {
    scan [string trim $h] %x bld_hash
}
if {[catch {exec -ignorestderr git -C $fpga_root status --porcelain -- src build.tcl} st] == 0} {
    if {[string length [string trim $st]] > 0} { set bld_dirty 1 }
}
set bld_git [expr {($bld_hash & 0x0FFFFFFF) | ($bld_dirty << 31)}]
set fh [open "$fpga_root/src/build_id.vh" w]
puts $fh "// GENEROVANO build.tcl -- neupravovat, neverzovat. Cas sestaveni a git zdroju (viz top.v)."
puts $fh [format "localparam \[31:0\] BLD_TIME = 32'h%08X;   // %s" $bld_time [clock format $bld_time -format "%Y-%m-%d %H:%M:%S" -gmt 1]]
puts $fh [format "localparam \[31:0\] BLD_GIT  = 32'h%08X;   // git %07x%s" $bld_git $bld_hash [expr {$bld_dirty ? " + neulozene zmeny" : ""}]]
close $fh
puts [format "BUILD_ID: time=0x%08X git=0x%08X" $bld_time $bld_git]

# L-0132: stare vystupy pred behem smazat, at je po selhani nejde omylem nahrat ani vyhodnotit
foreach f {Counter_FPGA.fs Counter_FPGA.tr Counter_FPGA.rpt.txt Counter_FPGA_tr_content.html device.cfg} {
    file delete -force "$fpga_root/impl/pnr/$f"
}
run all
