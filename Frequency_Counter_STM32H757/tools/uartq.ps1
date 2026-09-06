<#
  uartq.ps1 - posle jeden nebo VIC prikazu na UART desku a vypise celou odpoved.

  Proti puvodnimu uart.ps1:
    * cte dokud data PLYNOU (idle-timeout), ne pevnych 500 ms -> nerozseka
      dlouhe vypisy jako `status`, `sensors`, `stats`, `selftest`, `membench`
    * umi retezec prikazu za sebou (kazdy s vlastnim nadpisem)
    * `-Quiet` vypise jen odpovedi (bez "> prikaz" hlavicek), vhodne pro parsovani

  Pouziti:
    powershell -ExecutionPolicy Bypass -File tools\uartq.ps1 -Port COM8 status
    powershell -ExecutionPolicy Bypass -File tools\uartq.ps1 -Port COM8 -Idle 1500 status stats sensors
    powershell -ExecutionPolicy Bypass -File tools\uartq.ps1 -Port COM8 -Quiet freq

  ⚠️ Port nesmi byt soucasne otevreny v PuTTY/Tera Term (exkluzivni pristup).
  ⚠️ CPU cisla ze `stats` nemer, kdyz zaroven bezi cteni ladici sondou —
     halt cile nafoukne g_rtos_cpu_pct (viz CLAUDE.md).
#>
param(
  [string]$Port = "COM8",
  [int]   $Baud = 115200,
  [int]   $Idle = 900,      # ms ticha, po kterych povazujeme odpoved za dokoncenou
  [int]   $Max  = 15000,    # ms tvrdy strop na jeden prikaz
  [switch]$Quiet,
  # ⚠️ Pri `powershell -File` se argumenty predavaji jako RETEZCE, takze pole
  # `-Cmds a,b` prijde jako jediny string "a,b". Prikazy proto oddeluj
  # STREDNIKEM (mezery v prikazu tim zustanou zachovane): -Cmds "status;stats"
  [string]$Cmds = "ping"
)
$CmdList = @($Cmds -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" })

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.NewLine      = "`r`n"
$sp.ReadTimeout  = 200
$sp.Encoding     = [System.Text.Encoding]::UTF8
try {
  $sp.Open()
  Start-Sleep -Milliseconds 150
  $sp.DiscardInBuffer()
  foreach ($c in $CmdList) {
    if (-not $Quiet) { Write-Host ("===== > {0}" -f $c) -ForegroundColor Cyan }
    $sp.DiscardInBuffer()
    $sp.WriteLine($c)
    $sb = New-Object System.Text.StringBuilder
    $t0 = Get-Date
    $last = Get-Date
    while ($true) {
      Start-Sleep -Milliseconds 60
      $chunk = $sp.ReadExisting()
      if ($chunk.Length -gt 0) { [void]$sb.Append($chunk); $last = Get-Date }
      if ((New-TimeSpan $last (Get-Date)).TotalMilliseconds -ge $Idle) { break }
      if ((New-TimeSpan $t0   (Get-Date)).TotalMilliseconds -ge $Max)  { break }
    }
    $out = $sb.ToString().Trim()
    if ([string]::IsNullOrWhiteSpace($out)) { Write-Host "(zadna odpoved)" -ForegroundColor Yellow }
    else { Write-Output $out }
  }
} catch {
  Write-Host ("CHYBA: " + $_.Exception.Message) -ForegroundColor Red
  exit 1
} finally {
  if ($sp.IsOpen) { $sp.Close() }
}
