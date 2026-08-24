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
#include "meas_math.h"       /* g_meas_verdict + g_meas_cfg — limit pass/fail alarm (#44) */
#include "sensor_stat.h"     /* g_sensors[] — prahovy monitor VBAT/OCXO */
#include "stm32h7xx_hal.h"

/* Sdilene stavy (definice ve freertos.c). */
extern volatile unsigned char g_freq_stale;    /* 1 = FPGA signal lost / mrtvy link */
extern volatile unsigned char g_sound_muted;   /* 1 = globalni mute (Nastaveni) */

/* Pocitadla udalosti pro okno Alarmy (definice zde, extern v alarm.h). */
volatile unsigned int g_alarm_fpga_lost  = 0;  /* pocet ztrat FPGA signalu */
volatile unsigned int g_alarm_gps_lost   = 0;  /* pocet ztrat GPS locku */
volatile unsigned int g_alarm_limit_fail = 0;  /* pocet prechodu PASS->FAIL limitu (#44) */
volatile unsigned int g_alarm_vbat       = 0;  /* pocet poklesu VBAT pod prah */
volatile unsigned int g_alarm_ocxo       = 0;  /* pocet vybehnuti OCXO z pasma */
volatile unsigned int g_alarm_adev       = 0;  /* pocet prekroceni prahu σy@1s */

/* Prahovy monitor — konfigurace, stav, vstup z app vrstvy. */
mon_cfg_t g_mon_cfg;
volatile uint8_t g_mon_vbat_bad = 0;
volatile uint8_t g_mon_ocxo_bad = 0;
volatile uint8_t g_mon_adev_bad = 0;
volatile float   g_adev_1s      = 0.0f;

void mon_cfg_defaults(mon_cfg_t *c)
{
    if (c == NULL) return;
    /* VBAT: zalozni CR2032 s JMENOVITYMI 3,3 V (tato deska). Prah "vymen brzy"
     * = 2,8 V: ~0,5 V pod nominalem, stale s velkou rezervou nad spodni hranici
     * BKP/RTC domeny (~1,65 V) i nad kolenem CR2032 (~2,7 V). ZAPNUTO.
     * (Drive 2,6 V pri predpokladu 3,0 V nominalu.) */
    c->vbat_en    = 1;
    c->vbat_lo_mv = 2800.0f;
    /* OCXO: zmereno na teto desce 49,7–51,5 °C v ustalenem stavu, takze pasmo
     * 45–55 °C nechava rezervu na jinou okolni teplotu a pritom chyti rozladenou
     * pec. ZAPNUTO. */
    c->ocxo_en    = 1;
    c->ocxo_lo_c  = 45.0f;
    c->ocxo_hi_c  = 55.0f;
    /* ADEV: ⚠️ VYCHOZI VYPNUTO — σy@1s se dnes pocita ze SIMULACE headline
     * (~1e-8), takze jakykoli realisticky prah by pipal na sum. Zapnout az
     * po zprovozneni FPGA linku (#2). Hodnota 1e-9 = rozumny start pro OCXO. */
    c->adev_en    = 0;
    c->adev_max   = 1e-9f;
}

void alarm_reset_counters(void)
{
    g_alarm_fpga_lost = 0;
    g_alarm_gps_lost = 0;
    g_alarm_limit_fail = 0;
    g_alarm_vbat = 0;
    g_alarm_ocxo = 0;
    g_alarm_adev = 0;
}

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
static unsigned char s_meas_fail_prev = 0;   /* limit FAIL v predchozim vyhodnoceni */
static unsigned char s_meas_ever = 0;        /* uz nekdy byl PASS (jinak: zapnuti limitu na spatne hodnote nepipne) */

/* Touch click: UiTask jen nastavi flag, prehraje ho alarm_tick (jeden vlastnik
 * pattern stavu = defaultTask -> zadny cross-task zapis do s_phase). */
static volatile unsigned char s_click_req;
void alarm_click(void) { s_click_req = 1; }

/* ── Prahovy monitor (VBAT / OCXO pasmo / σy@1s) ─────────────────────────────
 * Sticky vyhodnoceni s hysterezi: jakmile je stav "mimo", vrati se do OK az kdyz
 * hodnota zaleze o `hyst` DOVNITR meze. Bez toho by velicina sedici presne na
 * prahu prepinala pri kazdem vyhodnoceni (5x/s) a pipala donekonecna.
 * Jednostranne meze se zadavaji nesmyslne velkou protilehlou hodnotou. */
#define MON_INF  1e30f

