/**
 * @file    alarm.h
 * @brief   Zvukovy alarm (beeper) na kriticke udalosti: ztrata FPGA signalu
 *          (SIGNAL_LOST / mrtvy link) a ztrata GPS locku.
 *
 * Hranove spouseni (jen na PRECHODU OK->chyba, zadny 1 Hz spam), non-blocking
 * pattern prehravany z alarm_tick(). Ztrata = 3 pipnuti, obnoveni = 1 pipnuti.
 * Respektuje globalni mute (g_sound_muted) z Nastaveni. Vola defaultTask ~100 Hz.
 */
#ifndef ALARM_H
#define ALARM_H

#include <stdbool.h>

/** Periodicky tik (~100 Hz z defaultTask): vyhodnoti stavy + prehraje pattern. */
void alarm_tick(void);

/** Spusti testovaci pipnuti (UART "beep test"). Mute plati i pro test
 *  (alarm_tick ho okamzite umlci) — UART odpoved na to upozorni. */
void alarm_test(void);

#endif /* ALARM_H */
