<#
  probe_test.ps1 - overi, jestli CTENI LADICI SONDOU zabiji I2C4.

  Podezreni (2026-08-30): `STM32_Programmer_CLI -r32` cil na dobu cteni HALTUJE.
  Kdyz halt padne doprostred I2C4 transakce, ATTINY (bit-bang slave, ktery musi
  sledovat KAZDOU transakci na sbernici vcetne cizich) uvidi prenos, ktery nikdy
  neskonci -> ztrati synchronizaci -> vsichni slave prestanou odpovidat.
  Kdyby to platilo, cast dnesnich "nahodnych" umrti jde na vrub MERENI, ne firmwaru.

  Postup: overi zdravou sbernici pres UART, pak provede N cteni sondou a sleduje,
  jestli se objevi souvisla serie chyb. UART na rozdil od sondy cil nezastavuje.

  Pouziti:
    powershell -ExecutionPolicy Bypass -File tools\probe_test.ps1 -Reads 5
#>
param(
  [string]$Port  = "COM8",
  [int]   $Baud  = 115200,
  [int]   $Reads = 5,        # kolik cteni sondou provest
  [int]   $Settle= 20        # s cekani na ustaleni pred testem
)

$CLI = "C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"

function Ask($sp, $cmd, $idle = 900, $max = 12000) {
  $sp.DiscardInBuffer(); $sp.WriteLine($cmd)
  $sb = New-Object System.Text.StringBuilder
  $t0 = Get-Date; $last = Get-Date
  while ($true) {
    Start-Sleep -Milliseconds 50
    $c = $sp.ReadExisting()
    if ($c.Length -gt 0) { [void]$sb.Append($c); $last = Get-Date }
    if ((New-TimeSpan $last (Get-Date)).TotalMilliseconds -ge $idle) { break }
    if ((New-TimeSpan $t0 (Get-Date)).TotalMilliseconds -ge $max) { break }
  }
  return $sb.ToString()
}
function Streak($sp) {
  $r = Ask $sp "status" 900 12000
  if ($r -match "err (\d+) \(v rade (\d+)\)") { return @([int]$Matches[1], [int]$Matches[2]) }
  return @(-1, -1)
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.NewLine = "`r`n"; $sp.ReadTimeout = 200
try {
  $sp.Open(); Start-Sleep -Milliseconds 200; $sp.DiscardInBuffer()

  Write-Host ("# cekam {0}s na ustaleni..." -f $Settle)
  Start-Sleep -Seconds $Settle
  $b = Streak $sp
  Write-Host ("PRED testem:  err celkem={0}  v rade={1}" -f $b[0], $b[1])
  if ($b[1] -gt 5) { Write-Host "sbernice uz je mrtva -> test nema smysl, resetni desku" -ForegroundColor Yellow; exit 2 }

  for ($i = 1; $i -le $Reads; $i++) {
    # jedno cteni sondou = jeden HALT cile
    & $CLI -q -c port=SWD mode=HOTPLUG -r32 0x24000000 1 | Out-Null
    Start-Sleep -Seconds 3
    $a = Streak $sp
    Write-Host ("  po cteni sondou #{0}:  err celkem={1}  v rade={2}" -f $i, $a[0], $a[1])
    if ($a[1] -gt 20) {
      Write-Host ("!!! SBERNICE UMRELA po {0}. cteni sondou -> HYPOTEZA POTVRZENA" -f $i) -ForegroundColor Red
      exit 0
    }
  }
  Write-Host "# {0} cteni sondou sbernici nezabilo -> hypoteza NEPOTVRZENA" -ForegroundColor Green
} catch {
  Write-Host ("CHYBA: " + $_.Exception.Message) -ForegroundColor Red; exit 1
} finally {
  if ($sp.IsOpen) { $sp.Close() }
}
