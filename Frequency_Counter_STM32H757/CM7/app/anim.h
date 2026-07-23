#pragma once
/**
 * @file anim.h
 * @brief Animacni helper: ease-out "dojezd" skalaru k cili.
 *
 * Exponencialni vyhlazeni — `cur` se kazdy tik priblizi k `target` o zlomek
 * `k` zbyvajici vzdalenosti (k~0,25 -> hezky dojezd za ~8-10 snimku pri ~20 Hz
 * kadenci volajiciho). `snap` = prah, pod kterym se cur dorovna presne na
 * target (utne nekonecny asymptoticky chvost a zastavi zbytecne prekreslovani
 * — anim_step vraci 0, kdyz se nic nezmenilo). Bez casove zavislosti (zadny
 * dt/pow) — kadence volajiciho je stabilni (~20 Hz), staci konstantni faktor.
 *
 * Sdileno mezi app_gpsdo.c (okno Animace/demo, Nastaveni-jas, tlacitka) a
 * screen_main.c (hlavni obrazovka — statistiky, digit highlight) — obe jsou
 * "app" vrstva, ale ruzne translation units, proto header-only static inline
 * misto static funkci v jednom .c souboru.
 *
 * g_anim_enabled=0 (globalni vypinac, okno Animace): anim_step okamzite
 * skoci na cil, takze VSECHNY animace postavene na anim_t se chovaji stejne
 * (okamzity skok), aniz by kazde volajici misto muselo kontrolovat flag samo.
 */
#include <stdint.h>

extern volatile uint8_t g_anim_enabled;

typedef struct { float cur, target; } anim_t;

static inline void anim_reset(anim_t *a, float v) { a->cur = a->target = v; }
static inline void anim_set(anim_t *a, float target) { a->target = target; }

static inline int anim_step(anim_t *a, float k, float snap)
{
    if (!g_anim_enabled) {
        if (a->cur == a->target) return 0;
        a->cur = a->target;
        return 1;
    }
    float d = a->target - a->cur;
    if (d <= snap && d >= -snap) {
        if (a->cur == a->target) return 0;
        a->cur = a->target;
        return 1;
    }
    a->cur += d * k;
    return 1;
}
