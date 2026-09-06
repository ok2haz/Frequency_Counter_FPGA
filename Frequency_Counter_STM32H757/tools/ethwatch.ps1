<#
  ethwatch.ps1 - sleduje stav site desky po resetu a zaroven ji zkousi
  dosahnout po siti. Odlisi „nikdy nenabehlo" od „nabehlo a pak umrelo".

  Vypisuje na jednom radku: uptime, co hlasi deska (link + IP) a jestli
  na ni v tu chvili odpovida HTTP z tohohle pocitace.
#>
param(
  [string]$Port  = "COM8",
  [int]   $Baud  = 115200,
  [int]   $Every = 15,      # s mezi vzorky
  [int]   $Count = 12,      # pocet vzorku
  [switch]$Reset
)

function Send-Cmd([System.IO.Ports.SerialPort]$sp, [string]$cmd, [int]$idle = 1500) {
  $sp.DiscardInBuffer(); $sp.Write($cmd + "`r")
  $buf = ""; $last = [Environment]::TickCount
  while (([Environment]::TickCount - $last) -lt $idle) {
    if ($sp.BytesToRead -gt 0) { $buf += $sp.ReadExisting(); $last = [Environment]::TickCount }
    else { Start-Sleep -Milliseconds 25 }
  }
  return $buf
}

if ($Reset) {
  $cli = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
  & $cli -c port=SWD mode=HOTPLUG -rst | Out-Null
  Write-Host "reset proveden, cekam na boot..."
  Start-Sleep -Seconds 12
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 2000; $sp.Open(); Start-Sleep -Milliseconds 300

Write-Host ("{0,-8} {1,-34} {2,-16} {3}" -f "uptime", "co hlasi deska", "HTTP z PC", "ping")
Write-Host ("-" * 78)

for ($i = 0; $i -lt $Count; $i++) {
  $t = Send-Cmd $sp "status" 2500
  $up = if ($t -match 'uptime (\d+)s') { $Matches[1] + "s" } else { "?" }
  $net = if ($t -match 'NET:\s*([^\r\n]+)') { $Matches[1].Trim() } else { "(bez NET radku)" }

  $ip = if ($net -match '(\d+\.\d+\.\d+\.\d+)') { $Matches[1] } else { $null }
  $http = "-"; $png = "-"
  if ($ip -and $ip -ne "0.0.0.0") {
    try {
      $r = Invoke-WebRequest -Uri ("http://" + $ip + "/api/state") -TimeoutSec 4 -UseBasicParsing
      $http = "HTTP " + $r.StatusCode
    } catch { $http = "timeout" }
    $png = if (Test-Connection -ComputerName $ip -Count 1 -Quiet -ErrorAction SilentlyContinue) { "ok" } else { "ne" }
  }
  Write-Host ("{0,-8} {1,-34} {2,-16} {3}" -f $up, $net, $http, $png)
  if ($i -lt ($Count - 1)) { Start-Sleep -Seconds $Every }
}
$sp.Close()
