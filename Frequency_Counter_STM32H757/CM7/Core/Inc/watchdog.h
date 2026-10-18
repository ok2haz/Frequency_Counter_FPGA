/**
 * @file    watchdog.h
 * @brief   Nezavisly watchdog (IWDG1) s heartbeat kontrolou kritickych tasku.
 *
 * IWDG1 ~4 s. defaultTask ho obnovuje JEN kdyz UiTask i FpgaTask nedavno "koply"
 * (heartbeat) — takze zatuhnuti jednoho z nich (nejen celeho scheduleru) vede na
 * reset. Registrova implementace (HAL_IWDG modul je v hal_conf vypnuty) -> regen-safe.
 * Pri DEBUG buildu se IWDG zmrazi na breakpointu (nezresetuje pri ladeni).
 */
#ifndef WATCHDOG_H
#define WATCHDOG_H

/** Zapne IWDG1 (~4 s). Volat v main.c USER CODE 2 (pred schedulerem). */
void watchdog_init(void);

/** Heartbeat UiTask — volat jednou za smycku UiTasku. */
void watchdog_kick_ui(void);

/** Heartbeat FpgaTask — volat jednou za smycku FpgaTasku. */
void watchdog_kick_fpga(void);

/** defaultTask ~100 Hz: obnovi IWDG jen kdyz oba tasky zily. Jinak necha vyprsit. */
void watchdog_supervise(void);

/* ── Dosazeny stav IWDG (audit F-0104) ──────────────────────────────────────
 * ⚠️ Cte se z REGISTRU po propagaci `PVU`/`RVU`, ne ze zamyslenych konstant.
 * Kdyz se propagace nepovede, zustanou reset defaulty (`PR = 0`, `RLR = 0xFFF`)
 * a watchdog hlida ~0,5 s misto 4 s — tise. Proto to `status` vypisuje. */
unsigned int watchdog_timeout_ms(void);   /* odvozeny timeout z PR/RLR a LSI 32 kHz */
unsigned int watchdog_cfg_pr(void);
unsigned int watchdog_cfg_rlr(void);
int          watchdog_cfg_ok(void);       /* 1 = PR i RLR sedi na zamyslene hodnoty */

/** Kolikrat uz byl detekovan stall, ktery se ale ZOTAVIL (nezpusobil reset).
 *  Bez tohoto pocitadla je takovy stav uplne neviditelny — zaznam v BKP se
 *  pri zotaveni zneplatni, aby se nepripsal pristimu, nesouvisejicimu resetu
 *  (audit F-0105). */
unsigned int watchdog_stall_recovered(void);

#endif /* WATCHDOG_H */
