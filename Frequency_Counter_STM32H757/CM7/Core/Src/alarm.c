/**
 * @file    alarm.c
 * @brief   Zvukovy alarm na SIGNAL_LOST + ztratu GPS locku. Viz alarm.h.
 *
 * Model: alarm_tick() (~100 Hz z defaultTask) sleduje dva stavy a hlida jejich
 * PRECHOD (edge). Ztrata (OK->chyba) spusti pattern 3 kratkych pipnuti, obnoveni
 * (chyba->OK) jedno delsi. Pattern se prehrava neblokujicne pres HAL_GetTick.
 * Globalni mute (g_sound_muted) okamzite umlci vse.
 *
 * Boot bez falesneho alarmu: FPGA link i GPS lock zacinaji jako "jeste nikdy
 * nebyly OK" -> zadna hrana OK->chyba pri startu (alarm az kdyz link/lock ozije
 * a pak spadne).
 */
#include "alarm.h"
#include "beeper.h"
#include "gps.h"
#include "stm32h7xx_hal.h"

/* Sdilene stavy (definice ve freertos.c). */
extern volatile unsigned char g_freq_stale;    /* 1 = FPGA signal lost / mrtvy link */
extern volatile unsigned char g_sound_muted;   /* 1 = globalni mute (Nastaveni) */

/* ── Neblokujici prehravac patternu (sekvence ON/OFF pulzu) ── */
static unsigned char  s_pulses_left;   /* zbyva ON pulzu */
static unsigned short s_on_ms, s_off_ms;
static unsigned char  s_phase;         /* 0 idle, 1 ON, 2 OFF */
static unsigned int   s_phase_until;   /* HAL_GetTick() konce faze */

static void pattern_start(unsigned char pulses, unsigned short on_ms, unsigned short off_ms)
{
    s_pulses_left = pulses;
    s_on_ms = on_ms;
    s_off_ms = off_ms;
    s_phase = 1;
    s_phase_until = HAL_GetTick() + on_ms;
    beeper_set(true);
}

static void pattern_stop(void)
{
    s_phase = 0;
    s_pulses_left = 0;
    beeper_set(false);
}

static void pattern_service(void)
{
    if (s_phase == 0) return;
    unsigned int now = HAL_GetTick();
    if (s_phase == 1) {                      /* ON dobehl? */
        if ((int)(now - s_phase_until) < 0) return;
        beeper_set(false);
        if (--s_pulses_left == 0) { s_phase = 0; return; }
        s_phase = 2;
        s_phase_until = now + s_off_ms;
    } else {                                 /* OFF dobehl? -> dalsi ON */
        if ((int)(now - s_phase_until) < 0) return;
        beeper_set(true);
        s_phase = 1;
        s_phase_until = now + s_on_ms;
    }
}

/* ── Vyhodnoceni stavu + hranove spousteni ── */
static unsigned char s_fpga_bad_prev = 1;    /* boot = predpokladej mrtvy link (zadna OK->bad hrana) */
static unsigned char s_fpga_ever = 0;        /* uz nekdy byl link OK (jinak start tichy) */
static unsigned char s_gps_lock_prev = 0;    /* boot = jeste nikdy zamknuto */
static unsigned char s_gps_ever = 0;         /* uz nekdy byl lock (jinak neresime jeho ztratu) */

void alarm_tick(void)
{
    /* Mute: umlci okamzite (i rozehrany pattern). */
    if (g_sound_muted && (s_phase != 0 || beeper_is_on())) pattern_stop();
    /* Presne casovani pipnuti — kazdy tik (~100 Hz), jen kdyz neni mute. */
    if (!g_sound_muted) pattern_service();

    /* Vyhodnoceni stavu (hrany) jen 5x/s — gps_get kopiruje ~200B v kriticke sekci. */
    static unsigned int last_eval;
    unsigned int now = HAL_GetTick();
    if ((now - last_eval) < 200u) return;
    last_eval = now;

    gps_data_t g; gps_get(&g);
    unsigned char fpga_bad = g_freq_stale ? 1 : 0;
    unsigned char lock = (g.valid || g.fix_mode >= 2) ? 1 : 0;

    if (g_sound_muted) {
        /* drz prev stavy aktualni, aby po odmuteni nepipl na starou (davno proslou) hranu */
        if (!fpga_bad) s_fpga_ever = 1;
        s_fpga_bad_prev = fpga_bad;
        if (lock) s_gps_ever = 1;
        s_gps_lock_prev = lock;
        return;
    }

    /* FPGA signal: prvni link-up po startu je TICHY (s_fpga_ever), pak OK->lost =
     * alarm (3 pipnuti), lost->OK = 1 pipnuti (obnoveni). */
    if (!fpga_bad && s_fpga_bad_prev) {
        if (s_fpga_ever) pattern_start(1, 150, 0);   /* obnoveni (ne prvni link-up) */
        s_fpga_ever = 1;
    } else if (fpga_bad && !s_fpga_bad_prev && s_fpga_ever) {
        pattern_start(3, 80, 80);                    /* ztrata signalu */
    }
    s_fpga_bad_prev = fpga_bad;

    /* GPS lock: ztrata jen kdyz uz nekdy byl (bench bez anteny se nikdy nezamkne). */
    if (lock && !s_gps_lock_prev) {
        if (s_gps_ever) pattern_start(1, 150, 0);   /* re-lock potvrzeni (ne pri prvnim) */
        s_gps_ever = 1;
    } else if (!lock && s_gps_lock_prev && s_gps_ever) {
        pattern_start(2, 120, 120);                 /* ztrata locku */
    }
    s_gps_lock_prev = lock;
}

void alarm_test(void)
{
    pattern_start(2, 100, 100);
}
