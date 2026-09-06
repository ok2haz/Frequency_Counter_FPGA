<#
  ltdc_sweep.ps1 - zmeri, jak `d2ddt` (mrtvy cas DMA2D) ovlivnuje PODTECENI
  LTDC FIFO.

  Proc: LTDC cte pixely ze SDRAM pres tutéž sbernici, po ktere DMA2D kresli.
  Kdyz DMA2D zabere prilis velky podil pasma, LTDC nestihne naplnit FIFO a
  snimek se poskodi. `d2ddt` vklada mezi prenosy DMA2D mrtvy cas, cimz pasmo
  vraci LTDC — za cenu pomalejsiho kresleni.

  Merí se PODIL podteceni na snimek (ne absolutni pocet), aby vysledek
  nezavisel na tom, jak rychle se zrovna prekresluje.

  Pouziti:
    powershell -ExecutionPolicy Bypass -File tools\ltdc_sweep.ps1 -Values "0,4,8,16,32,64"
#>
param(
  [string]$Port   = "COM8",
  [int]   $Baud   = 115200,
  [string]$Values = "8",
  [int]   $Settle = 2,      # s po zmene, nez se zacne merit
  [int]   $Window = 6       # s merici okno
)

function Send-Cmd([System.IO.Ports.SerialPort]$sp, [string]$cmd, [int]$idle = 900) {
  $sp.DiscardInBuffer()
  $sp.Write($cmd + "`r")
  $buf = ""
  $last = [Environment]::TickCount
  while (([Environment]::TickCount - $last) -lt $idle) {
    if ($sp.BytesToRead -gt 0) {
      $buf += $sp.ReadExisting()
      $last = [Environment]::TickCount
    } else { Start-Sleep -Milliseconds 30 }
  }
  return $buf
}

function Get-Counters([System.IO.Ports.SerialPort]$sp) {
  $t = Send-Cmd $sp "status" 2000
  $u = 0; $f = 0
  if ($t -match 'podteceni FIFO\s+(\d+)') { $u = [int]$Matches[1] }
  if ($t -match 'flip=(\d+)')             { $f = [int]$Matches[1] }
  return @{ u = $u; f = $f }
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 1500; $sp.NewLine = "`n"
$sp.Open()
Start-Sleep -Milliseconds 300

Write-Host ("{0,-6} {1,10} {2,10} {3,12} {4,12}" -f `
  "d2ddt", "flipu/s", "podt./s", "podt./flip", "verdikt")
Write-Host ("-" * 56)

$best = $null
foreach ($v in ($Values -split ',')) {
  $v = $v.Trim()
  [void](Send-Cmd $sp "d2ddt $v" 800)
  Start-Sleep -Seconds $Settle
  $a = Get-Counters $sp
  Start-Sleep -Seconds $Window
  $b = Get-Counters $sp

  $df = $b.f - $a.f
  $du = $b.u - $a.u
  if ($df -le 0) {
    Write-Host ("{0,-6} {1,10} {2,10} {3,12} {4,12}" -f $v, "-", $du, "n/a", "bez kresleni")
    continue
  }
  $fps  = [math]::Round($df / $Window, 1)
  $ups  = [math]::Round($du / $Window, 1)
  $perf = [math]::Round($du / $df, 3)
  $verd = if ($perf -lt 0.01) { "CISTE" } elseif ($perf -lt 0.2) { "obcas" } else { "KAZDY SNIMEK" }
  Write-Host ("{0,-6} {1,10} {2,10} {3,12} {4,12}" -f $v, $fps, $ups, $perf, $verd)
  if ($null -eq $best -or $perf -lt $best.p) { $best = @{ v = $v; p = $perf; fps = $fps } }
}

# vrat vychozi hodnotu, at deska nezustane v experimentalnim stavu
[void](Send-Cmd $sp "d2ddt 8" 800)
$sp.Close()

if ($best) {
  Write-Host ""
  Write-Host ("nejmene podteceni: d2ddt={0} ({1} na snimek, {2} flipu/s)" -f `
    $best.v, $best.p, $best.fps)
  Write-Host "(d2ddt vraceno na vychozich 8; hodnota nepersistuje)"
}
