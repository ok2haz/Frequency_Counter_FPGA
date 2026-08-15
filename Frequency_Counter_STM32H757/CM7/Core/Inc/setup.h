#pragma once
/**
 * @file    setup.h
 * @brief   Uložit/načíst sestavu (instrument setup profily) — N číslovaných slotů
 *          do W25Q SETUP regionu. Klasická funkce laboratorního přístroje
 *          (Save/Recall state 1..N).
 *
 * Snapshot = uživatelská nastavení (jas/téma/jazyk/mute/auto-dim/zóna/UI cfg/efekty/
 * animace + Math/limity `g_meas_cfg`). Datalog stav se ZÁMĚRNĚ neukládá (není to
 * „sestava"). Všech N slotů = JEDEN blob přes `w25q_store` (wear-leveled, CRC,
 * power-safe). Volá se z UiTasku (tlačítka v okně SESTAVY, s_view=33).
 *
 * ⚠️ `setup_load` jen NASTAVÍ globály + `g_sys_cfg_dirty`; téma/jas aplikuje
 * volající (UiTask: `ui_theme_select` + `screen_main_invalidate` + re-render).
 */
#include <stdint.h>
#include <stdbool.h>

#define SETUP_N   8    /* počet slotů (1..N v UI) */

/** Inicializace store (po úspěšném w25q_init — voláno z syscfg_load). */
void    setup_init(void);
/** Bitmaska obsazených slotů (bit i = slot i uložen) — pro UI. */
uint8_t setup_used_mask(void);
/** Snapshot aktuálních g_* nastavení do slotu. @return true = OK. */
bool    setup_save(int slot);
/** Aplikuje uložený slot na g_* (jen nastaví; téma/jas doladí volající). @return true = slot byl obsazen a načten. */
bool    setup_load(int slot);
/** Uvolní slot. @return true = OK. */
bool    setup_erase(int slot);
/** Pure-logic unit test sanitizace slotu (součást UART `selftest`). 1 = PASS. */
int     setup_selftest(void);
