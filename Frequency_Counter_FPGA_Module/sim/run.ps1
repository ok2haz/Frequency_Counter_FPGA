# ============================================================
# sim/run.ps1 - spouští self-checking testbenche přes Icarus Verilog.
# Pouziti:
#   .\sim\run.ps1            # spustí všechny testy
#   .\sim\run.ps1 coarse     # spustí jen test "coarse"
#
# Vyžaduje iverilog + vvp v PATH (Icarus Verilog).
# ============================================================
param([string]$Only = "")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$src  = Join-Path $root "src"
$sim  = $PSScriptRoot

# Kontrola toolchainu
if (-not (Get-Command iverilog -ErrorAction SilentlyContinue)) {
    Write-Host "CHYBA: iverilog není v PATH. Nainstaluj Icarus Verilog (viz instrukce)." -ForegroundColor Red
    exit 1
}

# Tabulka testů: name -> @{ rtl = @(...); tb = "..."; top = "..." }
$tests = @(
    @{ name = "coarse"; top = "tb_coarse_counter";
       rtl = @("coarse_counter.v"); tb = "tb_coarse_counter.sv" }
    # 2026-10-03: recip_calc zanikl (FPGA uz kmitocet nepocita), nahrazen gate_div
    @{ name = "gatediv"; top = "tb_gate_div";
       rtl = @("spi_app.v"); tb = "tb_gate_div.sv" }
    # PHY: nova (predpocitany posun TX po segmentech) == puvodni modul
    @{ name = "phyequiv"; top = "tb_phy_equiv";
       rtl = @("spi_slave_phy.v"); tb = "tb_phy_equiv.sv"; extra = @("_phy_old.v") }
    # TDC: kalibrace ring oscilatorem + presnost a symetrie 2 kanalu (~2 min).
    # Behavioralni modely primitiv ALU/LUT1/LUT2 (sim/gowin_models.v), SIM_LUT_PS
    # zpomaluje kruh pro rychlost simulace.
    @{ name = "tdc"; top = "tb_tdc";
       rtl = @("tdc.v", "spi_app.v"); tb = "tb_tdc.sv"; extra = @("gowin_models.v");
       defs = @("-DSIM_LUT_PS=800") }
    @{ name = "phase"; top = "tb_phase_oversampler";
       rtl = @("spi_app.v"); tb = "tb_phase_oversampler.sv" }
    # end-to-end SPI: PHY + spi_app (ACK prijat, CAL report na zadost)
    @{ name = "link"; top = "tb_link";
       rtl = @("spi_slave_phy.v", "spi_app.v"); tb = "tb_link.sv" }
    # další testy přidávej sem, jak přibývají moduly
)

$fail = 0
foreach ($t in $tests) {
    if ($Only -ne "" -and $t.name -ne $Only) { continue }

    $vvp = Join-Path $sim ("{0}.vvp" -f $t.name)
    $files = @()
    foreach ($e in $t.extra) { $files += (Join-Path $sim $e) }
    foreach ($r in $t.rtl) { $files += (Join-Path $src $r) }
    $files += (Join-Path $sim $t.tb)

    Write-Host "--- Test '$($t.name)' ---" -ForegroundColor Cyan
    & iverilog -g2012 -s $t.top @($t.defs) -o $vvp @files
    if ($LASTEXITCODE -ne 0) { Write-Host "  COMPILE FAIL" -ForegroundColor Red; $fail++; continue }

    $out = & vvp $vvp
    $out | ForEach-Object { Write-Host "  $_" }
    if ($out -match "PASS") { Write-Host "  -> PASS" -ForegroundColor Green }
    else { Write-Host "  -> FAIL" -ForegroundColor Red; $fail++ }
    Remove-Item $vvp -ErrorAction SilentlyContinue
}

Write-Host ""
if ($fail -eq 0) { Write-Host "VSE PROSLO" -ForegroundColor Green; exit 0 }
else { Write-Host "$fail test(u) selhalo" -ForegroundColor Red; exit 1 }
