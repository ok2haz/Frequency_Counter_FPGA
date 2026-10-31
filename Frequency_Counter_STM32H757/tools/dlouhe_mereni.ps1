<#
  dlouhe_mereni.ps1 - plne automaticke dlouhe mereni BEZ DOTAZU (TODO #326 + GPS-only/survey-in).
  Spusti se jednou a bezi ~18 h (4 h faze 1 + 14 h faze 2), pak sam vrati pristroj do vychoziho stavu.
  ASCII only (PowerShell 5.1 cte skript bez BOM jako ANSI).

  FAZE 1 (vychozi 240 min): vliv smycky GPSDO / DAC / ditheru. Bloky po 20 min v poradi OFF, D200, D100, D2 (x3):
     OFF  = gpsdo off (kod DAC zmrazen, bez ditheru)       D200 / D100 / D2 = gpsdo on + dither 200 / 100 / 2 Hz
  FAZE 2 (vychozi 840 min): volne bezici OCXO (smycka OFF) vuci NASI GPS (strecha) i externimu GPSDO; GPS se NEMENI.
     Vychozi je JEDEN blok G0 (zaklad pro #312: ADEV OCXO, rozdil ext GPSDO - nase GPS, stari OCXO).
     POZOR: NEO-7M odmita survey-in (CFG-TMODE2 -> NAK) i GLONASS (CFG-GNSS multi -> NAK), zmereno 2026-10-18 na v0.30.6.
     Bloky GM (GPS+GLONASS) a GS (survey-in) jsou ponechany pro timing prijimac (NEO-M8T), -Phase2Names "G0,GM,GS,G1";
     na NEO-7M je NEPOUZIVEJ (jen by zbytecne restartovaly akvizici).
  Hradlo 0,25 s a kanal A si skript nastavi sam a overi (UART `scpi SENS:FREQ:GATE 0.25`, `SENS:FREQ:CHAN 0`).

  Co se uklada (vse do docs\audit\data, predpona <Tag>):
     <Tag>_faze1_smycka.csv, <Tag>_faze2_gps.csv   hlavni CSV z gpsdo_log.ps1 (kmitocet A+B, smycka, xr_ns, DAC, teploty, GPS, ...)
        + .stab.jsonl (ADEV firmwaru), .sats.jsonl (druziceni po 60 s), .phase.csv, .events.txt, .log
     <Tag>.raw.csv          surova mereni A z datove cache pristroje (tools\raw_fetch.py --follow; bez mezer)
     <Tag>.blocks.csv       KDY se co prepnulo (cas, faze, blok, stav smycky/ditheru/GNSS/survey, skip_min, vysledek ACK)
     <Tag>.plan.csv         plan bloku
     <Tag>.snap.csv         snimek stavu kazdych 5 min (UART: status, regr, gps, gpsraw, gps ack, dac) + <Tag>.snap.raw.log (cely text)
     <Tag>.run.log          protokol behu (resety, restarty loggeru, chyby, overeni stavu)

  Pouziti:
     powershell -ExecutionPolicy Bypass -File tools\dlouhe_mereni.ps1                 (ostre, 18 h)
     powershell -ExecutionPolicy Bypass -File tools\dlouhe_mereni.ps1 -DryRun         (jen vypise plan)
     powershell -ExecutionPolicy Bypass -File tools\dlouhe_mereni.ps1 -Check          (preflight + jeden snimek, nic nespusti)
  Vyzaduje firmware >= v0.30.6 (UART `gps gnss`, `gps survey`, `gps ack`). Port COM nesmi byt otevreny v jinem programu.
#>
param(
  [string]$Port = "COM10",
  [string]$Ip = "10.0.0.106",
  [string]$OutDir = "docs\audit\data",
  [string]$Tag = "",
  [int]$Phase1Min = 240,
  [int]$BlockMin1 = 20,
  [int]$Phase2Min = 840,
  [string]$Phase2Names = "G0",   # carkou oddelene bloky faze 2 (G0, GM, GS, G1); delka se deli rovnomerne
  [int]$SnapEverySec = 300,
  [switch]$DryRun,
  [switch]$Check,
  [switch]$NoFwCheck           # preskoci kontrolu verze firmware (kontrola se dela jen kdyz je naplanovana faze 2)
)
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if ($Tag -eq "") { $Tag = (Get-Date -Format "yyyy-MM-dd_HHmm") + "_dlouhe" }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$P = Join-Path $OutDir $Tag
$UartQ = Join-Path $PSScriptRoot "uartq.ps1"
$LoggerPs1 = Join-Path $PSScriptRoot "gpsdo_log.ps1"
$RawFetch = Join-Path $PSScriptRoot "raw_fetch.py"
# jedina instance: dve soucasne instance se perou o COM port a o stav smycky/ditheru (zjisteno 2026-10-18)
$script:mx = New-Object System.Threading.Mutex($false, "Global\dlouhe_mereni_gpsdo")
if (-not $DryRun -and -not $script:mx.WaitOne(0)) { Write-Host "FATAL: jiny beh dlouhe_mereni.ps1 uz bezi (dvoji beh by se pral o UART a stav desky). Nic se nespustilo."; exit 4 }
$runLog = "$P.run.log"; $blocksCsv = "$P.blocks.csv"; $planCsv = "$P.plan.csv"; $snapCsv = "$P.snap.csv"; $snapRaw = "$P.snap.raw.log"

function Log($s) { $l = "[" + (Get-Date -Format "yyyy-MM-dd HH:mm:ss") + "] " + $s; Write-Host $l; Add-Content -Path $runLog -Encoding ASCII -Value $l }
function Get-Json($path, $to) { try { return ((Invoke-WebRequest -Uri ("http://" + $Ip + $path) -UseBasicParsing -TimeoutSec $to).Content | ConvertFrom-Json) } catch { return $null } }
function Rx($text, $pat, $grp) { $m = [regex]::Match($text, $pat); if ($m.Success) { return $m.Groups[$grp].Value } else { return "" } }

# UART prikaz(y) pres uartq.ps1 (kazdy volani otevre a zavre port); pri prazdne odpovedi 3 pokusy.
function Invoke-Uart([string]$cmds, [int]$idle = 1300, [int]$max = 15000) {
  for ($try = 1; $try -le 3; $try++) {
    $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $UartQ -Port $Port -Quiet -Idle $idle -Max $max -Cmds $cmds 2>&1 | Out-String
    if ($out -and $out.Trim().Length -gt 0 -and $out -notmatch "CHYBA") { return $out }
    $why = ""; if ($out) { $why = $out.Trim(); if ($why.Length -gt 120) { $why = $why.Substring(0, 120) } }
    Log ("UART pokus " + $try + " selhal (" + $cmds + "): " + $why)
    Start-Sleep -Seconds 4
  }
  Log ("UART bez odpovedi: " + $cmds)
  return ""
}

# ---- plan bloku -------------------------------------------------------------------------------
$blocks = New-Object System.Collections.ArrayList
$names1 = @("OFF", "D200", "D100", "D2")
$n1 = [int][math]::Floor($Phase1Min / [double]$BlockMin1)
for ($i = 0; $i -lt $n1; $i++) {
  [void]$blocks.Add([pscustomobject]@{ phase = 1; name = $names1[$i % 4]; startMin = $i * $BlockMin1; lenMin = $BlockMin1; skip = 5 })
}
$p2start = $n1 * $BlockMin1
if ($Phase2Min -gt 0) {
  $names2 = @($Phase2Names -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" })
  $len2 = [int][math]::Floor($Phase2Min / [double]$names2.Count)
  for ($i = 0; $i -lt $names2.Count; $i++) {
    $sk = 15; if ($names2[$i] -eq "GM") { $sk = 30 }; if ($names2[$i] -eq "GS") { $sk = 60 }; if ($names2.Count -eq 1) { $sk = 10 }
    [void]$blocks.Add([pscustomobject]@{ phase = 2; name = $names2[$i]; startMin = $p2start + $i * $len2; lenMin = $len2; skip = $sk })
  }
}
$totalMin = 0
if ($blocks.Count -gt 0) { $lb = $blocks[$blocks.Count - 1]; $totalMin = $lb.startMin + $lb.lenMin }
Set-Content -Path $planCsv -Encoding ASCII -Value "faze,blok,start_min,delka_min,skip_min"
foreach ($b in $blocks) { Add-Content -Path $planCsv -Encoding ASCII -Value ("{0},{1},{2},{3},{4}" -f $b.phase, $b.name, $b.startMin, $b.lenMin, $b.skip) }

if ($DryRun) {
  Write-Host ("PLAN: " + $blocks.Count + " bloku, celkem " + $totalMin + " min (" + [math]::Round($totalMin / 60.0, 1) + " h), predpona " + $P)
  $blocks | Format-Table -AutoSize | Out-String | Write-Host
  exit 0
}

function Cmd-For($name) {
  switch ($name) {
    "OFF"  { return "gpsdo off" }
    "D200" { return "gpsdo on;dac dither 200" }
    "D100" { return "gpsdo on;dac dither 100" }
    "D2"   { return "gpsdo on;dac dither 2" }
    "G0"   { return "gpsdo off" }
    "GM"   { return "gpsdo off;gps survey off;gps gnss multi" }
    "GS"   { return "gpsdo off;gps gnss gps;gps survey on" }
    "G1"   { return "gpsdo off" }
    "NS"   { return "gpsdo off;gps nav5 stationary" }   # v0.30.7: UBX-CFG-NAV5 dynModel Stationary (NEO-7M ACKuje, na rozdil od TMODE2)
    "NA"   { return "gpsdo off;gps nav5 auto" }         # totez, dynModel Portable (kontrolni blok)
  }
  return ""
}
function State-Of($name) {   # loop, dither, gnss, survey (pro blocks.csv)
  switch ($name) {
    "OFF"  { return @("OFF", "-", "gps", "-") }
    "D200" { return @("ON", "200", "gps", "-") }
    "D100" { return @("ON", "100", "gps", "-") }
    "D2"   { return @("ON", "2", "gps", "-") }
    "G0"   { return @("OFF", "-", "gps", "off") }
    "GM"   { return @("OFF", "-", "multi", "off") }
    "GS"   { return @("OFF", "-", "gps", "on") }
    "G1"   { return @("OFF", "-", "gps", "off") }
    "NS"   { return @("OFF", "-", "gps", "off") }
    "NA"   { return @("OFF", "-", "gps", "off") }
  }
  return @("?", "?", "?", "?")
}

# ---- snimek stavu -----------------------------------------------------------------------------
$snapCols = @("wall","uptime_s","gpsdo_state","gpsdo_y_ppb","gpsdo_code","dither_hz","dac_writes","crc_err","seq_gaps","seq_resync","selfchk_count","selfchk_clock","selfchk_len","tdc_rez_a","tdc_rez_b","regr_win","regr_used","regr_rej","regr_sigma_ppb","heap_free","cpu_pct","stack_fpga","stack_dac","stack_idle","ltdc_under","ltdc_flips","gps_fix","gps_sat","gpsraw_rx_ok","gpsraw_rx_bad","gps_ack","errlog_total","block")
if (-not (Test-Path $snapCsv)) { Set-Content -Path $snapCsv -Encoding ASCII -Value ($snapCols -join ",") }
function Take-Snapshot($blockName) {
  $t = Invoke-Uart "status;regr;gps;gpsraw;gps ack;dac;gpsdo;stats" 1800 30000
  Add-Content -Path $snapRaw -Encoding ASCII -Value ("===== " + (Get-Date -Format "yyyy-MM-ddTHH:mm:ss") + " block " + $blockName)
  Add-Content -Path $snapRaw -Encoding ASCII -Value $t
  $el = Get-Json "/api/errlog?n=1&from=0" 5
  $etot = ""; if ($el) { $etot = [string]$el.total }
  $ack = (Rx $t "GPS ACK: ([^\r\n]*)" 1) -replace ",", ";"
  $v = @(
    (Get-Date -Format "yyyy-MM-ddTHH:mm:ss"),
    (Rx $t "uptime (\d+)s" 1),
    (Rx $t "GPSDO: (OFF|FLL|LOCK|WAIT|HOLD)" 1),
    (Rx $t "GPSDO: [A-Z]+[^|\r\n]*\|[^|\r\n]*\| y ([+-]?[0-9.]+) ppb" 1),
    (Rx $t "kod ([0-9]+\.[0-9]+) \|" 1),
    (Rx $t "dither (\d+) Hz" 1),
    (Rx $t "zapisu (\d+), chyb" 1),
    (Rx $t "CRC err (\d+)" 1),
    (Rx $t "SEQ FPGA: diry (\d+)" 1),
    (Rx $t "resync (\d+)" 1),
    (Rx $t "zamitnuto oken: citani (\d+)" 1),
    (Rx $t "zamitnuto oken: citani \d+, hodiny (\d+)" 1),
    (Rx $t "delka okna (\d+)" 1),
    (Rx $t "REZERVA: A (\d+) tapu" 1),
    (Rx $t "B (\d+) tapu \(" 1),
    (Rx $t "oken s blokem (\d+)" 1),
    (Rx $t "pouzito (\d+), zamitnuto" 1),
    (Rx $t "zamitnuto (\d+) \| znacek" 1),
    (Rx $t "sigma ([0-9.]+) ppb" 1),
    (Rx $t "Heap: free (\d+) B" 1),
    (Rx $t "CPU (\d+)%" 1),
    (Rx $t "stack Fpga\s+free (\d+) B" 1),
    (Rx $t "DacTask\s+\d+%\s+\d+\s+(\d+) B" 1),
    (Rx $t "IDLE\s+\d+%\s+\d+\s+(\d+) B" 1),
    (Rx $t "podteceni FIFO (\d+) /" 1),
    (Rx $t "podteceni FIFO \d+ / (\d+) flipu" 1),
    (Rx $t "FIX:(\d)" 1),
    (Rx $t "SAT:(\d+)" 1),
    (Rx $t "RX:(\d+)/" 1),
    (Rx $t "RX:\d+/(\d+)" 1),
    $ack,
    $etot,
    $blockName
  )
  Add-Content -Path $snapCsv -Encoding ASCII -Value ($v -join ",")
  return $t
}

# ---- pomocne procesy (loggery) ------------------------------------------------------------------
function Start-Logger($out, $maxMin) {
  $a = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $LoggerPs1, "-Out", $out, "-Ip", $Ip, "-Every", "0.2", "-OnNew", "-ExpectGate", "0.25", "-StabEvery", "600", "-SatsEvery", "60", "-MaxMin", ([string]$maxMin))
  return (Start-Process -FilePath "powershell.exe" -ArgumentList $a -PassThru -WindowStyle Minimized)
}
function Start-Raw() {
  $a = @($RawFetch, "--out", "$P.raw.csv", "--ip", $Ip, "--follow", "--every", "10")
  return (Start-Process -FilePath "python" -ArgumentList $a -PassThru -WindowStyle Minimized -RedirectStandardOutput "$P.raw.stdout.log" -RedirectStandardError "$P.raw.stderr.log")
}

# ---- preflight ----------------------------------------------------------------------------------
Log ("START dlouheho mereni, predpona " + $P + ", plan " + $totalMin + " min")
$ver = Invoke-Uart "version" 800 6000
$vs = Rx $ver "v(\d+\.\d+\.\d+)" 1
Log ("firmware: " + $vs)
if (-not $NoFwCheck -and $Phase2Min -gt 0 -and ($vs -eq "" -or ([version]$vs) -lt ([version]"0.30.6"))) {
  Log "FATAL: firmware < v0.30.6 (chybi UART gps gnss / gps survey / gps ack). Naflashuj v0.30.6 a spust znovu. Nic se nespustilo."
  exit 2
}
$st0 = Get-Json "/api/state" 5
if (-not $st0) { Log "FATAL: /api/state neodpovida na $Ip. Nic se nespustilo."; exit 3 }
Log ("stav: uptime " + $st0.uptime_s + " s, smycka " + $st0.gpsdo.state + ", kanal " + $st0.channel + ", hradlo " + $st0.set_gate_s + " s, GPS fix " + $st0.gps.fix_mode + ", druzic " + $st0.gps.num_sat)

# hradlo 0,25 s, kanal A, RUN
$g = Invoke-Uart "scpi SENS:FREQ:GATE 0.25;scpi SENS:FREQ:CHAN 0;scpi INIT:CONT?;regr reset" 1000 12000
Start-Sleep -Seconds 3
$st1 = Get-Json "/api/state" 5
$okCfg = $st1 -and ([math]::Abs([double]$st1.set_gate_s - 0.25) -lt 1e-6) -and ([int]$st1.channel -eq 0)
Log ("nastaveni hradla/kanalu: gate " + $st1.set_gate_s + " s, kanal " + $st1.channel + " -> " + $(if ($okCfg) { "OK" } else { "NESEDI (pokracuji, viz run.log)" }))
if ((Rx $g "INIT:CONT\?\s*[\r\n]+\s*(\d)" 1) -eq "0") { [void](Invoke-Uart "scpi INIT" 800 5000); Log "INIT:CONT bylo 0 -> poslano INIT" }

if ($Check) {
  $t = Take-Snapshot "check"
  Log "CHECK: jeden snimek ulozen do $snapCsv; radek:"
  Get-Content $snapCsv -Tail 2 | ForEach-Object { Log $_ }
  $sj = Get-Json "/api/sats" 4
  if ($sj) { Log ("CHECK: /api/sats n=" + $sj.n) } else { Log "CHECK: /api/sats neodpovida" }
  exit 0
}

# pred startem pockat na LOCK smycky (aspon 300 s zamceno, nejdele 30 min), aby se mereni nezacalo uprostred rozbehu OCXO/FLL
$waitUntil = (Get-Date).AddMinutes(30); $sw = $null
while ((Get-Date) -lt $waitUntil) {
  $sw = Get-Json "/api/state" 4
  if ($sw -and $sw.gpsdo.state -eq "LOCK" -and [int]$sw.gpsdo.t_s -ge 300) { break }
  Start-Sleep -Seconds 20
}
if ($sw) { Log ("cekani na LOCK skoncilo: smycka " + $sw.gpsdo.state + ", zamceno " + $sw.gpsdo.t_s + " s") } else { Log "cekani na LOCK: /api/state neodpovida, pokracuji" }

if (-not (Test-Path $blocksCsv)) { Set-Content -Path $blocksCsv -Encoding ASCII -Value "wall,faze,blok,start_min_plan,delka_min,skip_min,smycka,dither_hz,gnss,survey,ack,overeni" }

$logF1 = "${P}_faze1_smycka.csv"; $logF2 = "${P}_faze2_gps.csv"
$t0 = Get-Date
$endAt = $t0.AddMinutes($totalMin)
$procRaw = Start-Raw
Log ("raw_fetch spusten, PID " + $procRaw.Id)
$procL1 = $null; $procL2 = $null
if ($n1 -gt 0) { $procL1 = Start-Logger $logF1 ($n1 * $BlockMin1 + 3); Log ("logger faze 1 spusten, PID " + $procL1.Id) }
$curIdx = -1; $lastSnap = (Get-Date).AddSeconds(-$SnapEverySec); $lastMon = Get-Date; $lastUp = -1; $resets = 0; $p2started = $false

function Apply-Block($b, $reason) {
  $cmd = Cmd-For $b.name
  $out = Invoke-Uart $cmd 1500 25000
  Start-Sleep -Seconds 5
  $ackTxt = Rx (Invoke-Uart "gps ack" 900 6000) "GPS ACK: ([^\r\n]*)" 1
  $chk = Invoke-Uart "gpsdo;dac" 1200 10000
  $st = State-Of $b.name
  $gs = Rx $chk "GPSDO: ([A-Z]+)" 1
  $dh = Rx $chk "dither (\d+) Hz" 1
  $ok = $true
  if ($st[0] -eq "OFF" -and $gs -ne "OFF") { $ok = $false }
  if ($st[0] -eq "ON" -and $gs -eq "OFF") { $ok = $false }
  if ($st[1] -ne "-" -and $dh -ne $st[1]) { $ok = $false }
  $ov = $(if ($ok) { "OK" } else { "NESEDI(gpsdo=" + $gs + ";dither=" + $dh + ")" })
  Log ("BLOK " + $b.name + " (faze " + $b.phase + ", " + $reason + "): " + ($cmd -replace ";", " ; ") + " -> " + $ov + " | ack: " + $ackTxt)
  Add-Content -Path $blocksCsv -Encoding ASCII -Value ((Get-Date -Format "yyyy-MM-ddTHH:mm:ss") + "," + $b.phase + "," + $b.name + "," + $b.startMin + "," + $b.lenMin + "," + $b.skip + "," + $st[0] + "," + $st[1] + "," + $st[2] + "," + $st[3] + "," + ($ackTxt -replace ",", ";") + "," + $ov)
  if (-not $ok) { [void](Invoke-Uart $cmd 1500 25000); Log ("BLOK " + $b.name + ": prikazy poslany znovu") }
}

try {
  while ((Get-Date) -lt $endAt) {
    $el = ((Get-Date) - $t0).TotalMinutes
    $idx = -1
    for ($i = 0; $i -lt $blocks.Count; $i++) { if ($el -ge $blocks[$i].startMin -and $el -lt ($blocks[$i].startMin + $blocks[$i].lenMin)) { $idx = $i } }
    if ($idx -ge 0 -and $idx -ne $curIdx) {
      $curIdx = $idx
      $b = $blocks[$idx]
      if ($b.phase -eq 2 -and -not $p2started) {
        $p2started = $true
        $procL2 = Start-Logger $logF2 ($Phase2Min + 3); Log ("logger faze 2 spusten, PID " + $procL2.Id)
      }
      Apply-Block $b "zacatek bloku"
    }
    $curName = ""; if ($curIdx -ge 0) { $curName = $blocks[$curIdx].name }
    # monitor: reset pristroje, zivost pomocnych procesu
    if (((Get-Date) - $lastMon).TotalSeconds -ge 30) {
      $lastMon = Get-Date
      $s = Get-Json "/api/state" 4
      if ($s) {
        $up = [int]$s.uptime_s
        if ($lastUp -ge 0 -and $up -lt $lastUp) {
          $resets++; Log ("!!! RESET pristroje: uptime " + $lastUp + " -> " + $up + " (c. " + $resets + ") - nastaveni se po resetu NEPERSISTUJE, aplikuji znovu blok " + $curName)
          Start-Sleep -Seconds 60
          [void](Invoke-Uart "scpi SENS:FREQ:GATE 0.25;scpi SENS:FREQ:CHAN 0" 1000 8000)
          if ($curIdx -ge 0) { Apply-Block $blocks[$curIdx] "po resetu" }
        }
        $lastUp = $up
      }
      if ($procRaw.HasExited) { Log "raw_fetch skoncil -> restart"; $procRaw = Start-Raw }
      if ($procL1 -and $procL1.HasExited -and $curIdx -ge 0 -and $blocks[$curIdx].phase -eq 1) {
        $rem = [int][math]::Ceiling(($n1 * $BlockMin1) - $el) + 2
        if ($rem -gt 0) { Log ("logger faze 1 skoncil predcasne -> restart na " + $rem + " min"); $procL1 = Start-Logger $logF1 $rem }
      }
      if ($procL2 -and $procL2.HasExited -and $curIdx -ge 0 -and $blocks[$curIdx].phase -eq 2) {
        $rem = [int][math]::Ceiling($totalMin - $el) + 2
        if ($rem -gt 0) { Log ("logger faze 2 skoncil predcasne -> restart na " + $rem + " min"); $procL2 = Start-Logger $logF2 $rem }
      }
    }
    if (((Get-Date) - $lastSnap).TotalSeconds -ge $SnapEverySec) { $lastSnap = Get-Date; [void](Take-Snapshot $curName) }
    Start-Sleep -Seconds 10
  }
}
finally {
  Log "KONEC planu (nebo preruseni) -> vracim pristroj do vychoziho stavu"
  [void](Invoke-Uart "gpsdo on;dac dither 100" 1500 25000)
  if ($Phase2Names -match "GM|GS") { [void](Invoke-Uart "gps survey off;gps gnss gps" 1500 15000) }
  [void](Take-Snapshot "konec")
  Start-Sleep -Seconds 120
  foreach ($pr in @($procL1, $procL2, $procRaw)) { if ($pr -and -not $pr.HasExited) { try { Stop-Process -Id $pr.Id -Force } catch { } } }
  Log ("HOTOVO: resetu pristroje " + $resets + ". Data: " + $P + "*")
}
