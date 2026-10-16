<#
  cold_start_log.ps1 - zaznamnik studeneho startu: GET http://<ip>/api/state po -Every s, CSV + surove JSON.
  Odpovida na: kdy (uptime) se Si5356 zamkne / nezamkne, jak se vyviji OCXO teplota a Vc, kdy zacne merit (seq_meas).
  Pred zapnutim desky spustit; dokud deska neodpovida, zapisuje radky s NA. Konec: Ctrl+C nebo -MaxMin.
  Pouziti:
    powershell -ExecutionPolicy Bypass -File tools\cold_start_log.ps1 -Out docs\audit\data\2026-10-15_M3b_studeny.csv -Every 5 -MaxMin 30
  Surove JSON jde do <Out>.raw.jsonl. ASCII only (PowerShell 5.1 cte skript bez BOM jako ANSI).
#>
param(
  [string]$Out = "cold_start.csv",
  [string]$Ip = "10.0.0.106",
  [int]$Every = 5,
  [int]$MaxMin = 30
)
$raw = "$Out.raw.jsonl"
# Sloupce za `ok` nese FW s IPC v23 (2026-10-15): rezerva TDC retezu z posledni kalibrace a pocet automatickych
# re-initu Si5356 (#295/#297). Starsi FW je nema -> "null".
$extra = @("temp_fpga_c","tdc_rez_n","tdc_rez_a","tdc_rez_b","tdc_lvl_a","tdc_lvl_b","tdc_ovf_a","tdc_ovf_b","si_reinit")
if (-not (Test-Path $Out)) {
  Set-Content -Path $Out -Encoding ASCII -Value ("wall,uptime_s,si5356_status,seq_meas,freq_hz,temp_ocxo_c,temp_board_c,vc_mv,ok," + ($extra -join ","))
}
function V($j, $n) { if ($j -and $j.PSObject.Properties[$n] -and $null -ne $j.$n) { return [string]$j.$n } else { return "null" } }
$t0 = Get-Date
$last = ""
while (((Get-Date) - $t0).TotalMinutes -lt $MaxMin) {
  $j = $null; $ok = 0; $txt = ""
  try {
    $txt = (Invoke-WebRequest -Uri ("http://" + $Ip + "/api/state") -UseBasicParsing -TimeoutSec 4).Content
    $j = $txt | ConvertFrom-Json
    $ok = 1
  } catch { }
  $row = (Get-Date -Format s) + "," + (V $j "uptime_s") + "," + (V $j "si5356_status") + "," + (V $j "seq_meas") + "," +
         (V $j "freq_hz") + "," + (V $j "temp_ocxo_c") + "," + (V $j "temp_board_c") + "," + (V $j "vc_mv") + "," + $ok
  foreach ($k in $extra) { $row += "," + (V $j $k) }
  Add-Content -Path $Out -Encoding ASCII -Value $row
  if ($ok) { Add-Content -Path $raw -Encoding ASCII -Value $txt }
  Write-Host $row
  Start-Sleep -Seconds $Every
}
Write-Host "HOTOVO"
