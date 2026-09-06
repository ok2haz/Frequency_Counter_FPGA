<#
  uartlog.ps1 - PASIVNE poslouchá UART a kazdy radek orazitkuje casem od startu
  poslechu. Urceno pro zachyceni BOOT logu (spust, pak resetni desku sondou)
  nebo pro sledovani, KDY se objevi chybova hlaska.

  Volitelne umi behem poslechu periodicky posilat prikaz (-Poll) — tim se da
  vzorkovat stav, ktery se sam netiskne (napr. `temperature` -> kdy zestale).

  Pouziti:
    powershell -ExecutionPolicy Bypass -File tools\uartlog.ps1 -Port COM8 -Seconds 20
    powershell -ExecutionPolicy Bypass -File tools\uartlog.ps1 -Port COM8 -Seconds 20 -Poll "temperature" -PollMs 1000
#>
param(
  [string]$Port    = "COM8",
  [int]   $Baud    = 115200,
  [int]   $Seconds = 20,
  [string]$Poll    = "",
  [int]   $PollMs  = 1000
)
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.NewLine = "`r`n"
$sp.ReadTimeout = 100
try {
  $sp.Open()
  $sp.DiscardInBuffer()
  $t0 = Get-Date
  $lastPoll = $t0
  $buf = ""
  Write-Host ("# poslouchám {0} po dobu {1}s (cas = s od startu poslechu)" -f $Port, $Seconds)
  while ((New-TimeSpan $t0 (Get-Date)).TotalSeconds -lt $Seconds) {
    Start-Sleep -Milliseconds 40
    $buf += $sp.ReadExisting()
    while ($buf.Contains("`n")) {
      $i = $buf.IndexOf("`n")
      $line = $buf.Substring(0, $i).TrimEnd("`r")
      $buf = $buf.Substring($i + 1)
      $ts = (New-TimeSpan $t0 (Get-Date)).TotalSeconds
      if ($line.Trim() -ne "") { "{0,7:N2}  {1}" -f $ts, $line }
    }
    if ($Poll -ne "" -and (New-TimeSpan $lastPoll (Get-Date)).TotalMilliseconds -ge $PollMs) {
      $lastPoll = Get-Date
      $sp.WriteLine($Poll)
    }
  }
} catch {
  Write-Host ("CHYBA: " + $_.Exception.Message) -ForegroundColor Red
  exit 1
} finally {
  if ($sp.IsOpen) { $sp.Close() }
}
