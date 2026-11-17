<#
  cdc_probe.ps1 - diagnostika konzole (USB CDC): posle prikaz a loguje KDY (ms od odeslani) dorazily bajty.
  Odpovida na: odpovida konzole vubec, s jakym zpozdenim, a zalezi to na DTR/RTS? Vystup do souboru -Out (ASCII).
  Pouziti: powershell -ExecutionPolicy Bypass -File tools\cdc_probe.ps1 -Port COM10 -Cmd ping -Dtr 1 -Rts 1 -Secs 12 -Out x.txt
  ASCII only (PowerShell 5.1 cte skript bez BOM jako ANSI).
#>
param(
  [string]$Port = "COM10",
  [string]$Cmd = "ping",
  [int]$Dtr = 0,
  [int]$Rts = 0,
  [int]$Secs = 12,
  [int]$Repeat = 1,
  [string]$Out = ""
)
function Log($s) { if ($Out) { Add-Content -Path $Out -Encoding ASCII -Value $s } ; Write-Host $s }
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.ReadTimeout = 50
$sp.DtrEnable = [bool]$Dtr
$sp.RtsEnable = [bool]$Rts
try { $sp.Open() } catch { Log ("OPEN CHYBA: " + $_.Exception.Message); exit 1 }
Log ("# " + (Get-Date -Format s) + " port=" + $Port + " cmd='" + $Cmd + "' dtr=" + $Dtr + " rts=" + $Rts)
for ($r = 1; $r -le $Repeat; $r++) {
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  $sp.WriteLine($Cmd)
  $total = 0
  $first = -1
  $lastMs = 0
  $buf = ""
  while ($sw.ElapsedMilliseconds -lt ($Secs * 1000)) {
    Start-Sleep -Milliseconds 20
    $n = $sp.BytesToRead
    if ($n -gt 0) {
      $s = $sp.ReadExisting()
      $total += $s.Length
      if ($first -lt 0) { $first = $sw.ElapsedMilliseconds }
      $lastMs = $sw.ElapsedMilliseconds
      $buf += $s
      $t = ($s -replace "[\r\n]+", "|")
      if ($t.Length -gt 70) { $t = $t.Substring(0, 70) + "..." }
      Log ("  +" + $sw.ElapsedMilliseconds + " ms: " + $s.Length + " B  [" + $t + "]")
    }
  }
  Log ("# beh " + $r + ": celkem " + $total + " B, prvni bajt po " + $first + " ms, posledni po " + $lastMs + " ms")
}
$sp.Close()
