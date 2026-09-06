#ifndef INC_ENCODER_H_
#define INC_ENCODER_H_

/**
 * @file    encoder.h
 * @brief   Rotacni encoder s tlacitkem — HW vrstva + udalosti (Faze A prechodu na encoder).
 *
 * HW: TIM1 v encoder modu, CH1=PA8, CH2=PA9, tlacitko=PC13 (pull-up, spina na zem).
 * ⚠️ Piny si modul konfiguruje SAM (idempotentne) — v `.ioc` NENI nic z toho,
 * stejny regen-safe vzor jako CS pin ve `fpga_freq_init`.
 *
 * ⚠️ VLAKNA: `encoder_poll()` vola VYHRADNE UiTask (~100 Hz smycka). Modul nema
 * zadny zamek — jeden ctenar staci a UI stejne kresli jen UiTask.
 *
 * ⚠️ DVOJKLIK A KRATKY STISK PRICHAZEJI SPOLU. `short_press` se hlasi HNED pri
 * uvolneni, `double_click` se pripoji, kdyz druhy stisk dorazi do `ENC_DOUBLE_MS`.
 * Zamerne: opozdit kazdy kratky stisk o 400 ms kvuli detekci dvojkliku by udelalo
 * cele ovladani liene. Dusledek: dvojklik na hlavni obrazovce nejdriv posune
 * cyklus aktivniho parametru o jednu polozku a teprve pak otevre menu — cyklus
 * je nedestruktivni, takze to nevadi.
 */

#include <stdint.h>

/** Udalosti z jednoho pollu. Muze prijit vic naraz (otoceni i stisk). */
typedef struct {
    int16_t  steps;         /* +/- zapadkove kroky od posledniho pollu (0 = nic) */
    uint16_t steps_per_s;   /* rychlost otaceni -> adaptivni krok (zadani UI §5) */
    uint8_t  short_press;   /* 1 = kratky stisk (pri uvolneni) */
    uint8_t  long_press;    /* 1 = drzeno >= 1 s; hlasi se JEDNOU, uvolneni uz nedela short */
    uint8_t  double_click;  /* 1 = druhy kratky stisk do 400 ms (spolu se short_press) */
} encoder_ev_t;

/** Nakonfiguruje piny + TIM1. Idempotentni, volat pred prvnim `encoder_poll`. */
void encoder_init(void);

/** Precte udalosti od minuleho volani. Volat pravidelne (>= 50 Hz) z UiTasku. */
void encoder_poll(encoder_ev_t *ev);

/** 1 = od bootu prislo aspon jedno otoceni nebo stisk.
 *  ⚠️ Slouzi k tomu, aby UI NEZOBRAZOVALO fokus, dokud uzivatel encoder nepouzil —
 *  pri ovladani dotykem by ramecek fokusu jen mátl. */
int encoder_seen(void);

/** Monotonni citace UVNITR modulu — pro diagnostiku BEZ druheho `encoder_poll`.
 *  🔴 `encoder_poll()` je jednokonzumentove API: kazde volani spotrebuje deltu
 *  citace i priznaky. Kdyby ho vedle UiTasku volal jeste UART prikaz `enc`,
 *  oba by si udalosti kradli a diagnostika by lhala (presne to se 2026-08-31
 *  stalo). `enc` proto jen CTE tyhle citace. */
uint32_t encoder_event_count(void);
int32_t  encoder_step_total(void);

/** Delic kroku TIM1 na jednu ZAPADKU encoderu. Vychozi 4 (mode 3 pocita obe
 *  hrany obou kanalu). ⚠️ Je to jedina HW-zavisla konstanta modulu a u jineho
 *  encoderu muze byt 1 nebo 2 — proto je nastavitelna ZA BEHU (`enc div N`)
 *  a persistuje v syscfg, aby se kvuli ni nemuselo preflashovat.
 *  Platne hodnoty 1/2/4; jina se ignoruje. */
void encoder_set_div(uint8_t d);
uint8_t encoder_div(void);

#endif /* INC_ENCODER_H_ */
