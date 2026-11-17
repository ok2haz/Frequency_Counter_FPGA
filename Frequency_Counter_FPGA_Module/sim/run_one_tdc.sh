#!/usr/bin/env bash
# run_one_tdc.sh <tb_top> <tb_soubor.sv> <src_adresar> <vystup_prefix> [-Ddefinice ...]
# Testbench SAMOTNEHO tdc.v (napr. tb_n1) proti zvolenemu adresari RTL (src = nova FW, src_fw0424_backup = negativni kontrola).
set -u
export PATH="/c/iverilog/bin:$PATH"
cd "$(dirname "$0")"
top="$1"; tb="$2"; src="../$3"; out="$4"; shift 4
iverilog -g2012 -I . -I ../src -s "$top" -DSIM_IDX -DSIM_TAP_MEAN=39 -DSIM_TAP_SPREAD=15 -DSIM_BREAK_IDX=268 "$@" -o "$out.vvp" \
  "$src/tdc.v" "$tb" gowin_models.v > "$out.log" 2>&1 || { echo "PREKLAD SELHAL"; cat "$out.log"; exit 2; }
vvp -n "$out.vvp" >> "$out.log" 2>&1
grep -a -E "PASS:|FAIL:" "$out.log" | tail -1
