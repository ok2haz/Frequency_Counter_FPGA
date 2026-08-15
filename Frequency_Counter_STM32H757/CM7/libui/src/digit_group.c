/**
 * @file digit_group.c
 * @brief ZAMERNE PRAZDNE — zustava jen kvuli build systemu.
 *
 * Typy `ui_digit_segment_t` / `ui_digit_level_t` (digit_group.h) se POUZIVAJI:
 * cifry mereneho kmitoctu s urovnemi jistoty kresli `big_number.c`, ktere je
 * proto tou zivou implementaci. Zdejsi `ui_digit_group_render/width` byly jeho
 * duplikat a nikdo je nikdy nezavolal -> odstraneny 2026-08-13 (v git historii).
 *
 * Soubor se NEMAZE proto, ze odebrani zdrojaku vyzaduje zasah do modelu
 * STM32CubeIDE (jinak zustane v `objects.list` a build spadne) — a usporilo by
 * to 0 B, protoze `--gc-sections` nepouzite funkce stejne zahodi.
 */

#include <ui/digit_group.h>
