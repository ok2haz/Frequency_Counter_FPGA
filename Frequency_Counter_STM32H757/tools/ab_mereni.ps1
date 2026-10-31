# ab_mereni.ps1 - jeden beh A/B testu: pocka na zahrati a dost dlouhy beh, stahne surova mereni a ulozi je i s identitou bitstreamu.
#
# Pouziti (PowerShell, kdekoli):
#   powershell -ExecutionPolicy Bypass -File C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757\tools\ab_mereni.ps1 -Name novy1
#   powershell -ExecutionPolicy Bypass -File ...\tools\ab_mereni.ps1 -Name stary1 -Port COM10 -TargetUptimeS 8100
# Vysledek: raw_<Name>.csv (surova mereni) a raw_<Name>.txt (vypis status v okamziku stazeni) ve slozce projektu.
# Skript NIC nezapisuje do pristroje, jen cte (`status`) a stahuje /api/raw. Port COM nesmi byt otevreny jinde.
# ASCII only (PowerShell 5.1 cte skript bez BOM jako ANSI).
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [string]$Port = "COM10",
    [int]$TargetUptimeS = 8100,      # 45 min zahrati + 90 min mereni (3 useky po 30 min)
    [int]$PollS = 300
)

$ErrorActionPreference = "Stop"
$P = "C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757"
$uart = Join-Path $P "tools\uartq.ps1"

function Get-Status {
    $txt = & powershell -NoProfile -ExecutionPolicy Bypass -File $uart -Port $Port -Idle 2500 -Max 40000 -Cmds "status" 2>&1 | Out-String
    $up = -1
    if ($txt -match "uptime\s+(\d+)s") { $up = [int]$Matches[1] }
    $build = ""
    if ($txt -match "FPGA BUILD:\s*([^\r\n]+)") { $build = $Matches[1].Trim() }
    $lock = ($txt -match "GPSDO: LOCK")
    $warn = ($txt -match "NEUSTALENA")
    return @{ Text = $txt; Uptime = $up; Build = $build; Lock = $lock; Warm = $warn }
}

Write-Host ("A/B beh '{0}': cekam na uptime {1} s (zahrati + mereni), kontrola kazdych {2} s" -f $Name, $TargetUptimeS, $PollS)
while ($true) {
    $s = Get-Status
    if ($s.Uptime -lt 0) { Write-Host "  status se nepodarilo precist (zavrit jine programy na $Port?)"; Start-Sleep -Seconds 30; continue }
    Write-Host ("  {0:HH:mm:ss}  uptime {1} s  LOCK={2}  neustalena={3}  FPGA {4}" -f (Get-Date), $s.Uptime, $s.Lock, $s.Warm, $s.Build)
    if ($s.Uptime -ge $TargetUptimeS -and $s.Lock) { break }
    $left = $TargetUptimeS - $s.Uptime
    Start-Sleep -Seconds ([Math]::Min($PollS, [Math]::Max(10, $left)))
}

$csv = Join-Path $P ("raw_{0}.csv" -f $Name)
$txtf = Join-Path $P ("raw_{0}.txt" -f $Name)
if (Test-Path $csv) { Remove-Item $csv }
& python (Join-Path $P "tools\raw_fetch.py") --out $csv
$s = Get-Status
$s.Text | Out-File -FilePath $txtf -Encoding ascii
Write-Host ("Hotovo: {0}  (uptime {1} s, FPGA {2})" -f $csv, $s.Uptime, $s.Build)
Write-Host "Dalsi krok: python tools\ab_report.py novy1=raw_novy1.csv stary1=raw_stary1.csv"
