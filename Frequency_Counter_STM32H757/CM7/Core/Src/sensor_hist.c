/*
 * sensor_hist.c — kratkodoba historie senzoru v RAM (decimacni pyramida).
 * Viz sensor_hist.h. Idiom prevzat z trend_feed() ve screen_main.c.
 */
#include "sensor_hist.h"
#include <stddef.h>

/* Jedna stage jednoho senzoru — ring + akumulator pro krmeni vyssi stage. */
typedef struct {
    float   ring[SENSOR_HIST_RING];
    int16_t head, count;
    float   acc;
    int16_t acc_n;
} hist_stage_t;

static hist_stage_t s_hist[SENS_COUNT][SENSOR_HIST_STAGES];
static uint8_t      s_div;          /* delic 2 Hz -> BASE_S (500ms × 4 = 2s) */

/* Vlozi jednu hodnotu do pyramidy senzoru (kaskadova decimace jako trend_feed). */
static void hist_push(sensor_id_t id, float v)
{
    for (int s = 0; s < SENSOR_HIST_STAGES; s++) {
        hist_stage_t *sg = &s_hist[id][s];
        sg->ring[sg->head] = v;
        sg->head = (int16_t)((sg->head + 1) % SENSOR_HIST_RING);
        if (sg->count < SENSOR_HIST_RING) sg->count++;
        sg->acc += v;
        if (++sg->acc_n < SENSOR_HIST_DECIM) return;   /* vyssi stage jeste nema co krmit */
        v = sg->acc / (float)SENSOR_HIST_DECIM;
        sg->acc = 0; sg->acc_n = 0;
    }
}

void sensor_hist_feed(void)
{
    /* Feed z 2 Hz sweepu; decimuj na zakladni krok. 500ms × BASE_S/… */
    if (++s_div < (SENSOR_HIST_BASE_S * 2)) return;    /* 2 Hz -> BASE_S sekund */
    s_div = 0;
    for (int id = 0; id < SENS_COUNT; id++) {
        /* Krmime i "stary" last (drzi posledni dobrou hodnotu) — dulezite pro
         * casove zarovnani pyramidy (vsechny stage musi dostat vzorek kazdy krok).
         * Dokud senzor nemel platny vzorek (samples==0), je last=0 -> okno pozna
         * podle count<2 a ukaze "cekam". */
        hist_push((sensor_id_t)id, g_sensors[id].last);
    }
}

static int32_t hist_res(int s)          /* rozliseni stage [s/vzorek] = BASE_S·DECIM^s */
{
    int32_t r = SENSOR_HIST_BASE_S;
    for (int i = 0; i < s; i++) r *= SENSOR_HIST_DECIM;
    return r;
}

/* Nejjemnejsi stage, jehoz rozsah pokryje okno [s]. */
static int hist_pick(int32_t win_s)
{
    for (int s = 0; s < SENSOR_HIST_STAGES; s++)
        if ((int64_t)SENSOR_HIST_RING * hist_res(s) >= (int64_t)win_s) return s;
    return SENSOR_HIST_STAGES - 1;
}

static float hist_at(sensor_id_t id, int s, int age)   /* age 0 = nejnovejsi */
{
    const hist_stage_t *sg = &s_hist[id][s];
    int idx = (sg->head - 1 - age + 2 * SENSOR_HIST_RING) % SENSOR_HIST_RING;
    return sg->ring[idx];
}

int sensor_hist_series(sensor_id_t id, int32_t win_s,
                       float *out, int max_out,
                       float *out_min, float *out_max,
                       int32_t *out_step_s, int32_t *out_span_s)
{
    if (out == NULL || max_out < 2) return 0;

    int st = hist_pick(win_s);
    while (st > 0 && s_hist[id][st].count < 2) st--;   /* fallback na to, co uz mame */
    int have = s_hist[id][st].count;
    if (have < 2) return 0;

    /* Kolik vzorku okno pokryje (od nejnovejsiho zpet), max ring / max_out. */
    int32_t step = hist_res(st);
    int want = (int)((win_s + step - 1) / step) + 1;   /* pokryti win_s */
    if (want > have)    want = have;
    if (want > max_out) want = max_out;
    if (want < 2)       want = 2;

    /* Zapis oldest→newest; min/max pres zapsane. */
    float mn = 1e30f, mx = -1e30f;
    for (int i = 0; i < want; i++) {
        int age = want - 1 - i;                         /* i=0 -> nejstarsi */
        float v = hist_at(id, st, age);
        out[i] = v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    if (out_min)    *out_min = mn;
    if (out_max)    *out_max = mx;
    if (out_step_s) *out_step_s = step;
    if (out_span_s) *out_span_s = (int32_t)(want - 1) * step;
    return want;
}
