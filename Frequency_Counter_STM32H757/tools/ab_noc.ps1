# ab_noc.ps1 - jedna noc mereni JEDNIM prikazem: stahuje surova mereni (raw_fetch --follow) az do zadane hodiny a pak vyhodnoti.
# Srovnava se s minulou noci (jiny bitstream) ZA STEJNE HODINY DNE, takze se denni kolisani zdroje nepripisuje bitstreamu.
#
# Pouziti (PowerShell, kdekoli):
#   powershell -ExecutionPolicy Bypass -File C:\GitHub\Frequency_Counter_FPGA\Frequency_Counter_STM32H757\tools\ab_noc.ps1
#   powershell -ExecutionPolicy Bypass -File ...\ab_noc.ps1 -Until 07:50 -RefBoot 21:12 -Window 22:00-05:00
# Vystup: raw_noc.csv (surova mereni), raw_noc.txt (status na zacatku a na konci), ab_noc_report.txt (vyhodnoceni).
# Do pristroje se NIC nezapisuje (jen `status`). Port COM nesmi byt otevreny jinde; PC se nesmi uspat.
# ASCII only (PowerShell 5.1 cte skript bez BOM jako ANSI).
param(
    [string]$Until = "07:50",                    # kdy skoncit (nejblizsi takove HH:MM)
    [string]$Port = "COM10",
    [string]$Ref = "noc_raw.csv",                # srovnavaci beh (soubor ve slozce projektu)
    [string]$RefBoot = "21:12",                  # hodina zapnuti desky v srovnavacim behu (minula noc: power-cyklus 21:12)
    [string]$Window = "22:00-05:00",             # srovnava se jen stejna denni doba
    [string]$Name = "noc"
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
    return @{ Text = $txt; Uptime = $up; Build = $build; Lock = ($txt -match "GPSDO: LOCK"); Warm = ($txt -match "NEUSTALENA") }
}

$s = Get-Status
if ($s.Uptime -lt 0) { throw "status se nepodarilo precist - zavri programy drzici $Port" }
$now = Get-Date
$bootClock = $now.AddSeconds(-$s.Uptime)
$bootHM = $bootClock.ToString("HH:mm")

$deadline = Get-Date -Hour ([int]$Until.Split(":")[0]) -Minute ([int]$Until.Split(":")[1]) -Second 0
if ($deadline -le $now) { $deadline = $deadline.AddDays(1) }
$minutes = [int][Math]::Ceiling(($deadline - $now).TotalMinutes)

Write-Host ("Start {0:yyyy-MM-dd HH:mm:ss}; konec {1:yyyy-MM-dd HH:mm} (za {2} min)" -f $now, $deadline, $minutes)
Write-Host ("Deska: uptime {0} s (zapnuta v {1}), LOCK={2}, neustalena={3}, FPGA {4}" -f $s.Uptime, $bootHM, $s.Lock, $s.Warm, $s.Build)
if (-not $s.Lock) { Write-Host "POZOR: GPSDO neni zamceno (LOCK) - mereni bude zatizeno nabehem reference." }
if ($s.Warm)      { Write-Host "POZOR: OCXO se jeste zahriva - prvnich 45 minut se do vyhodnoceni stejne nezapocita." }

$csv = Join-Path $P ("raw_{0}.csv" -f $Name)
$txtf = Join-Path $P ("raw_{0}.txt" -f $Name)
$rep = Join-Path $P ("ab_{0}_report.txt" -f $Name)
if (Test-Path $csv) { Remove-Item $csv }
("=== START {0:yyyy-MM-dd HH:mm:ss} (zapnuto {1}) ===`r`n{2}" -f $now, $bootHM, $s.Text) | Out-File -FilePath $txtf -Encoding ascii

& python (Join-Path $P "tools\raw_fetch.py") --out $csv --follow --every 10 --max-min $minutes

$e = Get-Status
("`r`n=== KONEC {0:yyyy-MM-dd HH:mm:ss} ===`r`n{1}" -f (Get-Date), $e.Text) | Out-File -FilePath $txtf -Encoding ascii -Append
Write-Host ("Sber hotov: {0}  (uptime {1} s, FPGA {2})" -f $csv, $e.Uptime, $e.Build)

$out = & python (Join-Path $P "tools\ab_report.py") ("novy={0}@{1}" -f $csv, $bootHM) ("stary={0}@{1}" -f (Join-Path $P $Ref), $RefBoot) --window $Window 2>&1 | Out-String
$out | Out-File -FilePath $rep -Encoding ascii
Write-Host $out
Write-Host ("Vyhodnoceni ulozeno do {0}" -f $rep)
