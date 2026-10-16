<#
  gpsdo_log.ps1 - dlouhodobe zaznamnik smycky GPSDO pres ETH (bez konzole, bez sondy, beze zmeny stavu pristroje).
  Kazdych -Every s precte /api/state a zapise radek CSV: kmitocet proti externi referenci, stav smycky, y [ppt], faze x_ns/xr_ns,
  kod DAC (i se zlomkem), teploty, GPS (fix/druzice/HDOP), pocitadla 1PPS. Kazdych -StabEvery s uklada /api/stab (ADEV z firmwaru)
  a na zacatku a konci /api/errlog (nova udalost = reset, vypadek signalu, ...). Pri vypadku spojeni zapise radek s NA a jede dal.
  Zadne zapisy do pristroje (jen GET).

  Pouziti:
    powershell -ExecutionPolicy Bypass -File tools\gpsdo_log.ps1 -Out docs\audit\data\2026-10-17_noc.csv -Every 2 -MaxMin 480
  Vystupy: <Out> (CSV), <Out>.stab.jsonl (ADEV), <Out>.events.txt (errlog pred a po), <Out>.log (preflight + udalosti).
  ASCII only (PowerShell 5.1 cte skript bez BOM jako ANSI).
#>
param(
  [string]$Out = "gpsdo_log.csv",
  [string]$Ip = "10.0.0.106",
  [int]$Every = 2,
  [int]$StabEvery = 600,
  [double]$MaxMin = 480
)
$ErrorActionPreference = "Continue"
$stab = "$Out.stab.jsonl"; $evt = "$Out.events.txt"; $lg = "$Out.log"

function Log($s) { $l = "[" + (Get-Date -Format "yyyy-MM-dd HH:mm:ss") + "] " + $s; Write-Host $l; Add-Content -Path $lg -Encoding ASCII -Value $l }
function Get-Json($path, $to) { try { return ((Invoke-WebRequest -Uri ("http://" + $Ip + $path) -UseBasicParsing -TimeoutSec $to).Content | ConvertFrom-Json) } catch { return $null } }
function V($o, $a) { try { $x = $o; foreach ($k in $a) { $x = $x.$k }; if ($null -eq $x) { return "" } else { return [string]$x } } catch { return "" } }

$cols = @("wall","uptime_s","seq_meas","freq_hz","gp_state","gp_locked","gp_why","gp_y_ppt","gp_x_ns","gp_xr_ns","gp_code","gp_t_s","gp_pps_ok","gp_pps_rej","gp_tau","gp_kdac_e13","gp_saw",
          "dac_code","dac_mv","vc_mv","t_ocxo","t_board","t_fpga","t_mcu","gps_valid","gps_fix","gps_sats","gps_hdop","warmup","si5356","cm4_ipc","sigma1s")
if (-not (Test-Path $Out)) { Set-Content -Path $Out -Encoding ASCII -Value ($cols -join ",") }

# ---- preflight --------------------------------------------------------------------------------
$s = Get-Json "/api/state" 4
if (-not $s) { Log "PREFLIGHT: deska na $Ip neodpovida - zaznam pojede dal a cekat na ni (radky NA)" }
else {
  $g = $s.gpsdo
  Log ("PREFLIGHT: uptime " + $s.uptime_s + " s, IPC " + (V $s @("cm4","ipc_version")) + ", smycka " + (V $g @("state")) + " (zamceno " + (V $g @("locked")) + ", " + (V $g @("t_s")) + " s)")
  if ($null -eq $g) { Log "VAROVANI: /api/state nema blok gpsdo (stary CM4/CM7?)" }
  elseif ($g.state -ne "LOCK") { Log ("VAROVANI: smycka neni LOCK (" + $g.state + ") - pocitej s rozbehem, zaklad meri az od zamceni") }
  elseif ([int]$g.t_s -lt 3600) { Log ("POZOR: LOCK jen " + $g.t_s + " s; pro zakladni mereni pockej aspon 1 h od zamceni (OCXO + smycka se usta)") }
  if ($s.warmup) { Log "VAROVANI: OCXO se jeste zahriva (warmup)" }
  if ($s.gps -and [int]$s.gps.fix_mode -lt 3) { Log "VAROVANI: GPS bez 3D fixu" }
  if ($s.uptime_s -lt 1800) { Log "POZOR: uptime pod 30 min" }
}
$e = Get-Json "/api/errlog?n=12&from=0" 6
if ($e) { $tot0 = [int]$e.total; ("=== errlog PRED ZACATKEM (total " + $tot0 + ") ===") | Set-Content -Path $evt -Encoding ASCII
  foreach ($x in $e.e) { ("{0}  up {1}  k={2}  {3}  {4}" -f ([DateTimeOffset]::FromUnixTimeSeconds([int64]$x.t).ToLocalTime().ToString("HH:mm:ss")), $x.u, $x.k, $x.g, $x.x) | Add-Content -Path $evt -Encoding ASCII } }