static uint8_t band_eval(uint8_t prev_bad, float v, float lo, float hi, float hyst)
{
    if (prev_bad) return (v > lo + hyst && v < hi - hyst) ? 0u : 1u;
    return (v < lo || v > hi) ? 1u : 0u;
}

/* Jedna podminka: hrana OK->mimo pipne a zvedne citac; mimo->OK pipne kratce.
 * `ever` = uz byla nekdy v poradku. Bez toho by trvale spatna hodnota (napr.
 * vybita zaloznі baterie) pipala pri KAZDEM bootu — na displeji ji stejne vidis
 * (SYS pilulka + okno Alarmy), takze zvuk by byl jen otravny. Stejna filozofie
 * jako "start tichy" u FPGA/GPS vyse. */
static void mon_edge(uint8_t bad, volatile uint8_t *state, uint8_t *ever,
                     volatile unsigned int *cnt)
{
    if (!bad) {
        /* ⚠️ Navratove pipnuti JEN kdyz uz drive byl dobry stav (`*ever`) — jinak
         * by kazdy boot pipl. Zmereno na HW 2026-08-18: VBAT ma pri startu
         * transient (min 2516 mV proti ustalenym 2932), tedy POD vychozim prahem
         * 2600 mV. Prvni vyhodnoceni by dalo bad=1, druhe bad=0 -> "navrat do
         * mezi" a pipnuti pri KAZDEM zapnuti pristroje. Stejny guard uz maji
         * FPGA i GPS vetve nize. */
        if (*state && *ever) {                         /* navrat do mezi */
            if (!g_sound_muted) pattern_start(1, 150, 0);
        }
        *ever = 1;
    } else if (!*state && *ever) {                     /* prave vybehlo z meze */
        /* Delsi/pomalejsi pattern nez u FPGA (80 ms) a GPS (120 ms) — prahovy
         * monitor je "neco se pomalu kazi", ne "prave se ztratil signal". */
        if (!g_sound_muted) pattern_start(3, 200, 120);
        (*cnt)++;
    }
    *state = bad;
}

static void mon_eval(void)
{
    static uint8_t s_vbat_ever = 0, s_ocxo_ever = 0, s_adev_ever = 0;

    /* VBAT — jen pri platnem cteni; neplatny senzor NENI duvod k alarmu. */
    if (g_mon_cfg.vbat_en) {
        const sensor_stat_t *b = &g_sensors[SENS_VBAT];
        if (b->samples && b->valid) {
            uint8_t bad = band_eval(g_mon_vbat_bad, b->last,
                                    g_mon_cfg.vbat_lo_mv, MON_INF, 30.0f);
            mon_edge(bad, &g_mon_vbat_bad, &s_vbat_ever, &g_alarm_vbat);
        }
    } else { g_mon_vbat_bad = 0; s_vbat_ever = 0; }

    /* OCXO teplota v pasmu (0x49). */
    if (g_mon_cfg.ocxo_en) {
        const sensor_stat_t *t = &g_sensors[SENS_T49];
        if (t->samples && t->valid) {
            uint8_t bad = band_eval(g_mon_ocxo_bad, t->last,
                                    g_mon_cfg.ocxo_lo_c, g_mon_cfg.ocxo_hi_c, 0.5f);
            mon_edge(bad, &g_mon_ocxo_bad, &s_ocxo_ever, &g_alarm_ocxo);
        }
    } else { g_mon_ocxo_bad = 0; s_ocxo_ever = 0; }

    /* σy@1s (plni app vrstva do g_adev_1s). 0 = jeste neni dost vzorku ->
     * nevyhodnocovat, jinak by "0 < prah" vypadala jako perfektni stabilita. */
    if (g_mon_cfg.adev_en) {
        float a = g_adev_1s;
        if (a > 0.0f) {
            uint8_t bad = band_eval(g_mon_adev_bad, a, -MON_INF,
                                    g_mon_cfg.adev_max, g_mon_cfg.adev_max * 0.1f);
            mon_edge(bad, &g_mon_adev_bad, &s_adev_ever, &g_alarm_adev);
        }
    } else { g_mon_adev_bad = 0; s_adev_ever = 0; }
}

