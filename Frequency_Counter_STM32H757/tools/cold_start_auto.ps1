<#
  cold_start_auto.ps1 - automaticky scenar studeneho startu (M3b). Spustit PRED zapnutim desky.
  1) ceka na /api/state a na COM port, 2) hned po naběhnuti `status;tdc;sensors` (BEZ `tdc cal` = boot kalibrace c. 1),
  3) pri uptime >= 60 s a si5356_status != 0: UART `si5356` (re-init) + znovu status, 4) pri uptime ~150 s znovu `status;tdc`.
  Vse do <Out> (surovy text s casovymi znackami). ASCII only.
#>
param(
  [string]$Port = "COM10",
  [string]$Ip = "10.0.0.106",
  [string]$Out = "cold_start_auto.txt",
  [int]$WaitMin = 20,
  [switch]$WaitDown
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$uartq = Join-Path $here "uartq.ps1"

function Log($s) { Add-Content -Path $Out -Encoding ASCII -Value ("[" + (Get-Date -Format "HH:mm:ss.fff") + "] " + $s) }
function State() {
  try { return ((Invoke-WebRequest -Uri ("http://" + $Ip + "/api/state") -UseBasicParsing -TimeoutSec 3).Content | ConvertFrom-Json) } catch { return $null }
}
function Uart($cmds, $idle) {
  Log ("UART >>> " + $cmds)
  $o = & powershell -NoProfile -ExecutionPolicy Bypass -File $uartq -Port $Port -Idle $idle -Max 60000 -Cmds $cmds 2>&1
  Log ("UART <<<`n" + ($o -join "`n"))
}

# -WaitDown: nejdriv pockat, az deska vypadne (ETH nebo COM zmizi), pak teprve armovat - spustit lze za behu desky
if ($WaitDown) {
  Log "cekam na vypnuti desky"
  while ($true) {
    $up = State
    if (-not $up) { break }
    Start-Sleep -Milliseconds 700
  }
  Log "deska vypnuta, armuji"
}
$t0 = Get-Date
$j = $null; $com = $false
while (((Get-Date) - $t0).TotalMinutes -lt $WaitMin) {
  if (-not $j) { $j = State; if ($j) { Log ("ETH odpovida, uptime_s=" + $j.uptime_s + " si5356_status=" + $j.si5356_status) } }
  if (-not $com) { if ([System.IO.Ports.SerialPort]::GetPortNames() -contains $Port) { $com = $true; Log ($Port + " existuje") } }
  if ($j -and $com) { break }
  Start-Sleep -Milliseconds 700
}
if (-not ($j -and $com)) { Log ("TIMEOUT: eth=" + [bool]$j + " com=" + $com); exit 1 }

Start-Sleep -Seconds 2
# prvni zachyceni s opakovanim: COM port se po bootu muze prehlasit (CDC se znovu enumeruje) nebo konzole zatim mlci
for ($try = 1; $try -le 6; $try++) {
  Log ("pokus o prvni zachyceni c. " + $try)
  $o = & powershell -NoProfile -ExecutionPolicy Bypass -File $uartq -Port $Port -Idle 4000 -Max 60000 -Cmds "status;tdc;sensors" 2>&1
  $txt = $o -join "`n"
  Log ("UART <<<`n" + $txt)
  if ($txt -match 'RUNNING') { break }
  Start-Sleep -Seconds 4
}

# cekani na uptime >= 60 s
$s = $null
while ($true) {
  $s = State
  if ($s -and [int]$s.uptime_s -ge 60) { break }
  if (((Get-Date) - $t0).TotalMinutes -gt $WaitMin) { break }
  Start-Sleep -Seconds 2
}
if ($s) {
  Log ("uptime=" + $s.uptime_s + " si5356_status=" + $s.si5356_status + " seq_meas=" + $s.seq_meas + " freq_hz=" + $s.freq_hz)
  if ([int]$s.si5356_status -ne 0) {
    Log "Si5356 NEZAMCENY po 60 s -> UART si5356 (re-init)"
    Uart "si5356" 6000
    Start-Sleep -Seconds 5
    $s2 = State
    if ($s2) { Log ("PO RE-INITU: uptime=" + $s2.uptime_s + " si5356_status=" + $s2.si5356_status + " seq_meas=" + $s2.seq_meas + " freq_hz=" + $s2.freq_hz) }
    Uart "status" 5000
  }
}
# pozdeji znovu
while ($true) {
  $s = State
  if ($s -and [int]$s.uptime_s -ge 150) { break }
  if (((Get-Date) - $t0).TotalMinutes -gt $WaitMin) { break }
  Start-Sleep -Seconds 3
}
Uart "status;tdc" 5000
Log "HOTOVO"
