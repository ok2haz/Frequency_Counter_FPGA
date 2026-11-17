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
#include <stddef.h>   /* size_t — parametr `syscfg_storage_text` */

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

/** 1 = CONFIG store ve W25Q je pouzitelny (nastaveni se ma kam ukladat).
 *  0 = `syscfg_load()` se nepodarilo pripravit ulozistě -> `syscfg_save()` bude
 *  vracet false a nastaveni se NIKDY neulozi (audit F-0098). */
int syscfg_store_ready(void);

/** Kolikrat `syscfg_flash_tick()` zkusil ulozistě znovu pripravit (F-0098).
 *  Nenulove = pri bootu to nevyslo a bezi zachrana; cte to `status`. */
uint32_t syscfg_store_retries(void);

/** Souhrn stavu VSECH PETI blob storu ve W25Q do jedne vety, napr.
 *  `"syscfg OK | calib OK | sestavy OK | flightrec OK | errlog OK"`.
 *  @return KOLIK z peti je pripravenych (0..5); 5 = vsechno v poradku.
 *
 *  🔑 JEDEN ZDROJ TEXTU pro vsechny konzumenty (UART `status`, okno System Health).
 *  Vznikla schvalne jako funkce, a ne jako dva nezavisle vypisy — presne timhle
 *  se rozesel `errlog dump` s oknem CHYBY (nalez F-0100, lekce L-0049).
 *  ⚠️ Zije v `syscfg.c`, protoze to je modul perzistence; novy `.c` by se do buildu
 *  nedostal bez `Close -> Open Project` (mechanicke pravidlo CLAUDE.md). */
int syscfg_storage_text(char *buf, size_t n);

/** Kolik z peti blob storu ve W25Q je pripravenych (0..5). Levna varianta
 *  `syscfg_storage_text()` bez skladani retezce — pro SYS pilulku, ktera se
 *  vyhodnocuje casto. 🔑 JEDINY zdroj faktu; nikdo si tu petici necte sam. */
int syscfg_storage_ready_count(void);

#endif /* SYSCFG_H */
