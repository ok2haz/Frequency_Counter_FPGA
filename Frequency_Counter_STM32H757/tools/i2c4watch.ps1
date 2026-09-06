<#
  i2c4watch.ps1 - hlida I2C4 a v okamziku, kdy sbernice umre, sam vyvola
  `flightrec test` + `flightrec`, aby se zachytilo 60 s HISTORIE PRED poruchou
  (CPU, heap, nejmensi stack, teploty, pocet I2C chyb).

  Duvod: smrt I2C4 je INTERMITENTNI (nekdy za 7 s, nekdy za 136 s, nekdy vubec),
  takze rucne se ten okamzik netrefi. Detekce je pasivni — `temperature` cte
  cache `g_sensors`, NEsaha na sbernici, takze mereni samo vysledek neovlivnuje.

  Pouziti:
    powershell -ExecutionPolicy Bypass -File tools\i2c4watch.ps1 -Port COM8 -Seconds 900
#>
param(
  [string]$Port    = "COM8",
  [int]   $Baud    = 115200,
  [int]   $Seconds = 900,
  [int]   $PollMs  = 2000
)

function Send-Cmd($sp, $cmd, $idleMs = 900, $maxMs = 12000) {
  $sp.DiscardInBuffer()
  $sp.WriteLine($cmd)
  $sb = New-Object System.Text.StringBuilder
  $t0 = Get-Date; $last = Get-Date
  while ($true) {
    Start-Sleep -Milliseconds 50
    $c = $sp.ReadExisting()
    if ($c.Length -gt 0) { [void]$sb.Append($c); $last = Get-Date }
    if ((New-TimeSpan $last (Get-Date)).TotalMilliseconds -ge $idleMs) { break }
    if ((New-TimeSpan $t0   (Get-Date)).TotalMilliseconds -ge $maxMs)  { break }
  }
  return $sb.ToString()
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.NewLine = "`r`n"; $sp.ReadTimeout = 200
try {
  $sp.Open(); Start-Sleep -Milliseconds 150; $sp.DiscardInBuffer()
  $t0 = Get-Date
  $script:nextLog = 0
  Write-Host ("# hlidam I2C4 na {0}, max {1}s" -f $Port, $Seconds)
  while ((New-TimeSpan $t0 (Get-Date)).TotalSeconds -lt $Seconds) {
    # ⚠️ Kriterium je SOUVISLA serie chyb, ne jedno selhane cteni. `temperature`
    # se STALE reaguje uz na jediny vypadek, a ty jsou bezne PRECHODNE (mereno
    # 2026-08-30: err 2 / v rade 0, `scanner` vzapeti nasel vsechna 3 zarizeni).
    # Skutecna smrt = streak roste a uz neklesne, proto se cte `status`.
    $r = Send-Cmd $sp "status" 900 12000
    $el = [math]::Round((New-TimeSpan $t0 (Get-Date)).TotalSeconds, 1)
    $streak = 0
    if ($r -match "v rade (\d+)") { $streak = [int]$Matches[1] }
    if ($streak -gt 20) {
      Write-Host ""
      Write-Host ("!!! I2C4 MRTVA v t={0}s od startu hlidani (souvisla serie {1} chyb)" -f $el, $streak) -ForegroundColor Red
      Write-Host "--- status ---"; Write-Output $r
      Write-Host "--- flightrec test (ulozi 60 s PRED poruchou) ---"
      [void](Send-Cmd $sp "flightrec test" 1500 15000)
      Write-Host "--- flightrec dump ---"; Write-Output (Send-Cmd $sp "flightrec" 2000 20000)
      Write-Host "--- scanner (kdo jeste odpovida na I2C4) ---"
      Write-Output ((Send-Cmd $sp "scanner" 3000 20000) -split "`n" | Where-Object { $_ -notmatch "nic nedela" })
      exit 0
    }
    $cpu = ""; if ($r -match "CPU (\d+)%") { $cpu = $Matches[1] }
    $up  = ""; if ($r -match "uptime (\d+)s") { $up = $Matches[1] }
    $tot = ""; if ($r -match "err (\d+) \(v rade") { $tot = $Matches[1] }
    if ($el -ge $script:nextLog) {
      $script:nextLog = $el + 300      # log 1x/5 min (drive modulo obcas okno preskocilo)
      Write-Host ("  uptime={0}s  CPU={1}%  I2C4 err celkem={2} / v rade={3}" -f $up, $cpu, $tot, $streak)
    }
    Start-Sleep -Milliseconds $PollMs
  }
  Write-Host ("# konec: I2C4 zustala zdrava celych {0}s" -f $Seconds) -ForegroundColor Green
} catch {
  Write-Host ("CHYBA: " + $_.Exception.Message) -ForegroundColor Red; exit 1
} finally {
  if ($sp.IsOpen) { $sp.Close() }
}
