<#
  uart.ps1 - posle jeden prikaz na UART desku a vypise odpoved.
  Pouziti:
    powershell -ExecutionPolicy Bypass -File .\tools\uart.ps1 "screen main"
    powershell -ExecutionPolicy Bypass -File .\tools\uart.ps1 ping
    powershell -ExecutionPolicy Bypass -File .\tools\uart.ps1 "clear" COM11 115200
  Pozn.: port nesmi byt soucasne otevreny v PuTTY/Tera Term (exkluzivni pristup).
#>
param(
  [string]$cmd  = "ping",
  [string]$port = "COM11",
  [int]   $baud = 115200
)
$sp = New-Object System.IO.Ports.SerialPort $port, $baud, 'None', 8, 'One'
$sp.NewLine     = "`r`n"     # prikazy zakoncene CRLF
$sp.ReadTimeout = 2000
try {
  $sp.Open()
  Start-Sleep -Milliseconds 100
  $sp.DiscardInBuffer()
  Write-Host ("> {0}" -f $cmd) -ForegroundColor Cyan
  $sp.WriteLine($cmd)
  Start-Sleep -Milliseconds 500
  $resp = $sp.ReadExisting()
  if ([string]::IsNullOrWhiteSpace($resp)) { Write-Host "(zadna odpoved - spravne firmware/port/baud?)" -ForegroundColor Yellow }
  else { Write-Host $resp.Trim() -ForegroundColor Green }
} catch {
  Write-Host ("CHYBA: " + $_.Exception.Message) -ForegroundColor Red
} finally {
  if ($sp.IsOpen) { $sp.Close() }
}
