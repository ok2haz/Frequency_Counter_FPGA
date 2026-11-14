<#
  tdc_m1_repeat.ps1 - M1: N po sobe jdoucich `tdc cal` za ustalene teploty (audit 2026-10-10_volba-kmitoctu-hodin.md kap. 10.4).
  Odpovida na otazku, zda rozptyl posledniho kodu (B 250/257) je statisticky, nebo jde o boot kalibraci.
  Po kazdem `tdc cal;tdc` zapise radek CSV (posledni kod, max tap, nejvetsi bin, udalosti za koncem, rezerva) + teploty z /api/state.
  Pouziti (COM10 nesmi byt otevreny jinde):
    powershell -ExecutionPolicy Bypass -File tools\tdc_m1_repeat.ps1 -Port COM10 -Out m1.csv -N 10 -GapS 30 -Tag "popis"
  Surovy vystup jde do <Out>.raw.txt. ASCII only (PowerShell 5.1 cte skript bez BOM jako ANSI).
#>
param(
  [string]$Port = "COM10",
  [string]$Out = "tdc_m1.csv",
  [int]$N = 10,
  [int]$GapS = 30,
  [string]$Ip = "10.0.0.106",
  [string]$Tag = ""
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$uartq = Join-Path $here "uartq.ps1"
$raw = "$Out.raw.txt"

function Num($m, $i) { if ($m -and $m.Success) { return $m.Groups[$i].Value } else { return "NA" } }

if (-not (Test-Path $Out)) {
  Set-Content -Path $Out -Encoding ASCII -Value "wall,run,uptime_s,t_ocxo,t_board,t_fpga,a_last,a_maxtap,a_maxbin_ps,a_ovf,b_last,b_maxtap,b_maxbin_ps,b_ovf,rez_a,rez_b,tag"
}

for ($i = 1; $i -le $N; $i++) {
  $o = & powershell -NoProfile -ExecutionPolicy Bypass -File $uartq -Port $Port -Idle 4000 -Max 40000 -Cmds "tdc cal;tdc" 2>&1
  $t = $o -join "`n"
  $upt = "NA"; $to = "NA"; $tb = "NA"; $tf = "NA"
  try {
    $j = (Invoke-WebRequest -Uri ("http://" + $Ip + "/api/state") -UseBasicParsing -TimeoutSec 8).Content | ConvertFrom-Json
    $upt = $j.uptime_s; $to = $j.temp_ocxo_c; $tb = $j.temp_board_c; $tf = $j.temp_fpga_c
  } catch { }
  Add-Content -Path $raw -Encoding ASCII -Value ("##### run " + $i + " " + (Get-Date -Format s) + " uptime~" + $upt + "`n" + $t)
  $pa = [regex]::Match($t, 'TDC A: kodu \d+/\d+ \(nejvyssi (\d+)\), nejvetsi bin \d+ = (\d+) ps.*?za koncem retezu (\d+), max tap (\d+)')
  $pb = [regex]::Match($t, 'TDC B: kodu \d+/\d+ \(nejvyssi (\d+)\), nejvetsi bin \d+ = (\d+) ps.*?za koncem retezu (\d+), max tap (\d+)')
  $rz = [regex]::Match($t, 'rezerva retezu A (-?\d+) tapu, B (-?\d+) tapu')
  $row = (Get-Date -Format s) + "," + $i + "," + $upt + "," + $to + "," + $tb + "," + $tf + "," +
         (Num $pa 1) + "," + (Num $pa 4) + "," + (Num $pa 2) + "," + (Num $pa 3) + "," +
         (Num $pb 1) + "," + (Num $pb 4) + "," + (Num $pb 2) + "," + (Num $pb 3) + "," +
         (Num $rz 1) + "," + (Num $rz 2) + "," + $Tag
  Add-Content -Path $Out -Encoding ASCII -Value $row
  Write-Host $row
  if ($i -lt $N) { Start-Sleep -Seconds $GapS }
}
Write-Host "HOTOVO"
