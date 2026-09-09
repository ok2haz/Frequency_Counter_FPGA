<#
  probe_test.ps1 - ZABIJI CTENI LADICI SONDOU I2C4? Verze s KONTROLNI VETVI.

  !! PROC PREPSANO (2026-09-06): puvodni verze delala jen "zdrava sbernice ->
  N cteni sondou -> sleduj chyby" a z toho se v CLAUDE.md stal ZAKAZ sondy.
  Jenze NIKDY nezmerila, co udela zdrava sbernice ponechana stejnou dobu
  O SAMOTE - a bezela prave v den, kdy sbernice hynula sama po 7 s / 136 s /
  ~2016 s. Soubeh byl uplny, takze z toho mereni neplynulo nic. (SKILL 6o)

  JAK TO DELA TEDA SPRAVNE:
    Faze 0  baseline    - overi pres UART, ze sbernice ZIJE (err 0, v rade 0)
    Faze 1  KONTROLA    - stejne dlouhy usek BEZ JEDINEHO HALTU, vzorkuje err
    Faze 2  ZASAH       - stupnovane davky haltu (1, 3, 10, 33) a po kazde odecet
  Teprve ROZDIL mezi fazi 1 a 2 neco znamena. Kdyz sbernice padne uz ve fazi 1,
  sonda je ocistena a vada je jinde.

  33 = kolik pripojeni delal stary `gpio_drift.ps1`, tedy davka, po ktere
  sbernice minule umrela. Kdyz nepadne ani po ni, je sonda ocistena.

  !! CENA: kdyz sbernice padne, dotyk a TMP117 0x48 jsou mrtve az do
  POWER-CYKLU. Test spoustej jen kdyz na nej mas desku k dispozici.

    powershell -ExecutionPolicy Bypass -File tools\probe_test.ps1
#>
param(
  [string]$Port    = "COM8",
  [int]   $Baud    = 115200,
  [int]   $Control = 300,                 # s kontrolni faze (bez sondy)
  [string]$Bursts  = "1,3,10,33",         # stupnovane davky haltu
  [string]$Cli     = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
)

function Ask($sp, [string]$cmd, [int]$idle = 900, [int]$max = 12000) {
  $sp.DiscardInBuffer(); $sp.Write($cmd + "`r")
  $sb = New-Object System.Text.StringBuilder
  $t0 = Get-Date; $last = Get-Date
  while ($true) {
    Start-Sleep -Milliseconds 50
    $c = $sp.ReadExisting()
    if ($c.Length -gt 0) { [void]$sb.Append($c); $last = Get-Date }
    if ((New-TimeSpan $last (Get-Date)).TotalMilliseconds -ge $idle) { break }
    if ((New-TimeSpan $t0 (Get-Date)).TotalMilliseconds -ge $max) { break }
  }
  return $sb.ToString()
}

# Vrati @(err_celkem, v_rade, uptime_s). -1 = neprecteno.
function Probe-State($sp) {
  $r = Ask $sp "status" 900 12000
  $e = -1; $s = -1; $u = -1
  if ($r -match "err (\d+) \(v rade (\d+)\)") { $e = [int]$Matches[1]; $s = [int]$Matches[2] }
  if ($r -match "uptime (\d+)s")              { $u = [int]$Matches[1] }
  return @($e, $s, $u)
}

function Halt-Once {
  # jedno pripojeni = jeden HALT jadra; ctena adresa je lhostejna
  & $Cli -q -c port=SWD mode=HOTPLUG -r32 0x24000000 1 2>&1 | Out-Null
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200
$sp.Open(); Start-Sleep -Milliseconds 300; $sp.DiscardInBuffer()

try {
  # ---- Faze 0: baseline -----------------------------------------------------
  $b = Probe-State $sp
  Write-Host ("BASELINE   uptime={0}s  err={1}  v rade={2}" -f $b[2], $b[0], $b[1])
  if ($b[0] -lt 0) { Write-Host "UART neodpovida - konec."; exit 2 }
  if ($b[1] -gt 5) { Write-Host "sbernice uz je mrtva -> power-cyklus a spust znovu."; exit 2 }

  # ---- Faze 1: KONTROLA (zadny halt) ---------------------------------------
  Write-Host ""
  Write-Host ("=== FAZE 1: KONTROLA - {0} s BEZ jedineho haltu ===" -f $Control)
  $step = 60
  $elapsed = 0
  $ctlBad = $false
  while ($elapsed -lt $Control) {
    $w = [Math]::Min($step, $Control - $elapsed)
    Start-Sleep -Seconds $w
    $elapsed += $w
    $c = Probe-State $sp
    Write-Host ("  +{0,4}s  err={1}  v rade={2}" -f $elapsed, $c[0], $c[1])
    if ($c[1] -gt 5) { $ctlBad = $true; break }
  }
  if ($ctlBad) {
    Write-Host ""
    Write-Host "ZAVER: sbernice padla BEZ SONDY -> sonda je OCISTENA, vada je jinde."
    exit 0
  }
  Write-Host ("  kontrolni faze CISTA ({0} s bez chyb)" -f $Control)

  # ---- Faze 2: ZASAH (stupnovane davky haltu) ------------------------------
  Write-Host ""
  Write-Host "=== FAZE 2: ZASAH - stupnovane davky haltu sondou ==="
  $total = 0
  foreach ($bn in ($Bursts -split ',')) {
    $n = [int]$bn.Trim()
    for ($i = 0; $i -lt $n; $i++) { Halt-Once }
    $total += $n
    Start-Sleep -Seconds 3
    $a = Probe-State $sp
    Write-Host ("  davka {0,3} haltu (celkem {1,3})  err={2}  v rade={3}" -f $n, $total, $a[0], $a[1])
    if ($a[1] -gt 5) {
      Write-Host ""
      Write-Host ("ZAVER: sbernice padla po {0} haltech, zatimco {1} s BEZ sondy prezila." -f $total, $Control)
      Write-Host "       -> sonda je USVEDCENA. Nutny power-cyklus."
      exit 1
    }
  }

  Write-Host ""
  Write-Host ("ZAVER: {0} haltu sondou a sbernice ZIJE (err v rade 0)." -f $total)
  Write-Host "       -> sonda je OCISTENA; zakaz v CLAUDE.md byl neopodstatneny."
  exit 0
}
finally {
  if ($sp.IsOpen) { $sp.Close() }
}
