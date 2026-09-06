<#
  ltdc_knee.ps1 - najde NEJMENSI `d2ddt`, pri kterem uz LTDC nepodteka,
  a zaroven zmeri, co to stoji na rychlosti kresleni.

  Proc dve veliciny: `d2ddt` vraci pasmo LTDC tim, ze zpomaluje DMA2D. Samotne
  "podteceni = 0" je proto bezcenne, kdyz u toho kresleni znatelne zpomali -
  meritkem musi byt OBOJI.

  Meri se pri VYNUCENEM plnem prekresleni (`ui`), protoze prave to uzivatel
  vidi jako probliknuti; staticka obrazovka nic nevypovi.
#>
param(
  [string]$Port   = "COM8",
  [int]   $Baud   = 115200,
  [string]$Values = "0,64,128,160,192,224,255",
  [int]   $Redraws = 6
)

function Send-Cmd([System.IO.Ports.SerialPort]$sp, [string]$cmd, [int]$idle = 700) {
  $sp.DiscardInBuffer(); $sp.Write($cmd + "`r")
  $buf = ""; $last = [Environment]::TickCount
  while (([Environment]::TickCount - $last) -lt $idle) {
    if ($sp.BytesToRead -gt 0) { $buf += $sp.ReadExisting(); $last = [Environment]::TickCount }
    else { Start-Sleep -Milliseconds 20 }
  }
  return $buf
}
function Get-Cnt([System.IO.Ports.SerialPort]$sp) {
  $t = Send-Cmd $sp "status" 2000
  $u = 0; $f = 0
  if ($t -match 'podteceni FIFO\s+(\d+)') { $u = [int]$Matches[1] }
  if ($t -match 'flip=(\d+)')             { $f = [int]$Matches[1] }
  return @{ u = $u; f = $f }
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 1500; $sp.Open(); Start-Sleep -Milliseconds 300

Write-Host ("{0,-6} {1,10} {2,12} {3,12} {4,14}" -f "d2ddt", "flipu", "podteceni", "podt./flip", "ms/prekresleni")
Write-Host ("-" * 60)

$rows = @()
foreach ($v in ($Values -split ',')) {
  $v = $v.Trim()
  [void](Send-Cmd $sp "d2ddt $v" 600)
  Start-Sleep -Milliseconds 800
  $a = Get-Cnt $sp
  $t0 = [Environment]::TickCount
  for ($i = 0; $i -lt $Redraws; $i++) { [void](Send-Cmd $sp "ui" 500) }
  $ms = [Environment]::TickCount - $t0
  Start-Sleep -Milliseconds 500
  $b = Get-Cnt $sp

  $df = $b.f - $a.f
  $du = $b.u - $a.u
  $perf = if ($df -gt 0) { [math]::Round($du / $df, 3) } else { "n/a" }
  $per  = [math]::Round($ms / $Redraws, 0)
  Write-Host ("{0,-6} {1,10} {2,12} {3,12} {4,14}" -f $v, $df, $du, $perf, $per)
  $rows += @{ v = $v; du = $du; df = $df; ms = $per }
}

[void](Send-Cmd $sp "d2ddt 8" 600)
$sp.Close()

$clean = $rows | Where-Object { $_.du -eq 0 } | Select-Object -First 1
Write-Host ""
if ($clean) {
  Write-Host ("nejmensi d2ddt bez podteceni: {0}  ({1} ms na prekresleni)" -f $clean.v, $clean.ms)
} else {
  Write-Host "zadna testovana hodnota nedala nulu - podteceni neni (jen) o pasmu DMA2D"
}
Write-Host "(d2ddt vraceno na 8; hodnota nepersistuje)"
