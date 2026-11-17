# F-0286: zaznam bublin okna, vadnych segmentu a rezervy retezu TDC v zavislosti na teplote FPGA desky.
# Jen cteni pres UART (fpga diag, fpga v3, tdc, sensors, gpsdo) - nic se do pristroje nezapisuje.
# Pouziti (terminal s UART MUSI byt zavreny, COM port je exkluzivni):
#   powershell -ExecutionPolicy Bypass -File tools\f0286_tepl_log.ps1 -Out C:\cesta\tepl.csv -Hours 3
# Faze (ZAKLAD / TEPLO / CHLAZENI) se cte ze souboru -PhaseFile, text lze menit za behu:
#   Set-Content C:\cesta\faze.txt TEPLO
# Postup: 30 min ZAKLAD (kontrolni vetev), pak ohrev FPGA desky o 15-20 C (cidlo TMP117 0x4A),
#         drzet ~60 min, pak CHLAZENI. NEPREKRACOVAT 60 C na cidle 0x4A (znaceno ve sloupci alarm).
param(
  [string]$Port = "COM10",
  [string]$Out = "tepl.csv",
  [string]$PhaseFile = "",
  [double]$Hours = 3,
  [int]$PeriodS = 60,
  [double]$MaxT = 60
)
$uartq = Join-Path $PSScriptRoot "uartq.ps1"
$raw = [System.IO.Path]::ChangeExtension($Out, ".raw.log")
$hdr = "cas;faze;T48;T49;T4A;Tmcu;bubP;bubS;csP;csS;oken;vadneA;vadneB;mezer;tdcRezA;tdcRezB;binA_ps;binB_ps;stav;y_ppb;TIE_ns;alarm"
if (-not (Test-Path $Out)) { $hdr | Out-File -Encoding ascii $Out }
function Pick($text, $rx, $g) {
  $m = [regex]::Match($text, $rx)
  if ($m.Success) { return $m.Groups[$g].Value } else { return "" }
}
$end = (Get-Date).AddHours($Hours)
$n = 0
while ((Get-Date) -lt $end) {
  $t = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
  $o = & powershell -ExecutionPolicy Bypass -File $uartq -Port $Port -Idle 1500 -Quiet -Cmds "fpga diag;fpga v3;tdc;sensors;gpsdo" | Out-String
  $faze = ""
  if ($PhaseFile -ne "" -and (Test-Path $PhaseFile)) { $faze = (Get-Content $PhaseFile -TotalCount 1).Trim() }
  $t4a = Pick $o 'TMP117 0x4A\s+([\d.]+)/' 1
  $alarm = ""
  if ($t4a -ne "" -and [double]$t4a -ge $MaxT) { $alarm = "PREHRATI" }
  $f = @(
    $t, $faze,
    (Pick $o 'TMP117 0x48\s+([\d.]+)/' 1), (Pick $o 'TMP117 0x49\s+([\d.]+)/' 1), $t4a,
    (Pick $o 'MCU jadro\s+([\d.]+)/' 1),
    (Pick $o 'bubliny okna P (\d+) S (\d+)' 1), (Pick $o 'bubliny okna P (\d+) S (\d+)' 2),
    (Pick $o 'znacky pri CS P (\d+) S (\d+)' 1), (Pick $o 'znacky pri CS P (\d+) S (\d+)' 2),
    (Pick $o 'oken (\d+) \(zahozeno' 1),
    (Pick $o 'vadnym segmentem \(z LSQ vyrazen\): A (\d+) B (\d+)' 1),
    (Pick $o 'vadnym segmentem \(z LSQ vyrazen\): A (\d+) B (\d+)' 2),
    (Pick $o 'mezer (\d+)' 1),
    (Pick $o 'rezerva retezu A (\d+) tapu, B (\d+)' 1), (Pick $o 'rezerva retezu A (\d+) tapu, B (\d+)' 2),
    (Pick $o 'TDC A: kodu .*nejvetsi bin \d+ = (\d+) ps' 1), (Pick $o 'TDC B: kodu .*nejvetsi bin \d+ = (\d+) ps' 1),
    (Pick $o 'GPSDO: (HOLD|LOCK|FLL|WAIT|OFF)' 1),
    (Pick $o '\| y ([-+\d.]+) ppb' 1), (Pick $o 'TIE ([-+\d]+) ns' 1),
    $alarm
  )
  ($f -join ";") | Out-File -Append -Encoding ascii $Out
  if ($n % 10 -eq 0) { "##### $t" | Out-File -Append -Encoding ascii $raw; $o | Out-File -Append -Encoding ascii $raw }
  $n++
  Start-Sleep -Seconds $PeriodS
}