else { $tot0 = -1 }

# ---- smycka -----------------------------------------------------------------------------------
$t0 = Get-Date; $lastStab = (Get-Date).AddSeconds(-$StabEvery); $nok = 0; $nna = 0; $lastUp = -1; $resets = 0
Log ("START: " + $Out + ", perioda " + $Every + " s, max " + $MaxMin + " min")
while (((Get-Date) - $t0).TotalMinutes -lt $MaxMin) {
  $tick = Get-Date
  $s = Get-Json "/api/state" 3
  if ($s) {
    $nok++
    $g = $s.gpsdo
    $row = @((Get-Date -Format "yyyy-MM-ddTHH:mm:ss"), (V $s @("uptime_s")), (V $s @("seq_meas")), (V $s @("freq_hz")),
      (V $g @("state")), (V $g @("locked")), (V $g @("why")), (V $g @("y_ppt")), (V $g @("x_ns")), (V $g @("xr_ns")), (V $g @("code")), (V $g @("t_s")),
      (V $g @("pps_ok")), (V $g @("pps_rej")), (V $g @("tau_s")), (V $g @("kdac_e13")), (V $g @("saw")),
      (V $s @("dac","code")), (V $s @("dac","mv")), (V $s @("vc_mv")), (V $s @("temp_ocxo_c")), (V $s @("temp_board_c")), (V $s @("temp_fpga_c")), (V $s @("temp_mcu_c")),
      (V $s @("gps","valid")), (V $s @("gps","fix_mode")), (V $s @("gps","num_sat")), (V $s @("gps","hdop")), (V $s @("warmup")), (V $s @("si5356_status")),
      (V $s @("cm4","ipc_version")), (V $s @("sy1e15")))
    Add-Content -Path $Out -Encoding ASCII -Value ($row -join ",")
    $up = [int]$s.uptime_s
    if ($lastUp -ge 0 -and $up -lt $lastUp) { $resets++; Log ("!!! RESET pristroje: uptime " + $lastUp + " -> " + $up + " (reset c. " + $resets + ")") }
    $lastUp = $up
    if ($g -and $g.state -ne "LOCK") { if ($script:prevSt -ne $g.state) { Log ("stav smycky: " + $g.state + " (why " + $g.why + ")") } }
    if ($g) { $script:prevSt = $g.state }
  } else {
    $nna++
    Add-Content -Path $Out -Encoding ASCII -Value ((Get-Date -Format "yyyy-MM-ddTHH:mm:ss") + (",NA" * ($cols.Count - 1)))
    if ($nna -eq 1 -or ($nna % 30) -eq 0) { Log ("NEODPOVIDA (" + $nna + "x)") }
  }
  if (((Get-Date) - $lastStab).TotalSeconds -ge $StabEvery) {
    $lastStab = Get-Date
    $st = Get-Json "/api/stab" 6
    if ($st) { Add-Content -Path $stab -Encoding ASCII -Value ((Get-Date -Format "yyyy-MM-ddTHH:mm:ss") + " " + ($st | ConvertTo-Json -Compress -Depth 6)) }
  }
  $sl = $Every - ((Get-Date) - $tick).TotalSeconds
  if ($sl -gt 0) { Start-Sleep -Milliseconds ([int]($sl * 1000)) }
}

# ---- zaver ------------------------------------------------------------------------------------
$e = Get-Json "/api/errlog?n=12&from=0" 6
if ($e) { "" | Add-Content -Path $evt -Encoding ASCII; ("=== errlog PO KONCI (total " + $e.total + ", pred: " + $tot0 + ") ===") | Add-Content -Path $evt -Encoding ASCII
  foreach ($x in $e.e) { ("{0}  up {1}  k={2}  {3}  {4}" -f ([DateTimeOffset]::FromUnixTimeSeconds([int64]$x.t).ToLocalTime().ToString("HH:mm:ss")), $x.u, $x.k, $x.g, $x.x) | Add-Content -Path $evt -Encoding ASCII } }
Log ("KONEC: radku OK " + $nok + ", NA " + $nna + ", resetu pristroje " + $resets)
