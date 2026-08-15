/**
 * @file prim.c
 * @brief ZAMERNE PRAZDNE — zustava jen kvuli build systemu.
 *
 * Obsahovalo `prim_version()` a dvojici `prim_last_error()` /
 * `prim_internal_set_error()`. Chybovy kanal se nikdy nezapojil — libprim hlasi
 * chyby navratovou hodnotou a firmware diagnostiku resi pres UART `status`
 * a crash black-box (BKP_DR3..5). Odstraneno 2026-08-13, viz git historie.
 * Duvod, proc soubor zustava, je stejny jako u `digit_group.c`.
 */

#include <prim/prim.h>
