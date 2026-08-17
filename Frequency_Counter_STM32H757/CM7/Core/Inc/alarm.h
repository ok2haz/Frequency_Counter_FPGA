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
#include <stdint.h>

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

/* ══════════════ Prahovy monitor realnych velicin (okno PRAHY, s_view=39) ══════
 * Do 2026-08-17 alarm hlidal jen UDALOSTI (FPGA link, GPS lock, limit pass/fail)
 * — z deseti senzoru, ktere merи 24/7 realna data, ANI JEDEN. Tohle to doplnuje:
 * tri prahove podminky nad skutecne merenymi velicinami.
 *
 * ⚠️ HYSTEREZE je povinna. Analogova velicina sedici presne na prahu by bez ni
 * prepinala stav pri kazdem vyhodnoceni (5x/s) a pipala donekonecna. */
typedef struct {
    /* VBAT = zalozni CR2032 pro RTC/BKP domenu. Kdyz dojde, TICHE se ztraci cas
     * a nastaveni pri kazdem odpojeni napajeni — degradace, kterou jinak nelze
     * zaznamenat, dokud se neprojevi. Prah = "vymen brzy", ne "uz je pozde". */
    uint8_t vbat_en;
    float   vbat_lo_mv;        /* pod = alarm */

    /* OCXO teplota mimo pasmo = pec se rozladila. Pro kmitoctovy normal je to
     * primo relevantni (teplota rizne kmitocet), a je to realne merene 0x49. */
    uint8_t ocxo_en;
    float   ocxo_lo_c, ocxo_hi_c;

    /* σy@1s nad prahem = kratkodoba stabilita se zhorsila.
     * ⚠️ DNES SE POCITA ZE SIMULACE headline (viz #2 v STATUS.md) — proto je
     * tenhle prah VYCHOZI VYPNUTY. Az pojede realny FPGA link, staci ho zapnout;
     * mechanika je hotova a spravna. Zapnout ho DRIV znamena pipat na sum. */
    uint8_t adev_en;
    float   adev_max;          /* nad = alarm */
} mon_cfg_t;

extern mon_cfg_t g_mon_cfg;

/** Vychozi prahy (volá syscfg pri neznamem/chybejicim zaznamu). */
void mon_cfg_defaults(mon_cfg_t *c);

/* Aktualni stav podminek — 1 = mimo mez. Cte SYS pilulka (`compute_sys_level`)
 * i okno Alarmy/PRAHY. Zapisuje vyhradne `alarm_tick` (defaultTask). */
extern volatile uint8_t g_mon_vbat_bad;
extern volatile uint8_t g_mon_ocxo_bad;
extern volatile uint8_t g_mon_adev_bad;

/* Pocitadla prekroceni (nuluje `alarm_reset_counters`). */
extern volatile unsigned int g_alarm_vbat;
extern volatile unsigned int g_alarm_ocxo;
extern volatile unsigned int g_alarm_adev;

/** σy@1s pro prahove vyhodnoceni. Publikuje APP vrstva
 *  (`app_gpsdo_tick_stats_sample` ze `screen_main_adev_1s()`), cte `alarm_tick`.
 *  ⚠️ Stejny vzor jako `g_meas_verdict`: Core vrstva nesmi sahat do `app/screens/`,
 *  takze se hodnota preda pres globál. 0 = jeste neni dost vzorku. */
extern volatile float g_adev_1s;

#endif /* ALARM_H */
