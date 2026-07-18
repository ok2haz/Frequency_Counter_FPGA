<#
.SYNOPSIS
  Spoji CM7 (bank1 @0x08000000) a CM4 (bank2 @0x08100000) do JEDNOHO Intel HEX
  souboru pro distribuci / flash na jeden zatah.

.DESCRIPTION
  STM32H757 je dual-core: displej + logika bezi na CM7, ale CM7 na startu ceka na
  boot CM4 (D2 handshake). Kdyz se naflashuje jen jedna banka, displej zustane cerny
  (viz CONTRIBUTING.md paragraf 7). Tento skript vyrobi combined image, aby slo
  naflashovat obe jadra jednim souborem v STM32CubeProgrammer (Program) -- zadne
  rucni skladani dvou .elf a zadna zamena, ktere banka kam patri.

  Postup:
    1) najde CM7 a CM4 build artefakty (.hex preferovane; jinak .elf -> objcopy),
    2) sanity-check adresnich rozsahu (CM7 < 0x08100000, CM4 >= 0x08100000),
    3) spoji je (odstrani EOF record prvniho souboru) -> release/gpsdo_combined.hex.

.PARAMETER Config
  "Debug" (default) nebo "Release" -- ktera build slozka se pouzije.

.PARAMETER Root
  Korenova slozka STM32 projektu. Default = ..\Frequency_Counter_STM32H757
  relativne od umisteni skriptu.

.EXAMPLE
  powershell -File tools\make_release_image.ps1 -Config Release
#>
[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")] [string]$Config = "Debug",
    [string]$Root
)

$ErrorActionPreference = "Stop"

# --- Cesty --------------------------------------------------------------------
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Split-Path -Parent $scriptDir
if (-not $Root) { $Root = Join-Path $repoRoot "Frequency_Counter_STM32H757" }
if (-not (Test-Path $Root)) { throw "STM32 projekt nenalezen: $Root" }

$outDir = Join-Path $repoRoot "release"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outHex = Join-Path $outDir "gpsdo_combined.hex"

# --- Najdi objcopy (pro pripad, ze existuji jen .elf) -------------------------
function Find-Objcopy {
    $cmd = Get-Command arm-none-eabi-objcopy -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    # CLAUDE.md: toolchain je na disku pod C:\ST\STM32CubeIDE_*
    $hit = Get-ChildItem "C:\ST\STM32CubeIDE_*" -Recurse -Filter "arm-none-eabi-objcopy.exe" `
             -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

# --- Ziskej .hex pro dane jadro (CM7 / CM4) -----------------------------------
function Get-CoreHex {
    param([string]$Core)   # "CM7" nebo "CM4"

    $buildDir = Join-Path $Root (Join-Path $Core $Config)
    if (-not (Test-Path $buildDir)) {
        throw "Build slozka chybi: $buildDir  (postav $Core v CubeIDE, config $Config)"
    }

    # 1) Uz existujici .hex (CubeIDE post-build)?
    $hex = Get-ChildItem $buildDir -Filter "*.hex" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($hex) {
        Write-Host "[$Core] nalezen HEX: $($hex.FullName)"
        return $hex.FullName
    }

    # 2) Jinak konvertuj .elf -> .hex
    $elf = Get-ChildItem $buildDir -Filter "*.elf" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $elf) { throw "[$Core] ani .hex ani .elf v $buildDir -- nejdriv build v CubeIDE." }

    $objcopy = Find-Objcopy
    if (-not $objcopy) {
        throw ("[$Core] mam jen .elf ($($elf.Name)), ale arm-none-eabi-objcopy nenalezen. " +
               "Zapni v CubeIDE post-build 'Convert to Intel Hex', nebo pridej objcopy do PATH.")
    }
    $genHex = Join-Path $buildDir ($elf.BaseName + ".hex")
    Write-Host "[$Core] konvertuji .elf -> .hex pres objcopy..."
    & $objcopy -O ihex $elf.FullName $genHex
    if ($LASTEXITCODE -ne 0) { throw "[$Core] objcopy selhal (exit $LASTEXITCODE)" }
    return $genHex
}

# --- Zjisti min/max absolutni adresu v Intel HEX (kvuli sanity check) ---------
# Sleduje Extended Linear Address (typ 04) + data recordy (typ 00).
function Get-HexAddrRange {
    param([string]$Path)
    $upper = 0
    $min = [uint64]::MaxValue
    $max = [uint64]0
    foreach ($line in [System.IO.File]::ReadLines($Path)) {
        if (-not $line.StartsWith(":")) { continue }
        $len  = [Convert]::ToInt32($line.Substring(1, 2), 16)
        $off  = [Convert]::ToInt32($line.Substring(3, 4), 16)
        $type = [Convert]::ToInt32($line.Substring(7, 2), 16)
        if ($type -eq 4) {
            $upper = [Convert]::ToInt32($line.Substring(9, 4), 16)
        }
        elseif ($type -eq 0) {
            $addr = ([uint64]$upper -shl 16) -bor [uint64]$off
            if ($addr -lt $min) { $min = $addr }
            $end = $addr + [uint64]$len - 1
            if ($end -gt $max) { $max = $end }
        }
    }
    return [pscustomobject]@{ Min = $min; Max = $max }
}

# --- Hlavni ------------------------------------------------------------------
Write-Host "== make_release_image ($Config) ==" -ForegroundColor Cyan

$cm7Hex = Get-CoreHex -Core "CM7"
$cm4Hex = Get-CoreHex -Core "CM4"

$cm7Range = Get-HexAddrRange $cm7Hex
$cm4Range = Get-HexAddrRange $cm4Hex
$BANK2 = [uint64]0x08100000

Write-Host ("[CM7] adresy 0x{0:X8}..0x{1:X8}" -f $cm7Range.Min, $cm7Range.Max)
Write-Host ("[CM4] adresy 0x{0:X8}..0x{1:X8}" -f $cm4Range.Min, $cm4Range.Max)

# Sanity: CM7 patri do bank1 (< 0x08100000), CM4 do bank2 (>= 0x08100000).
if ($cm7Range.Max -ge $BANK2) {
    throw ("SANITY: CM7 image presahuje do bank2 (0x{0:X8}) -- prohozene jadra?" -f $cm7Range.Max)
}
if ($cm4Range.Min -lt $BANK2) {
    throw ("SANITY: CM4 image lezi pod 0x08100000 (0x{0:X8}) -- prohozene jadra / spatny .elf?" -f $cm4Range.Min)
}

# Spojeni: CM7 bez zaveracneho EOF (:00000001FF) + cely CM4 (ma svuj EOF).
$cm7Lines = [System.IO.File]::ReadAllLines($cm7Hex) |
            Where-Object { $_ -notmatch '^:00000001FF\s*$' }
$cm4Lines = [System.IO.File]::ReadAllLines($cm4Hex)

Set-Content -Path $outHex -Value ($cm7Lines + $cm4Lines) -Encoding ascii

Write-Host ""
Write-Host "OK -> $outHex" -ForegroundColor Green
Write-Host "Flash: STM32CubeProgrammer -> Open file -> Program (adresy jsou v HEX)."
Write-Host "Nezapomen option bytes: BCM7=1 a BCM4=1."
