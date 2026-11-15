# F-0293: lokalni mereni strmosti K (kmitocet / LSB DAC) v okoli pracovniho kodu.
# POZOR: ZAPISUJE do pristroje (prikaz dac, gpsdo off). Smycka GPSDO je po dobu testu vypnuta, OCXO jede
# volne (holdover s rucnim kodem). Na konci i pri chybe se vzdy provede `gpsdo on`.
# Postup: stridani zakladni kod C0 a kod C0+d (d = 1,-1,2,-2,4,-4,8,-8), kazda faze -PhaseMin minut,
#         kazdou minutu se zapise radek `gpsdo` (TIE = surova faze, jejiz sklon je kmitocet).
# Start: skript nejdriv CEKA, az je smycka -WaitLockMin minut v LOCK (ZAMCENO) a varovani "TEPLOTA NEUSTALENA"
#        zmizelo (nejvys -WaitMaxMin minut, jinak konec bez zasahu). Prubeh cekani jde do <Out>.log.
# Pojistka: po prvni fazi (zakladni kod) musi mit TIE aspon 3 ruzne hodnoty, jinak se fazovy monitor pri vypnute
#           smycce nehybe a test by nic nemeril -> konec (kod vracen, gpsdo on).
# Vyhodnoceni: python -I tools\f0293_vyhodnot.py <vystupni csv>
# Pouziti (terminal s UART zavreny):
#   powershell -ExecutionPolicy Bypass -File tools\f0293_k_kroky.ps1 -Out C:\cesta\k_kroky.csv [-PhaseMin 40]
param(
  [string]$Port = "COM10",
  [string]$Out = "k_kroky.csv",
  [int]$PhaseMin = 25,
  [int]$WaitLockMin = 60,
  [int]$WaitMaxMin = 240,
  [int[]]$Steps = @(1, -1, 2, -2, 4, -4, 8, -8),
  [switch]$DryRun
)
$uartq = Join-Path $PSScriptRoot "uartq.ps1"
$logf = $Out + ".log"
function Q($cmds) {
  return (& powershell -ExecutionPolicy Bypass -File $uartq -Port $Port -Idle 1500 -Quiet -Cmds $cmds | Out-String)
}
function Pick($text, $rx, $g) {
  $m = [regex]::Match($text, $rx)
  if ($m.Success) { return $m.Groups[$g].Value } else { return "" }
}
function L($s) {
  $line = (Get-Date -Format "yyyy-MM-dd HH:mm:ss") + " " + $s
  $line | Out-File -Append -Encoding ascii $logf
  Write-Host $line
}

if (-not $DryRun) {
  L "start, cekam na LOCK >= $WaitLockMin min a ustalenou teplotu (nejvys $WaitMaxMin min)"
  $tLock = $null
  $tMax = (Get-Date).AddMinutes($WaitMaxMin)
  while ($true) {
    $o = Q "gpsdo"
    $w = Q "status"
    $locked = ($o -match 'GPSDO: LOCK \(ZAMCENO\)')
    $warm = ($w -match 'TEPLOTA NEUSTALENA')
    if ($locked -and -not $warm) { if ($tLock -eq $null) { $tLock = Get-Date } } else { $tLock = $null }
    $held = 0
    if ($tLock -ne $null) { $held = [int]((Get-Date) - $tLock).TotalMinutes }
    L ("cekani: stav " + (Pick $o 'GPSDO: (HOLD|LOCK|FLL|WAIT|OFF)' 1) + ", zamceno " + $locked + ", teplota neustalena " + $warm + ", drzeno " + $held + " min")
    if ($tLock -ne $null -and $held -ge $WaitLockMin) { break }
    if ((Get-Date) -gt $tMax) { L "LOCK se nepodarilo udrzet -> test se nespousti, pristroj beze zmeny"; exit 1 }
    Start-Sleep -Seconds 120
  }
}

$o = Q "gpsdo"
$state = Pick $o 'GPSDO: (HOLD|LOCK|FLL|WAIT|OFF)' 1
$kodTxt = Pick $o 'kod ([\d.]+)' 1
if ($state -ne "LOCK" -or $kodTxt -eq "") { L "Smycka neni v LOCK (stav '$state'), test se nespousti."; exit 1 }
$c0 = [int][math]::Round([double]::Parse($kodTxt, [Globalization.CultureInfo]::InvariantCulture))
L ("Zakladni kod C0 = $c0 (z $kodTxt), faze $PhaseMin min, kroky: " + ($Steps -join ','))
$plan = @()
$plan += ,@("base", $c0, 0)
foreach ($d in $Steps) { $plan += ,@("krok", ($c0 + $d), $d); $plan += ,@("base", $c0, 0) }
if ($DryRun) { $plan | ForEach-Object { "$($_[0]) kod $($_[1]) d $($_[2])" }; Write-Host ("celkem {0} faz = {1:N1} h" -f $plan.Count, ($plan.Count * $PhaseMin / 60.0)); exit 0 }
"cas;faze_n;typ;kod_cil;d;stav;y_ppb;faze_ns;TIE_ns;kod" | Out-File -Encoding ascii $Out
try {
  $fn = 0
  foreach ($p in $plan) {
    [void](Q "dac $($p[1])")
    L ("faze $fn " + $p[0] + " kod " + $p[1] + " (d " + $p[2] + ")")
    $ties = @{}
    $tEnd = (Get-Date).AddMinutes($PhaseMin)
    while ((Get-Date) -lt $tEnd) {
      $t = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
      $g = Q "gpsdo"
      $tie = Pick $g 'TIE ([-+\d]+) ns' 1
      if ($tie -ne "") { $ties[$tie] = 1 }
      $f = @($t, $fn, $p[0], $p[1], $p[2],
        (Pick $g 'GPSDO: (HOLD|LOCK|FLL|WAIT|OFF)' 1), (Pick $g '\| y ([-+\d.]+) ppb' 1),
        (Pick $g 'faze ([-+\d]+) ns' 1), $tie, (Pick $g 'kod ([\d.]+)' 1))
      ($f -join ";") | Out-File -Append -Encoding ascii $Out
      Start-Sleep -Seconds 60
    }
    if ($fn -eq 0 -and $ties.Count -lt 3) {
      L ("POJISTKA: TIE behem prvni faze jen " + $ties.Count + " ruznych hodnot -> fazovy monitor se nehybe, konec")
      break
    }
    $fn++
  }
}
finally {
  [void](Q "dac $c0")
  [void](Q "gpsdo on")
  L "Hotovo / prerusene: kod vracen na $c0, gpsdo on."
}
