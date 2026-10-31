#!/usr/bin/env bash
# run_one.sh <tb_top> <tb_soubor.sv> <src_adresar> <vystup_prefix> [-Ddefinice ...]
# Jeden testbench CELEHO top.v (jako run.ps1 "topselfchk") proti zvolenemu adresari RTL -- napr. src (FW nova) nebo
# src_fw0424_backup (negativni kontrola). Vystupy do vlastnich souboru, takze nekoliduje s run.ps1.
set -u
export PATH="/c/iverilog/bin:$PATH"
cd "$(dirname "$0")"
top="$1"; tb="$2"; src="../$3"; out="$4"; shift 4
iverilog -g2012 -I . -I ../src -s "$top" -DSIM_TOP -DSIM_TOP_REGR -DSIM_LUT_PS=800 -DSIM_IDX -DSIM_TAP_MEAN=39 -DSIM_TAP_SPREAD=15 \
  -DSIM_BREAK_IDX=268 "$@" -o "$out.vvp" \
  "$src/top.v" "$src/spi_app.v" "$src/tdc.v" ../src/spi_slave_phy.v "$tb" gowin_models.v > "$out.log" 2>&1 || { echo "PREKLAD SELHAL"; cat "$out.log"; exit 2; }
vvp -n "$out.vvp" >> "$out.log" 2>&1
grep -a -E "PASS:|FAIL:" "$out.log" | tail -1
