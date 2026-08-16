# post_generate.ps1 — automatická oprava po `Generate Code` z CubeMX
#
# ⚠️ POJISTKA, ne primární řešení. Od 2026-08-16 je HardFault vyřešený rovnou
# v CubeMX: v NVIC je u `HardFault_IRQn` odškrtnuté **"Generate IRQ handler"**
# (`.ioc` klíč `NVIC1.HardFault_IRQn` pole 6 = false — přesně jak to má FreeRTOS
# u PendSV/SysTick), takže CubeMX handler negeneruje a náš `naked` může žít
# v USER CODE 1, kam regen nesahá.
#
# Tenhle skript má smysl jen kdyby to políčko někdo zase zaškrtl (nebo se ztratilo
# při upgradu CubeMX) — pak handler obnoví. Nastavit ho jde v CubeMX:
#   Project Manager -> Project -> "Script (after generation)"  ->  tools\post_generate.bat
# (v `.ioc` se to projeví jako `ProjectManager.UAScriptAfterPath`).
#
# Skript je IDEMPOTENTNÍ: když je handler už naked, jen to oznámí a nic nemění.
# Spustitelný i ručně:  powershell -ExecutionPolicy Bypass -File tools\post_generate.ps1

$ErrorActionPreference = 'Stop'

# Kořen projektu = rodič adresáře, kde leží tenhle skript (nezávislé na CWD,
# protože CubeMX skript spouští z vlastního pracovního adresáře).
$root = Split-Path -Parent $PSScriptRoot
$itFile = Join-Path $root 'CM7\Core\Src\stm32h7xx_it.c'

if (-not (Test-Path $itFile)) {
    Write-Host "post_generate: NENALEZEN $itFile - preskakuji." -ForegroundColor Yellow
    exit 0
}

$text = Get-Content -Raw -Encoding UTF8 $itFile

if ($text -match '__attribute__\(\(naked\)\)\s*void\s+HardFault_Handler') {
    Write-Host "post_generate: HardFault_Handler uz je naked - OK, nic nemenim."
    exit 0
}

# Přesně ta podoba, kterou generuje CubeMX (prázdný handler se dvěma USER CODE bloky).
$generated = @'
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}
'@

$restored = @'
/* !! REGEN-CLOBBER: tuhle naked verzi CubeMX pri Generate Code PREPISE zpet na
 * prazdny `while(1)` (je mimo USER CODE — cela definice funkce se do USER CODE
 * bloku dat neda). Obnovuje ji `tools/post_generate.ps1`, ktery je nastaveny
 * jako "Script (after generation)" v Project Manageru. Helper
 * `hard_fault_capture` je v USER CODE 0 a regen prezije.
 * Historie: handler MUSI byt `naked`, jinak jeho prolog posune MSP a `frame[6]`
 * uz necte exception frame, ale prolog (commit b5f8411). */
__attribute__((naked)) void HardFault_Handler(void)
{
  /* Bez prologu -> `msp`/`psp` ukazuje PRESNE na exception frame.
   * EXC_RETURN bit2 rozlisuje, ktery zasobnik se pouzil. */
  __asm volatile (
    "tst  lr, #4             \n"
    "ite  eq                 \n"
    "mrseq r0, msp           \n"
    "mrsne r0, psp           \n"
    "b    hard_fault_capture \n"
  );
}
'@

# Normalizace konců řádků, ať shoda nezávisí na CRLF/LF.
$needle = $generated -replace "`r`n", "`n"
$hay    = $text      -replace "`r`n", "`n"

if ($hay.Contains($needle)) {
    $hay = $hay.Replace($needle, ($restored -replace "`r`n", "`n"))
    # Zpět na CRLF (soubor je v repu s CRLF jako zbytek generovaneho kodu).
    $out = $hay -replace "`n", "`r`n"
    Set-Content -Path $itFile -Value $out -Encoding UTF8 -NoNewline
    Write-Host "post_generate: HardFault_Handler OBNOVEN (naked + crash black-box)." -ForegroundColor Green
} else {
    Write-Host "post_generate: !! HardFault_Handler neni ani naked, ani v ocekavane generovane podobe." -ForegroundColor Red
    Write-Host "               Zkontroluj ho RUCNE - crash black-box HardFaultu je nefunkcni!" -ForegroundColor Red
    exit 1
}
