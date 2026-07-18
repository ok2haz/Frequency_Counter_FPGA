/**
 * @file    calib.h
 * @brief   Editovatelna kalibrace: AD8307 (RF urovnomer) + ADS1115 napetove
 *          delice (12V/5V vetev). Perzistence do W25Q CALIB regionu pres
 *          genericky w25q_store (viz w25q_map.h). Bez ulozeneho zaznamu
 *          (prazdna flash / nova deska) plati vychozi (datasheet) hodnoty.
 */
#ifndef CALIB_H
#define CALIB_H

#include <stdbool.h>

typedef struct {
    float ad8307_slope_mv_db;    /* mV / dB (typicky 25.0, datasheet AD8307) */
    float ad8307_intercept_dbm;  /* dBm pri 0 V (typicky -84.0) */
    float gain_12v;              /* multiplikator ADS mV -> skutecne mV, 12V vetev */
    float gain_5v;                /* multiplikator ADS mV -> skutecne mV, 5V vetev */
} calib_t;

/* Live hodnoty. Cte SensorsTask (gain_12v/gain_5v, prepocet ADS -> skutecne
 * napeti) i UiTask (AD8307 dBm vypocet). Zapisuje VYHRADNE UiTask (okno
 * Kalibrace, tlacitka -/+) po jednom floatu -> zarovnany 32bit zapis je na
 * Cortex-M7 atomicky, zadny mutex netreba (stejny pattern jako g_brightness). */
extern volatile calib_t g_calib;

/** Nacte kalibraci z W25Q CALIB store; prazdny/nevalidni zaznam nebo
 *  nedostupna flash -> g_calib zustane na vychozich (datasheet) hodnotach.
 *  Volat JEDNOU pri startu z UiTask kontextu (app_gpsdo_init) - blokujici
 *  (~ms), NE z ISR / pred schedulerem. */
void calib_load(void);

/** Ulozi aktualni g_calib do W25Q CALIB store (blokujici, erase+write ~stovky
 *  ms). Volat jen na explicitni pozadavek uzivatele (tlacitko ULOZIT), NE
 *  periodicky (opotrebeni flash). @return true = zapis OK. */
bool calib_save(void);

#endif /* CALIB_H */
