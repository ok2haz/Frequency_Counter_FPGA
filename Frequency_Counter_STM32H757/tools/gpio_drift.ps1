<#
  gpio_drift.ps1 - hlida CELOU TRIDU vady, ne jeden pin.

  !!  PROC: 2026-09-06 se dvakrat nezavisle ztratila konfigurace pinu na GPIOG -
  `PG8` (FMC_SDCLK) prisel o `MODER` (cerny displej) a `PG11` (ETH_TX_EN) o
  `AFR` (deska nedostala IP). Na GPIOG sahaji obe jadra a `HAL_GPIO_Init` dela
  neatomicke read-modify-write, takze ztraceny zapis tise vrati cizi pin.
  PG8 jsem tehdy opravil a NEPROHLEDAL zbytek portu - PG11 se proto nasel az
  o kolo pozdeji. Tenhle skript existuje presne proto, aby se to neopakovalo.

  JAK: nepotrebuje tabulku ocekavanych AF. Podpisem vady je ZMENA za behu,
  takze staci porovnat dva snimky konfiguracnich registru. Cokoli se lisi je
  podezrele; pin, ktery se ma menit legitimne (LED, rucne prepinane GPIO), se
  da vyloucit parametrem -Ignore.

  !!  Cteni sondou HALTUJE jadro, coz rozbiji I2C4 az do power-cyklu (CLAUDE.md).
  Pouzivej cilene, ne jako trvaly monitor. Levnejsi bezna cesta je `status` ->
  radek `GPIO HLIDAC` (pocitadlo oprav primo z firmwaru).

    powershell -File tools\gpio_drift.ps1 -Snapshot   # ulozi vychozi stav
    powershell -File tools\gpio_drift.ps1             # porovna se snimkem
#>
param(
  [string]$Cli  = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
  [string]$File = "$PSScriptRoot\gpio_snapshot.txt",
  [switch]$Snapshot,
  [string]$Ignore = ""      # napr. "G7,G14" pro piny, ktere se meni legitimne
)

# GPIOA..GPIOK, krok 0x400. Cteme MODER(+0x00), OTYPER(+0x04), AFR0(+0x20), AFR1(+0x24).
$Ports = @{ 'A'=0x58020000; 'B'=0x58020400; 'C'=0x58020800; 'D'=0x58020C00;
            'E'=0x58021000; 'F'=0x58021400; 'G'=0x58021800; 'H'=0x58021C00;
            'I'=0x58022000; 'J'=0x58022400; 'K'=0x58022800 }

function Read-Reg([uint32]$addr) {
  $o = & $Cli -c port=SWD mode=HOTPLUG -r32 ("0x{0:X8}" -f $addr) 0x4 2>&1 | Out-String
  # !!  Bereme AZ POSLEDNI shodu: banner programatoru obsahuje hexa retezce,
  #    ktere by prvni shodu ukradly (na tom se tenhle skript uz jednou spalil).
  $m = [regex]::Matches($o, '0x[0-9A-Fa-f]{8}\s*:\s*([0-9A-Fa-f]{8})')
  if ($m.Count -eq 0) { return $null }
  return $m[$m.Count - 1].Groups[1].Value.ToUpper()
}

$now = @{}
foreach ($p in ($Ports.Keys | Sort-Object)) {
  $b = $Ports[$p]
  $now["$p.MODER"] = Read-Reg $b
  $now["$p.AFR0"]  = Read-Reg ($b + 0x20)
  $now["$p.AFR1"]  = Read-Reg ($b + 0x24)
}

if ($Snapshot) {
  $now.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" } |
    Set-Content -Path $File -Encoding ascii
  Write-Host ("snimek ulozen: {0} ({1} registru)" -f $File, $now.Count)
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

  # rozeber, KTERE piny se lisi (2 bity/pin u MODER, 4 bity/pin u AFR)
  $va = [Convert]::ToUInt32($a, 16); $vb = [Convert]::ToUInt32($b, 16)
  $port = $k.Substring(0, 1)
  $wide = $k -like '*AFR*'
  $bits = if ($wide) { 4 } else { 2 }
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
else { Write-Host ("DRIFT: {0} pinu zmenilo konfiguraci za behu <== stejna trida jako PG8/PG11" -f $bad) }
exit ([int]($bad -gt 0))
