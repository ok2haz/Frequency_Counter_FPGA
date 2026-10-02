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

/* 🔴 2026-10-02: AD8307 (RF log-detektor) na teto desce FYZICKY NENI OSAZEN —
 * overeno kicad-cli exportem netlistu FPGA_Module_2_1 (zadny AD8307 v zadnem
 * z 5 listu schematu). AIN1 (SENS_ADS1, viz sensor_stat.h) meri VBUS, ne
 * vystup logaritmickeho detektoru. Vsechna mista, ktera z tohoto kanalu
 * pocitaji dBm (`mp_ad8307_dbm` nad `ad8307_slope_mv_db`/`ad8307_intercept_dbm`
 * nize), MUSI tento priznak respektovat a hlasit "nedostupne" (NaN/SCPI
 * 9.91E37/prazdny bar), ne pocitat verohodne vypadajici cislo ze spatneho
 * vstupu — stejna politika jako F-0165 u neplatne strmosti. */
#define RF_LEVEL_HW_PRESENT 0

typedef struct {
    float ad8307_slope_mv_db;    /* mV / dB (typicky 25.0, datasheet AD8307).
                                     Viz RF_LEVEL_HW_PRESENT vyse — na teto desce
                                     se nepouziva (HW neni osazen), pole zustava
                                     kvuli stabilite formatu CALIB blobu. */
    float ad8307_intercept_dbm;  /* dBm pri 0 V (typicky -84.0), totez omezeni */
    /* 🔴 2026-10-02: nazvy poli jsou HISTORICKE, neodpovidaji uz realne
     * fyzicke vetvi (overeno netlistem FPGA_Module_2_1) - prejmenovani je
     * samostatny zasah, viz calib.c. `gain_12v` je dnes AIN2 = +3V3,
     * `gain_5v` je dnes AIN3 = +5V pres jiny delic nez puvodne. */
    float gain_12v;              /* multiplikator ADS mV -> skutecne mV, AIN2 = +3V3 vetev */
    float gain_5v;                /* multiplikator ADS mV -> skutecne mV, AIN3 = +5V vetev */
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

/** 1 = CALIB store ve W25Q je pouzitelny. 0 = `calib_load()` ho nepripravil ->
 *  `g_calib` drzi datasheetove vychozi hodnoty a mereni RF/napeti je nekalibrovane,
 *  bez jakekoli stopy (audit F-0098). */
int calib_store_ready(void);

#endif /* CALIB_H */
