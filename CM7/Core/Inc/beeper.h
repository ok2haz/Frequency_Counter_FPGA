/**
 * @file    beeper.h
 * @brief   Pasivni beeper na PH9, ton 800 Hz generovany TIM7 (toggle @1600 Hz).
 *          Zapnuti/vypnuti pres beeper_set(); stav drzi modul.
 */
#ifndef BEEPER_H
#define BEEPER_H

#include <stdbool.h>

/** Inicializace: PH9 jako vystup + TIM7 (1600 Hz IRQ). Nezapina ton. */
void beeper_init(void);

/** Zapne (true) / vypne (false) ton 800 Hz. */
void beeper_set(bool on);

/** @return true pokud ton hraje. */
bool beeper_is_on(void);

/** Vola se z TIM7 IRQ (HAL_TIM_PeriodElapsedCallback) - prepne PH9. */
void beeper_isr_toggle(void);

#endif /* BEEPER_H */
