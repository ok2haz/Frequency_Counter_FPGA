<#
  tdc_rez_log.ps1 - casova rada rezervy TDC retezu po zapnuti (audit 2026-10-10_volba-kmitoctu-hodin.md kap. 10.4, krok 3).

  V zadanych okamzicich (uptime desky) posle UART `tdc cal`, pak `tdc` a `sensors` a zapise radek CSV:
  nejvyssi kod, max tap, nejvetsi bin a udalosti za koncem obou kanalu + teploty TMP117 0x48/0x49/0x4A.
  Proc rucni `tdc cal`: teplotni rekalibrace se spousti az po driftu 3 C na cidle desky (0x4A), ktere vlastni ohrev
  cipu nevidi - bez ni zustane tabulka z kalibrace pri startu.

  Pouziti (port nesmi byt otevreny jinde):
    powershell -ExecutionPolicy Bypass -File tools\tdc_rez_log.ps1 -Port COM10 -Out m2.csv -Tag "popis"
  Prvni radek se zapise hned, dalsi v uptime z -Targets (sekundy, oddelene carkou). Surovy vystup jde do <Out>.raw.txt.
  ASCII only (PowerShell 5.1 cte skript bez BOM jako ANSI).
#>
param(
  [string]$Port = "COM10",
  [string]$Out = "tdc_rez_log.csv",
  [string]$Targets = "300,600,900,1200,1800,2700,3600,5400",
  [string]$Tag = ""
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$uartq = Join-Path $here "uartq.ps1"
$raw = "$Out.raw.txt"

function Q([string]$cmds, [int]$idle) {
  $o = & powershell -NoProfile -ExecutionPolicy Bypass -File $uartq -Port $Port -Idle $idle -Max 40000 -Cmds $cmds 2>&1
  return ($o -join "`n")
}
function GetUptime {
  $t = Q "status" 2000
  if ($t -match 'uptime (\d+)s') { return [int]$Matches[1] }
  return -1
}
function Num($m, $i) { if ($m -and $m.Success) { return $m.Groups[$i].Value } else { return "NA" } }

if (-not (Test-Path $Out)) {
  Set-Content -Path $Out -Encoding ASCII -Value "wall,uptime_s,t48,t49,t4a,a_last,a_maxtap,a_maxbin_ps,a_ovf,b_last,b_maxtap,b_maxbin_ps,b_ovf,rez_a,rez_b,tag"
}

function Sample([int]$upt) {
  $t = Q "tdc cal;tdc;sensors" 3000
  Add-Content -Path $raw -Encoding ASCII -Value ("##### " + (Get-Date -Format s) + " uptime~" + $upt + "`n" + $t)
  $pa = [regex]::Match($t, 'TDC A: kodu \d+/\d+ \(nejvyssi (\d+)\), nejvetsi bin \d+ = (\d+) ps.*?za koncem retezu (\d+), max tap (\d+)')
  $pb = [regex]::Match($t, 'TDC B: kodu \d+/\d+ \(nejvyssi (\d+)\), nejvetsi bin \d+ = (\d+) ps.*?za koncem retezu (\d+), max tap (\d+)')
  $rz = [regex]::Match($t, 'rezerva retezu A (-?\d+) tapu, B (-?\d+) tapu')
  $t48 = [regex]::Match($t, 'TMP117 0x48\s+(-?[\d.]+)/')
  $t49 = [regex]::Match($t, 'TMP117 0x49\s+(-?[\d.]+)/')
  $t4a = [regex]::Match($t, 'TMP117 0x4A\s+(-?[\d.]+)/')
  $row = (Get-Date -Format s) + "," + $upt + "," + (Num $t48 1) + "," + (Num $t49 1) + "," + (Num $t4a 1) + "," +
         (Num $pa 1) + "," + (Num $pa 4) + "," + (Num $pa 2) + "," + (Num $pa 3) + "," +
         (Num $pb 1) + "," + (Num $pb 4) + "," + (Num $pb 2) + "," + (Num $pb 3) + "," +
         (Num $rz 1) + "," + (Num $rz 2) + "," + $Tag
  Add-Content -Path $Out -Encoding ASCII -Value $row
  Write-Host $row
}

$u0 = GetUptime
Write-Host ("start, uptime " + $u0)
Sample $u0
foreach ($tg in ($Targets -split ',')) {
  $target = [int]$tg
  if ($target -le $u0 + 30) { continue }
  while ($true) {
    $u = GetUptime
    if ($u -lt 0) { Start-Sleep -Seconds 5; continue }
    if ($u -ge $target) { break }
    Start-Sleep -Seconds ([Math]::Max(5, [Math]::Min(60, $target - $u - 5)))
  }
  Sample $u
}
Write-Host "HOTOVO"
