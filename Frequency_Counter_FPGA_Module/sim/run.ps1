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
    # TDC: kalibrace ring oscilatorem + presnost a symetrie 2 kanalu (~2 min).
    # Behavioralni modely primitiv ALU/LUT1/LUT2 (sim/gowin_models.v), SIM_LUT_PS
    # zpomaluje kruh pro rychlost simulace.
    @{ name = "tdc"; top = "tb_tdc";
       rtl = @("tdc.v", "spi_app.v"); tb = "tb_tdc.sv"; extra = @("gowin_models.v");
       defs = @("-DSIM_LUT_PS=800") }
    # 2026-10-07: TDC se SILICON modelem (zlom retezu na 268. ALU, ~39 ps/ALU) -- chyba casove znacky KAZDE hrany
    # podle faze (INL, skoky), histogram kalibrace. Prahy: sigma znacky < 40 ps, skoky < 1 %. Zdravy stav ~18 ps / 0 %.
    # (Stara architektura s `t0` dava na stejnem modelu obri bin 1,3 ns a sigma ~150 ps.) ~2-3 min.
    @{ name = "tdcinl"; top = "tb_tdc_inl";
       rtl = @("tdc.v", "spi_app.v"); tb = "tb_tdc_inl.sv"; extra = @("gowin_models.v");
       defs = @("-DSIM_LUT_PS=800", "-DSIM_IDX", "-DSIM_TAP_MEAN=39", "-DSIM_TAP_SPREAD=15", "-DSIM_BREAK_IDX=268",
                "-DCAL_LOG2_TB=14", "-DNEDGES=600", "-DPTAPS=320", "-DPSTRIDE=1", "-DPKD=1", "-DPDUAL=0",
                "-DEXPECT_SIGMA_PS=40", "-DEXPECT_MAXJUMP=1") }
    # totez s DUAL=1 (vzorky i na sestupnou hranu hodin); na desce se nevejde (CLS 87 %), ale musi fungovat
    @{ name = "tdcinl_dual"; top = "tb_tdc_inl";
       rtl = @("tdc.v", "spi_app.v"); tb = "tb_tdc_inl.sv"; extra = @("gowin_models.v");
       defs = @("-DSIM_LUT_PS=800", "-DSIM_IDX", "-DSIM_TAP_MEAN=39", "-DSIM_TAP_SPREAD=15", "-DSIM_BREAK_IDX=268",
                "-DCAL_LOG2_TB=14", "-DNEDGES=600", "-DPTAPS=256", "-DPSTRIDE=2", "-DPKD=2", "-DPDUAL=1",
                "-DEXPECT_SIGMA_PS=40", "-DEXPECT_MAXJUMP=1") }
    # 2026-10-07 (FW 0x0411): KONCOVY test regresniho bloku -- TDC (model krzemiku) + win_recip(REGR=1) na signalu
    # s "prohazujici" fazi; porovna chybu kmitoctu dvou bodu a regrese proti znamene pravde. Zisk musi byt >= 1,8x
    # (nameren 4,7x pri ~30 znackach v segmentu). ~3-4 min.
    @{ name = "regre2e"; top = "tb_regr_e2e";
       rtl = @("tdc.v", "spi_app.v"); tb = "tb_regr_e2e.sv"; extra = @("gowin_models.v");
       defs = @("-DSIM_LUT_PS=800", "-DSIM_IDX", "-DSIM_TAP_MEAN=39", "-DSIM_TAP_SPREAD=15", "-DSIM_BREAK_IDX=268") }
    # akumulator segmentu regr_acc == referencni vypocet (40 nahodnych segmentu, neekvidistantni indexy)
    @{ name = "regracc"; top = "tb_regr_acc"; rtl = @("spi_app.v"); tb = "tb_regr_acc.sv" }
    # dekoder teplomeru == puvodni funkce (bubliny, nasyceni), 3 velikosti
    @{ name = "decequiv256"; top = "tb_dec_equiv"; rtl = @("tdc.v"); tb = "tb_dec_equiv.sv"; extra = @("gowin_models.v"); defs = @("-DTB_TAPS=256") }
    @{ name = "decequiv320"; top = "tb_dec_equiv"; rtl = @("tdc.v"); tb = "tb_dec_equiv.sv"; extra = @("gowin_models.v"); defs = @("-DTB_TAPS=320") }
    # multiplexer TX bajtu (spi_app) == puvodni (tx_old_ref.v = spi_app pred prepisem 2026-10-07)
    @{ name = "txequiv"; top = "tb_tx_equiv"; rtl = @("spi_app.v"); tb = "tb_tx_equiv.sv"; extra = @("tx_old_ref.v") }
    @{ name = "phase"; top = "tb_phase_oversampler";
       rtl = @("spi_app.v"); tb = "tb_phase_oversampler.sv" }
    # end-to-end SPI: PHY (SCK domena + blokova RAM) + spi_app: ACK, CAL report,
    # vypis histogramu, zatez 120 ramcu pri SCK 5/20/31 MHz s novymi merenimi
    # v nahodnych okamzicich (konzistence snimku, CRC). Nahrazuje tb_phy_equiv
    # (porovnani se starou PHY, ktera po #264 neexistuje).
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
