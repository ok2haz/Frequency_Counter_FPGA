<#
  gen_fonts.ps1 - vygeneruje vsech 19 LVGL fontu pro GPSDO UI (mockup v9).
  Nazvy vystupu == externy v theme.h (FONTS_READY blok).

  Predpoklady:
    1) npm i -g lv_font_conv      (https://github.com/lvgl/lv_font_conv)
    2) TTF soubory v .\tools\ttf\ :
         JetBrainsMono-Regular.ttf / -Medium.ttf / -SemiBold.ttf / -Bold.ttf
         Inter-Regular.ttf / Inter-Medium.ttf

  Po dokonceni:
    - .c soubory vzniknou v CM7\Core\Src\ui\fonts\
    - pridej tu slozku do buildu (CubeIDE Source Location / Makefile)
    - v theme.h nastav  #define FONTS_READY 1
#>
$ErrorActionPreference = 'Stop'

# --- konfigurace cest ---
$TTF = Join-Path $PSScriptRoot 'ttf'
$OUT = Join-Path $PSScriptRoot '..\CM7\Core\Src\ui\fonts'

$JB_REG  = Join-Path $TTF 'JetBrainsMono-Regular.ttf'
$JB_MED  = Join-Path $TTF 'JetBrainsMono-Medium.ttf'
$JB_SEMI = Join-Path $TTF 'JetBrainsMono-SemiBold.ttf'
$JB_BOLD = Join-Path $TTF 'JetBrainsMono-Bold.ttf'
$IN_REG  = Join-Path $TTF 'Inter-Regular.ttf'
$IN_MED  = Join-Path $TTF 'Inter-Medium.ttf'

New-Item -ItemType Directory -Force $OUT | Out-Null

# spolecne rozsahy / symboly (kody dle podkladu)
$ASCII = @('-r','0x20-0x7E')
# bezpecne Latin-1 symboly: +- (0xB1) a stredova tecka (0xB7). Superskripty/Greek/
# geometricke (10^n, sigma, tau, RUN sipka, MENU) fonty NEMAJI -> v UI je e-notace + ASCII.
$SAFE  = @('-r','0xB1','-r','0xB7')

function Gen($name, $ttf, $size, $extra, $bpp = 2) {
    if (-not (Test-Path $ttf)) { throw "Chybi TTF: $ttf" }
    $cliArgs = @('--font', $ttf, '--size', $size, '--bpp', "$bpp",
                 '--format', 'lvgl', '--no-compress') + $extra + @('-o', (Join-Path $OUT "$name.c"))
    Write-Host ">> $name  ($size px, bpp $bpp)"
    & lv_font_conv @cliArgs
    if ($LASTEXITCODE -ne 0) { throw "lv_font_conv selhal pro $name" }
}

# === JetBrains Mono === (ASCII + SAFE; e-notace misto superskriptu)
Gen 'jbmono_med_78'  $JB_MED  78 @('-r','0x30-0x39') 4        # hlavni jiste cislice (bpp4 hladke)
Gen 'jbmono_med_48'  $JB_MED  48 @('-r','0x30-0x39') 4        # zasedle cislice pod presnosti (mensi)
Gen 'jbmono_semi_25' $JB_SEMI 25 $ASCII                       # PERIOD/TIME INT/MENU
Gen 'jbmono_bold_25' $JB_BOLD 25 $ASCII                       # tecky + "> RUN"
Gen 'jbmono_reg_24'  $JB_REG  24 $ASCII                       # cas
Gen 'jbmono_reg_21'  $JB_REG  21 $ASCII                       # GATE/CHAN radek 2
Gen 'jbmono_bold_20' $JB_BOLD 20 $ASCII                       # FREQUENCY
Gen 'jbmono_reg_20'  $JB_REG  20 ($ASCII + $SAFE)             # CH B / GATE 1 s + ·
Gen 'jbmono_med_18'  $JB_MED  18 $ASCII                       # pilulky (GNSS/System/9)
Gen 'jbmono_reg_17'  $JB_REG  17 ($ASCII + $SAFE)             # N312/trend (·, +-, e-notace)
Gen 'jbmono_reg_14'  $JB_REG  14 $ASCII                       # pilulky label

# === Inter ===
Gen 'inter_med_20'   $IN_MED  20 ($ASCII + $SAFE)            # nazvy karet + hodnoty (+- u ADEV)
Gen 'inter_reg_16'   $IN_REG  16 $ASCII                       # GATE label
Gen 'inter_reg_14'   $IN_REG  14 ($ASCII + $SAFE)             # datum + ·
Gen 'inter_reg_10'   $IN_REG  10 ($ASCII + $SAFE)             # osy grafu + ·

# lv_font_conv pise #include "lvgl/lvgl.h"; nas include path miri na KOREN lvgl/ -> oprav na "lvgl.h"
$u8 = New-Object System.Text.UTF8Encoding($false)
Get-ChildItem $OUT -Filter *.c | ForEach-Object {
  $t = [System.IO.File]::ReadAllText($_.FullName)
  if ($t.Contains('#include "lvgl/lvgl.h"')) {
    [System.IO.File]::WriteAllText($_.FullName, $t.Replace('#include "lvgl/lvgl.h"','#include "lvgl.h"'), $u8)
  }
}
Write-Host ""
Write-Host "Hotovo: 19 fontu v $OUT (include opraven na lvgl.h)"
Write-Host "Dalsi krok: pridej ui\fonts do buildu + v theme.h nastav FONTS_READY 1"
