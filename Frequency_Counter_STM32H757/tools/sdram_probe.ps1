<#
  sdram_probe.ps1 - PRIME testovani SDRAM ladici sondou, s CPU v HALTU.

  Proc sondou a ne firmwarem: se zastavenym jadrem odpada D-cache, MPU
  atributy i kresleni UI, takze se meri VYHRADNE pamet. Refresh bezi dal
  (FMC je autonomni HW, halt jadra ho nezastavi).

  🔴 CENA: halt cile rozhodi ATTINY na I2C4 -> dotyk a TMP117 0x48 zustanou
  mrtve az do POWER-CYKLU (viz CLAUDE.md). Pouzivej jen kdyz to stoji za to
  a pocitej s odpojenim napajeni po testu.

  Testuje se prave to, co odlisi vadnou BUNKU od vadne ADRESNI LINKY:
  na kazdou sledovanou adresu se zapise JINA hodnota a pak se VSECHNY ctou
  zpatky. Kdyz se dve adresy prekryvaji, druhy zapis prepise prvni.

    powershell -ExecutionPolicy Bypass -File tools\sdram_probe.ps1
#>
param(
  [string]$Cli = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
)

# Adresy vybrane tak, aby kazda testovala JEDEN vyssi adresni/bankovni bit.
# Mapovani pri 13 radkovych / 9 sloupcovych bitech a 16bit sbernici:
#   HADDR[9:1] -> sloupec, HADDR[22:10] -> radek, HADDR[24:23] -> banka
$Targets = @(
  @{ a = "0xC0000000"; v = "0x11110000"; n = "zaklad (radek 0, banka 0)" },
  @{ a = "0xC0080000"; v = "0x22220001"; n = "A9  = PF15 (HADDR19)" },
  @{ a = "0xC0100000"; v = "0x33330002"; n = "A10 = PG0  (HADDR20)" },
  @{ a = "0xC0200000"; v = "0x44440003"; n = "A11 = PG1  (HADDR21)" },
  @{ a = "0xC0400000"; v = "0x55550004"; n = "A12 = PG2  (HADDR22)" },
  @{ a = "0xC0800000"; v = "0x66660005"; n = "BA0 = PG4  (HADDR23)" },
  @{ a = "0xC1000000"; v = "0x77770006"; n = "BA1 = PG5  (HADDR24)" }
)

function Run-Cli([string[]]$cliArgs) {
  & $Cli @cliArgs 2>&1 | Out-String
}

Write-Host "=== 1) zapis ruznych hodnot na sledovane adresy ==="
$wargs = @("-c", "port=SWD", "mode=HOTPLUG")
foreach ($t in $Targets) { $wargs += @("-w32", $t.a, $t.v) }
$out = Run-Cli $wargs
if ($out -match "Error|error|cannot") {
  Write-Host "!! zapis hlasil chybu:"
  ($out -split "`n" | Select-String -Pattern "Error|error|cannot" | Select-Object -First 5) | ForEach-Object { Write-Host "   $_" }
}

Write-Host ""
Write-Host "=== 2) zpetne cteni ==="
$rargs = @("-c", "port=SWD", "mode=HOTPLUG")
foreach ($t in $Targets) { $rargs += @("-r32", $t.a, "0x4") }
$out = Run-Cli $rargs

# Vytahni dvojice adresa: hodnota
$got = @{}
foreach ($line in ($out -split "`n")) {
  if ($line -match '0x([0-9A-Fa-f]{8})\s*:\s*([0-9A-Fa-f]{8})') {
    $got["0x" + $Matches[1].ToUpper()] = "0x" + $Matches[2].ToUpper()
  }
}

Write-Host ("{0,-12} {1,-12} {2,-12} {3,-6} {4}" -f "adresa", "zapsano", "precteno", "stav", "linka")
Write-Host ("-" * 78)
$bad = 0
foreach ($t in $Targets) {
  $key = $t.a.ToUpper()
  $g = if ($got.ContainsKey($key)) { $got[$key] } else { "?" }
  $w = $t.v.ToUpper()
  $okv = ($g -eq $w)
  if (-not $okv) { $bad++ }
  Write-Host ("{0,-12} {1,-12} {2,-12} {3,-6} {4}" -f $t.a, $w, $g, $(if ($okv) { "OK" } else { "CHYBA" }), $t.n)
}

Write-Host ""
Write-Host "=== 3) retence: cteni znovu po ~2 s BEZ zapisu ==="
Start-Sleep -Seconds 2
$out2 = Run-Cli $rargs
$got2 = @{}
foreach ($line in ($out2 -split "`n")) {
  if ($line -match '0x([0-9A-Fa-f]{8})\s*:\s*([0-9A-Fa-f]{8})') {
    $got2["0x" + $Matches[1].ToUpper()] = "0x" + $Matches[2].ToUpper()
  }
}
foreach ($t in $Targets) {
  $key = $t.a.ToUpper()
  $g1 = if ($got.ContainsKey($key))  { $got[$key] }  else { "?" }
  $g2 = if ($got2.ContainsKey($key)) { $got2[$key] } else { "?" }
  $st = if ($g1 -eq $g2) { "drzi" } else { "ZMENILO SE" }
  Write-Host ("{0,-12} {1,-12} -> {2,-12} {3}" -f $t.a, $g1, $g2, $st)
}

Write-Host ""
if ($bad -eq 0) { Write-Host "vsechny sledovane adresy drzi svou hodnotu" }
else { Write-Host ("CHYBNYCH ADRES: {0} z {1}" -f $bad, $Targets.Count) }
Write-Host "!! I2C4 je po haltu pravdepodobne mrtva -> po testu POWER-CYKLUS."
