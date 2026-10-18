#!/usr/bin/env bash
# swdread.sh - READ-ONLY cteni pameti MCU pres ST-Link (SWD).
#
# Jen `-r32` (cteni). ZADNY zapis, erase ani flash -> bezpecne povolit Claude.
# Pridej do .claude/settings.json (nebo settings.local.json) do permissions.allow:
#     "Bash(bash tools/swdread.sh:*)"
# Pak smi Claude cist pamet (napr. staticke promenne pri ladeni), ale ne zapisovat.
#
# Pouziti:  bash tools/swdread.sh <addr_hex> [len_hex]
#   napr.   bash tools/swdread.sh 0x2401FC80 0x18
#
# ⚠️ Halt cile na dobu cteni zabije I2C4 (dotyk) do power-cyklu (viz CLAUDE.md).
set -euo pipefail
ADDR="${1:?pouziti: swdread.sh <addr_hex> [len_hex]}"
LEN="${2:-0x4}"
CLI="$(ls "/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe" \
          /c/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.*/tools/bin/STM32_Programmer_CLI.exe \
          2>/dev/null | head -1)"
[ -n "$CLI" ] || { echo "STM32_Programmer_CLI.exe nenalezen" >&2; exit 1; }
exec "$CLI" -c port=SWD mode=HOTPLUG -r32 "$ADDR" "$LEN"