void alarm_tick(void)
{
    /* Mute: umlci okamzite (i rozehrany pattern). */
    if (g_sound_muted && (s_phase != 0 || beeper_is_on())) pattern_stop();
    /* Presne casovani pipnuti — kazdy tik (~100 Hz), jen kdyz neni mute. */
    if (!g_sound_muted) pattern_service();

    /* Touch click (~12 ms tick @800 Hz): jen kdyz nehraje alarm pattern. */
    if (s_click_req) {
        s_click_req = 0;
        if (!g_sound_muted && s_phase == 0) pattern_start(1, 12, 0);
    }

    /* Vyhodnoceni stavu (hrany) jen 5x/s — gps_get kopiruje ~200B v kriticke sekci. */
    static unsigned int last_eval;
    unsigned int now = HAL_GetTick();
    if ((now - last_eval) < 200u) return;
    last_eval = now;

    /* ── Prahovy monitor realnych velicin ────────────────────────────────────
     * ⚠️ Vyhodnocuje se PRED mute vetvi nize a nezavisle na ni: `g_mon_*_bad`
     * cte SYS pilulka a okna, takze musi odrazet skutecnost i pri ztlumenem
     * zvuku. Mute umlcuje POUZE pipnuti. */
    mon_eval();

    gps_data_t g; gps_get(&g);
    unsigned char fpga_bad = g_freq_stale ? 1 : 0;
    unsigned char lock = (g.valid || g.fix_mode >= 2) ? 1 : 0;
    /* Limit pass/fail (#44): aktivni jen kdyz limity i alarm zapnute. FAIL = LO|HI.
     * s_meas_ever se armuje jen skutecnym PASS -> zapnuti limitu na uz spatne
     * hodnote nezpusobi pipnuti (az prechod PASS->FAIL). Vypnuti limitu resetuje. */
    unsigned char meas_active = (g_meas_cfg.limit_en && g_meas_cfg.alarm_en) ? 1 : 0;
    unsigned char meas_fail = (g_meas_verdict == MEAS_LO || g_meas_verdict == MEAS_HI) ? 1 : 0;

    if (g_sound_muted) {
        /* drz prev stavy aktualni, aby po odmuteni nepipl na starou (davno proslou) hranu */
        if (!fpga_bad) s_fpga_ever = 1;
        s_fpga_bad_prev = fpga_bad;
        if (lock) s_gps_ever = 1;
        s_gps_lock_prev = lock;
        if (!meas_active) { s_meas_fail_prev = 0; s_meas_ever = 0; }
        else { if (g_meas_verdict == MEAS_PASS) s_meas_ever = 1; s_meas_fail_prev = meas_fail; }
        return;
    }

    /* FPGA signal: prvni link-up po startu je TICHY (s_fpga_ever), pak OK->lost =
     * alarm (3 pipnuti), lost->OK = 1 pipnuti (obnoveni). */
    if (!fpga_bad && s_fpga_bad_prev) {
        if (s_fpga_ever) pattern_start(1, 150, 0);   /* obnoveni (ne prvni link-up) */
        s_fpga_ever = 1;
    } else if (fpga_bad && !s_fpga_bad_prev && s_fpga_ever) {
        pattern_start(3, 80, 80);                    /* ztrata signalu */
        g_alarm_fpga_lost++;                         /* pocitadlo pro okno Alarmy */
    }
    s_fpga_bad_prev = fpga_bad;

    /* GPS lock: ztrata jen kdyz uz nekdy byl (bench bez anteny se nikdy nezamkne). */
    if (lock && !s_gps_lock_prev) {
        if (s_gps_ever) pattern_start(1, 150, 0);   /* re-lock potvrzeni (ne pri prvnim) */
        s_gps_ever = 1;
    } else if (!lock && s_gps_lock_prev && s_gps_ever) {
        pattern_start(2, 120, 120);                 /* ztrata locku */
        g_alarm_gps_lost++;                         /* pocitadlo pro okno Alarmy */
    }
    s_gps_lock_prev = lock;

    /* Limit pass/fail: PASS->FAIL = 4 pipnuti, FAIL->PASS = 1 (obnoveni). */
    if (!meas_active) {
        s_meas_fail_prev = 0; s_meas_ever = 0;      /* vypnuto -> zadna hrana pri pristim zapnuti */
    } else {
        if (meas_fail && !s_meas_fail_prev && s_meas_ever) {
            pattern_start(4, 70, 70);               /* mimo meze */
            g_alarm_limit_fail++;
        } else if (!meas_fail && s_meas_fail_prev) {
            pattern_start(1, 150, 0);               /* zpet v mezich */
        }
        if (g_meas_verdict == MEAS_PASS) s_meas_ever = 1;   /* arm jen realnym PASS */
        s_meas_fail_prev = meas_fail;
    }
}

void alarm_test(void)
{
    pattern_start(2, 100, 100);
}
