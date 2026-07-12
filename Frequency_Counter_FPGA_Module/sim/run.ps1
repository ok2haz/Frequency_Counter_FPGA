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
    @{ name = "recip"; top = "tb_recip_calc";
       rtl = @("spi_app.v"); tb = "tb_recip_calc.sv" }
    @{ name = "phase"; top = "tb_phase_oversampler";
       rtl = @("spi_app.v"); tb = "tb_phase_oversampler.sv" }
    # další testy přidávej sem, jak přibývají moduly
)

$fail = 0
foreach ($t in $tests) {
    if ($Only -ne "" -and $t.name -ne $Only) { continue }

    $vvp = Join-Path $sim ("{0}.vvp" -f $t.name)
    $files = @()
    foreach ($r in $t.rtl) { $files += (Join-Path $src $r) }
    $files += (Join-Path $sim $t.tb)

    Write-Host "--- Test '$($t.name)' ---" -ForegroundColor Cyan
    & iverilog -g2012 -Wall -s $t.top -o $vvp @files
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
