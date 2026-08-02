/*
 * sensor_hist.h — kratkodoba historie senzoru v RAM (STATUS.md TODO #31).
 *
 * Per-senzor DECIMACNI PYRAMIDA (stejny idiom jako trend_feed / ADEV pyramida ve
 * screen_main.c): kazdy senzor ma nekolik ringu s rostoucim casovym krokem, takze
 * jedno pole pokryje minuty az hodiny bez velke pameti. Slouzi oknu Grafy (#31)
 * jako HLADKY kratkodoby zdroj (od bootu); dlouhou historii (dny) bere okno z
 * datalogu (W25Q). Zde jsou VSECHNY senzory (g_sensors[]) — datalog jen 4.
 *
 * Cadence: `sensor_hist_feed()` vola SensorsTask z 2 Hz plneho sweepu; uvnitr se
 * decimuje na zakladni krok SENSOR_HIST_BASE_S. Cteni: kdokoli (UI). Bez zamku
 * (stejne jako g_sensors[] — hodnoty se meni pomalu, roztrzene cteni tolerovano).
 *
 * Cisty C header (jen sensor_stat.h -> <stdint.h>): smi ho includovat firmware
 * i decoupled app/ vrstva.
 */
#ifndef INC_SENSOR_HIST_H_
#define INC_SENSOR_HIST_H_

#include <stdint.h>
#include "sensor_stat.h"

#define SENSOR_HIST_STAGES   4      /* pocet decimacnich stagi */
#define SENSOR_HIST_RING     96     /* vzorku na stage */
#define SENSOR_HIST_DECIM    4      /* decimace mezi stagemi (×4) */
#define SENSOR_HIST_BASE_S   2      /* casovy krok stage 0 [s] */
/* Pokryti: s0 2s×96=192s (3,2min), s1 8s×96=12,8min, s2 32s×96=51min,
 * s3 128s×96=~3,4h. Pamet: SENS_COUNT×4×96×4B ≈ 15 kB v RAM_D1. */

/** Periodicky feed z SensorsTask (2 Hz plny sweep). Uvnitr decimuje na
 *  SENSOR_HIST_BASE_S; kazdy senzor krmi svou hodnotou g_sensors[id].last. */
void sensor_hist_feed(void);

/** Naplni `out` (oldest→newest) serii senzoru `id` pro casove okno `win_s`.
 *  Vybere nejjemnejsi stage, ktery okno pokryje (fallback na nejvyssi s ≥2 vzorky).
 *  Vraci pocet zapsanych bodu (0 = jeste zadna data). Nepovinne vystupy (smi byt
 *  NULL): out_min/out_max = rozsah hodnot (autoscale), out_step_s = krok stage [s],
 *  out_span_s = skutecne pokryty cas [s]. `max_out` = kapacita `out` (>=2). */
int sensor_hist_series(sensor_id_t id, int32_t win_s,
                       float *out, int max_out,
                       float *out_min, float *out_max,
                       int32_t *out_step_s, int32_t *out_span_s);

#endif /* INC_SENSOR_HIST_H_ */
