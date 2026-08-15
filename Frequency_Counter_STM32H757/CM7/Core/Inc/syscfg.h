/**
 * @file    syscfg.h
 * @brief   Persistence systemoveho + UI nastaveni do W25Q flash (CONFIG region).
 *
 * PROC: BKP registry (DR1/DR2/DR6) prezijou jen warm reset (NRST/SW/WDG), NE plny
 * power-cycle bez VBAT baterie. Uzivatel chce nastaveni persistentni i pres
 * vypnuti -> zrcadlime je do W25Q flash (prezije cokoliv).
 *
 * HYBRID s BKP: BKP zustava jako INSTANT cache (bez wear, warm reset). Flash je
 * druha vrstva prezivajici power-cycle. Pri STUDENEM startu (BKP smazana) je
 * autoritativni flash (syscfg_load ji nacte). Pri WARM resetu ma prednost BKP
 * (uz drzi nejnovejsi) -> syscfg_load flash NEnacte, jen inicializuje store.
 *
 * ZAPIS je DEBOUNCED (syscfg_flash_tick z defaultTask): flash erase+write trva
 * ~desitky-stovky ms a ma wear -> zapisujeme az po ~1,5 s klidu (rychle +/- tapy
 * se slouci do jednoho zapisu, kriticke tasky se neblokuji casto).
 */
#ifndef SYSCFG_H
#define SYSCFG_H

#include <stdbool.h>
#include <stdint.h>

/** Nacte nastaveni z W25Q CONFIG store do g_* globalu — JEN pri studenem startu
 *  (g_syscfg_bkp_valid==0); pri warm resetu ma prednost BKP, jen se inicializuje
 *  store (aby fungoval zapis). Prazdny/nevalidni zaznam -> g_* zustanou na
 *  hodnotach nactenych z BKP / defaultech. Volat JEDNOU pri startu z UiTask
 *  (app_gpsdo_init), PRED prvnim renderem (kvuli tematu/jasu). Blokujici (~ms). */
void syscfg_load(void);

/** Zapise aktualni g_* nastaveni do W25Q CONFIG store (blokujici erase+write).
 *  Interne volano debounced z syscfg_flash_tick; primo netreba. @return true=OK. */
bool syscfg_save(void);

/** Debounced auto-save: hlida zmenu sledovanych g_* (shadow-diff) a po ~1,5 s
 *  klidu zapise do flash. Volat periodicky z defaultTask (~100 Hz). Prvni volani
 *  jen zaznamena baseline (zadny zapis pri bootu). */
void syscfg_flash_tick(void);

#endif /* SYSCFG_H */
