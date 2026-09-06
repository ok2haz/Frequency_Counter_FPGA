<#
  attiny_stress.ps1 - zatezovy test ZAPISU NA ATTINY (0x45) pres I2C4.

  Proc: uzivatel hlasi, ze dotyk umira PO AKTIVACI SPORICE. Spustenim sporice
  se provede prave jeden zapis jasu na ATTINY (auto-dim). Cekat 10 min na kazdy
  pokus je nepouzitelne — `scpi DISP:BRIG <n>` meni `g_brightness`, takze
  vyvola UPLNE STEJNY zapis (UiTask -> ws_panel_set_backlight) na pozadani.

  Podezreni na mechanismus: `HAL_I2C_Master_Transmit` je pollovaci a UiTask ma
  prioritu BelowNormal -> vyssi tasky ho muzou preemptnout MEZI BAJTY, kdy
  periferie DRZI SCL V NULE. Bit-bang slave (ATTINY) to nemusi prezit — stejny
  efekt jako halt ladici sondou (zmereno: 1 cteni sondou = 6 chyb).

  Merí se pocet chyb I2C4 pred/po serii zapisu. Cteni stavu jde pres UART
  (`status`), takze mereni samo na sbernici nesaha.

  Pouziti:
    powershell -ExecutionPolicy Bypass -File tools\attiny_stress.ps1 -Writes 40
#>
param(
  [string]$Port   = "COM8",
  [int]   $Baud   = 115200,
  [int]   $Writes = 40,     # kolik zapisu jasu vyvolat
  [int]   $GapMs  = 250     # rozestup mezi zapisy
)

function Ask($sp, $cmd, $idle = 700, $max = 12000) {
  $sp.DiscardInBuffer(); $sp.WriteLine($cmd)
  $sb = New-Object System.Text.StringBuilder
  $t0 = Get-Date; $last = Get-Date
  while ($true) {
    Start-Sleep -Milliseconds 40
    $c = $sp.ReadExisting()
    if ($c.Length -gt 0) { [void]$sb.Append($c); $last = Get-Date }
    if ((New-TimeSpan $last (Get-Date)).TotalMilliseconds -ge $idle) { break }
    if ((New-TimeSpan $t0 (Get-Date)).TotalMilliseconds -ge $max) { break }
  }
  return $sb.ToString()
}
function Err($sp) {
  $r = Ask $sp "status" 900 12000
  $e = -1; $st = -1; $w = -1
  if ($r -match "err (\d+) \(v rade (\d+)\)") { $e = [int]$Matches[1]; $st = [int]$Matches[2] }
  if ($r -match "zapisu jasu: (\d+) ok")         { $w = [int]$Matches[1] }
  return @($e, $st, $w)
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.NewLine = "`r`n"; $sp.ReadTimeout = 200
try {
  $sp.Open(); Start-Sleep -Milliseconds 200; $sp.DiscardInBuffer()
  $b = Err $sp
  Write-Host ("PRED:  err celkem={0}  v rade={1}  zapisu na ATTINY={2}" -f $b[0], $b[1], $b[2])
  if ($b[1] -gt 5) { Write-Host "sbernice uz je mrtva -> resetni desku" -ForegroundColor Yellow; exit 2 }

  # jas se stridave meni, aby se KAZDY prikaz projevil zapisem (stejna hodnota = zadny zapis)
  for ($i = 1; $i -le $Writes; $i++) {
    $v = if ($i % 2 -eq 0) { 60 } else { 80 }
    [void](Ask $sp ("scpi DISP:BRIG {0}" -f $v) 250 4000)
    Start-Sleep -Milliseconds $GapMs
    if ($i % 10 -eq 0) {
      $a = Err $sp
      Write-Host ("  po {0,3} pokusech:  err celkem={1}  v rade={2}  SKUTECNYCH zapisu={3}" -f $i, $a[0], $a[1], $a[2])
      if ($a[1] -gt 20) {
        Write-Host ("!!! SBERNICE UMRELA po {0} zapisech na ATTINY" -f $i) -ForegroundColor Red
        exit 0
      }
    }
  }
  $a = Err $sp
  Write-Host ("PO {0} pokusech:  err celkem={1}  v rade={2}  SKUTECNYCH zapisu={3}" -f $Writes, $a[0], $a[1], $a[2])
  if ($a[1] -le 5) { Write-Host "# zapisy na ATTINY sbernici NEZABILY" -ForegroundColor Green }
} catch {
  Write-Host ("CHYBA: " + $_.Exception.Message) -ForegroundColor Red; exit 1
} finally {
  if ($sp.IsOpen) { $sp.Close() }
}
