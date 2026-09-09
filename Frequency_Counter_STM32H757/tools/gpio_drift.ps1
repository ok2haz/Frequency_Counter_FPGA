<#
  gpio_drift.ps1 - hlida CELOU TRIDU vady, ne jeden pin.

  !! PROC: 2026-09-06 se dvakrat nezavisle ztratila konfigurace pinu na GPIOG -
  PG8 (FMC_SDCLK) prisel o MODER (cerny displej) a PG11 (ETH_TX_EN) o AFR
  (deska nedostala IP). Na GPIOG sahaji obe jadra a HAL_GPIO_Init dela
  neatomicke read-modify-write, takze ztraceny zapis tise vrati cizi pin.
  PG8 jsem tehdy opravil a NEPROHLEDAL zbytek portu - PG11 se proto nasel az
  o kolo pozdeji. Tenhle skript existuje presne proto, aby se to neopakovalo.

  JAK: nepotrebuje tabulku ocekavanych AF. Podpisem vady je ZMENA za behu,
  takze staci porovnat dva snimky konfiguracnich registru. Cokoli se lisi je
  podezrele; pin, ktery se ma menit legitimne, vyloucis pres -Ignore.

  !!! CENA: cteni sondou HALTUJE jadro a halt rozbiji I2C4 az do POWER-CYKLU
  (CLAUDE.md: uz 3 cteni staci na mrtvou sbernici). Prvni verze tohohle
  skriptu delala 33 samostatnych pripojeni a spolehlive tim shodila dotyk -
  nastroj urceny k hledani vad si vyrabel vlastni vadu. Ted se cte VSE JEDNIM
  pripojenim, ale porad plati: pouzivej cilene, ne jako monitor.
  Levna bezna cesta bez jakehokoli haltu je `status` -> radek `GPIO HLIDAC`
  (pocitadlo oprav primo z firmwaru).

    powershell -File tools\gpio_drift.ps1 -Snapshot   # ulozi vychozi stav
    powershell -File tools\gpio_drift.ps1             # porovna se snimkem
#>
param(
  [string]$Cli  = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
  [string]$File = "$PSScriptRoot\gpio_snapshot.txt",
  [switch]$Snapshot,
  [string]$Ignore = ""
)

# GPIOA..GPIOK, krok 0x400.
$Ports = [ordered]@{ 'A'=0x58020000; 'B'=0x58020400; 'C'=0x58020800; 'D'=0x58020C00;
                     'E'=0x58021000; 'F'=0x58021400; 'G'=0x58021800; 'H'=0x58021C00;
                     'I'=0x58022000; 'J'=0x58022400; 'K'=0x58022800 }
# nazev, offset, bitu na pin
$Regs = @( @('MODER',0x00,2), @('OTYPER',0x04,1), @('AFR0',0x20,4), @('AFR1',0x24,4) )

function Read-All {
  $cliArgs = @("-c", "port=SWD", "mode=HOTPLUG")
  $order = @()
  foreach ($p in $Ports.Keys) {
    foreach ($r in $Regs) {
      $cliArgs += @("-r32", ("0x{0:X8}" -f ($Ports[$p] + $r[1])), "0x4")
      $order   += "$p.$($r[0])"
    }
  }
  $o = & $Cli @cliArgs 2>&1 | Out-String
  # !! Jen radky ve tvaru "0xADRESA : HODNOTA" na ZACATKU radku - banner
  # programatoru obsahuje hexa retezce, ktere by jinak shodu ukradly.
  $m = [regex]::Matches($o, '(?m)^0x([0-9A-Fa-f]{8})\s*:\s*([0-9A-Fa-f]{8})')
  $res = [ordered]@{}
  for ($i = 0; $i -lt $order.Count -and $i -lt $m.Count; $i++) {
    $res[$order[$i]] = $m[$i].Groups[2].Value.ToUpper()
  }
  return $res
}

$now = Read-All
if ($now.Count -eq 0) { Write-Host "sonda nic neprecetla - je deska pripojena?"; exit 2 }

if ($Snapshot) {
  $now.GetEnumerator() | ForEach-Object { "$($_.Name)=$($_.Value)" } |
    Set-Content -Path $File -Encoding ascii
  Write-Host ("snimek ulozen: {0} ({1} registru, 1 pripojeni sondou)" -f $File, $now.Count)
  exit 0
}

if (-not (Test-Path $File)) {
  Write-Host "chybi vychozi snimek - spust nejdriv s -Snapshot"
  exit 2
}

$ref = @{}
Get-Content $File | ForEach-Object {
  if ($_ -match '^(.+?)=(.*)$') { $ref[$Matches[1]] = $Matches[2] }
}

$skip = @($Ignore -split ',' | ForEach-Object { $_.Trim().ToUpper() } | Where-Object { $_ })
$bad = 0
foreach ($k in ($ref.Keys | Sort-Object)) {
  $a = $ref[$k]; $b = $now[$k]
  if ($null -eq $b) { Write-Host ("{0,-10} necteno" -f $k); continue }
  if ($a -eq $b) { continue }

  $va = [Convert]::ToUInt32($a, 16); $vb = [Convert]::ToUInt32($b, 16)
  $port = $k.Substring(0, 1)
  $bits = if ($k -like '*AFR*') { 4 } elseif ($k -like '*OTYPER*') { 1 } else { 2 }
  $base = if ($k -like '*AFR1*') { 8 } else { 0 }
  $pins = @()
  for ($i = 0; $i -lt (32 / $bits); $i++) {
    $mask = ([uint32]((1 -shl $bits) - 1)) -shl ($i * $bits)
    if (($va -band $mask) -ne ($vb -band $mask)) {
      $pin = "$port$($base + $i)"
      if ($skip -notcontains $pin.ToUpper()) { $pins += $pin }
    }
  }
  if ($pins.Count -eq 0) { continue }
  Write-Host ("{0,-10} {1} -> {2}   ZMENILY SE PINY: {3}" -f $k, $a, $b, ($pins -join ', '))
  $bad += $pins.Count
}

Write-Host ""
if ($bad -eq 0) { Write-Host "konfigurace GPIO se nezmenila" }
else { Write-Host ("DRIFT: {0} pinu zmenilo konfiguraci za behu - stejna trida jako PG8/PG11" -f $bad) }
exit ([int]($bad -gt 0))
