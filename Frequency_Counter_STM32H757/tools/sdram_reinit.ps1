<#
  sdram_reinit.ps1 - spusti SDRAM inicializacni sekvenci ZNOVU, za behu, sondou.

  ROZHODUJICI EXPERIMENT pro symptom po power resetu cerny displej, po soft
  resetu citelny": kdyz pamet po druhem initu OZIJE, je vada ve STUDENE
  inicializaci (opravitelne firmwarem). Kdyz zustane mrtva, je to elektricka
  vada mezi MCU a cipem.

  Zapisuje se primo do FMC_SDCMR (0x52004150). Kodovani (RM0399 22.9.6):
    [2:0] MODE  1=clock enable, 2=PALL, 3=auto-refresh, 4=load mode
    [3]   CTB2, [4] CTB1        (cilova banka)
    [8:5] NRFS  (pocet auto-refresh - 1)
    [21:9] MRD  (obsah mode registru SDRAM)

  !!  Prodlevy mezi kroky zajistuje uz sama rezie sondy (kazde volani ~100 ms),
  takze pozadavek min. 100 us po clock enable" je splneny s velkou rezervou.
  !!  Halt cile rozbije I2C4 -> po testu POWER-CYKLUS.
#>
param(
  [string]$Cli = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
)

$SDCMR = "0x52004150"
$TEST  = "0xC0000100"          # mimo prvni radek, at to neni nahoda

function Invoke-Prog([string[]]$a) { & $Cli @a 2>&1 | Out-String }

function Read32([string]$addr) {
  $o = Invoke-Prog @("-c", "port=SWD", "mode=HOTPLUG", "-r32", $addr, "0x4")
  if ($o -match '0x[0-9A-Fa-f]{8}\s*:\s*([0-9A-Fa-f]{8})') { return "0x" + $Matches[1].ToUpper() }
  return "?"
}

Write-Host "--- stav PRED reinitem ---"
Write-Host ("  SDSR  = " + (Read32 "0x52004158"))
Write-Host ("  SDRTR = " + (Read32 "0x52004154"))
Write-Host ("  {0} = {1}" -f $TEST, (Read32 $TEST))

Write-Host ""
Write-Host "--- inicializacni sekvence (banka 1) ---"
$steps = @(
  @{ v = "0x00000011"; n = "1) CLK_ENABLE" },
  @{ v = "0x00000012"; n = "2) PALL (precharge all)" },
  @{ v = "0x000000F3"; n = "3) AUTO-REFRESH x8" },
  @{ v = "0x00046014"; n = "4) LOAD MODE (CAS3, BL1, single write)" }
)
foreach ($s in $steps) {
  $o = Invoke-Prog @("-c", "port=SWD", "mode=HOTPLUG", "-w32", $SDCMR, $s.v)
  $err = if ($o -match "Error|error") { "  <== CHYBA ZAPISU" } else { "" }
  Write-Host ("  " + $s.n + $err)
  Start-Sleep -Milliseconds 200
}

# refresh rate znovu (pro jistotu - init sekvence ho nemeni)
[void](Invoke-Prog @("-c", "port=SWD", "mode=HOTPLUG", "-w32", "0x52004154", "0x000002E6"))

Write-Host ""
Write-Host "--- test zapisu PO reinitu ---"
$pat = "0xA5A5C3C3"
$o = Invoke-Prog @("-c", "port=SWD", "mode=HOTPLUG", "-w32", $TEST, $pat)
if ($o -match "Error|error") {
  Write-Host "  zapis pres -w32 odmitnut programatorem (ceka external loader)"
}
$got = Read32 $TEST
Write-Host ("  zapsano {0}, precteno {1}  -> {2}" -f $pat, $got, $(if ($got -eq $pat) { "SDRAM OZILA" } else { "porad mrtva" }))
Write-Host ("  SDSR  = " + (Read32 "0x52004158"))
Write-Host ""
Write-Host "!! po tomto testu udelej POWER-CYKLUS (halt rozbil I2C4)."
