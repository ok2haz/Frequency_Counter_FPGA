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

#endif /* WATCHDOG_H */
