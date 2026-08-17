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

/** Pozadavek na kratky "click" (dotek tlacitka, ~12 ms). Thread-safe: jen
 *  nastavi flag, prehraje ho alarm_tick (defaultTask) — smi volat UiTask.
 *  Mute plati; bezici alarm pattern ma prednost (click se zahodi). */
void alarm_click(void);

/* Pocitadla alarmovych udalosti (okno Alarmy). */
extern volatile unsigned int g_alarm_fpga_lost;   /* pocet ztrat FPGA signalu */
extern volatile unsigned int g_alarm_gps_lost;    /* pocet ztrat GPS locku */
extern volatile unsigned int g_alarm_limit_fail;  /* pocet prechodu PASS->FAIL limitu (#44) */

/** Vynuluje vsechna tri pocitadla (UART "meas reset" + tlacitko v okne Alarmy).
 *  Nesaha na hranove guardy uvnitr alarm_tick (s_*_ever) — ty rizeni, jestli se
 *  ma pripistnout dalsi udalost, ne kolik jich uz bylo; reset citace na tom nic
 *  nemeni. */
void alarm_reset_counters(void);

#endif /* ALARM_H */
