/**
 * @file    beeper.h
 * @brief   Pasivni beeper na PH9, ton 800 Hz generovany TIM7 (toggle @1600 Hz).
 *          Zapnuti/vypnuti pres beeper_set(); stav drzi modul.
 */
#ifndef BEEPER_H
#define BEEPER_H

#include <stdbool.h>
#include <stdint.h>

/** Inicializace: PH9 jako vystup + TIM7 (1600 Hz IRQ). Nezapina ton.
 *  @return false = TIM7 se nepodarilo nastavit -> pristroj bude TRVALE NEMY
 *  (vcetne alarmu na ztratu reference). Stav hlasi `beeper_ready()` a `status`;
 *  zamerne se kvuli tomu NEVOLA `Error_Handler` — nemy pipak neni duvod shodit
 *  merici pristroj (audit F-0111). */
bool beeper_init(void);

/** @return true = `beeper_init()` probehl uspesne (jinak je pipak nemy). */
bool beeper_ready(void);

/** Zapne (true) / vypne (false) ton 800 Hz. */
void beeper_set(bool on);

/** Prehraje ton zadane frekvence [Hz] (0 = ticho). Pro melodie.
 *  ⚠️ Kmitocet se CLAMPUJE na `BEEPER_FREQ_MIN_HZ`..`BEEPER_FREQ_MAX_HZ`:
 *  TIM7 ma 16bitovy `ARR`, takze pod ~8 Hz by vypocet `1e6/(2*f)` pretekl
 *  a ton by byl uplne jiny, nez volajici chtel — tise (audit F-0110). */
#define BEEPER_FREQ_MIN_HZ   16u
#define BEEPER_FREQ_MAX_HZ   20000u
void beeper_tone(uint16_t freq_hz);

/** Kratka vzestupna boot melodie (blokujici osDelay; volat 1x z tasku pri startu). */
void beeper_boot_melody(void);

/** @return true pokud ton hraje. */
bool beeper_is_on(void);

/** Vola se z TIM7 IRQ (HAL_TIM_PeriodElapsedCallback) - prepne PH9. */
void beeper_isr_toggle(void);

#endif /* BEEPER_H */
