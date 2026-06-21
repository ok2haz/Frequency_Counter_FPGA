/*
 * sensor_stat.h
 *
 * Sdílený stav senzorů (teploty TMP117 ×3, napětí ADS1115 ×4): poslední hodnota
 * + platnost + statistika (min/max/avg) + čítače chyb. Slouží displeji,
 * UART výpisu i budoucí matematice/statistice/logu.
 *
 * Čistý C header (jen <stdint.h>), BEZ RTOS/HAL závislostí — smí ho includovat
 * jak firmware (Core/Src), tak decoupled app/ vrstva (UI).
 *
 * Zápis: VÝHRADNĚ SensorsTask (freertos_task_sensors.c) přes sensor_update()
 * (platný vzorek) / sensor_fail() (chyba čtení). Čtení: kdokoli.
 *
 * ⚠️ Sdílení bez zámku (jako ostatní g_* stavy). Hodnoty se mění ~1×/s; případné
 * roztržené čtení vícebajtové položky je pro monitor/diagnostiku akceptovatelné.
 * Přesná matematika nad vzorky by měla běžet uvnitř SensorsTask.
 */
#ifndef INC_SENSOR_STAT_H_
#define INC_SENSOR_STAT_H_

#include <stdint.h>

/* Index senzoru v g_sensors[]. Pořadí = pořadí čtení v SensorsTask. */
typedef enum {
    SENS_T48 = 0,   /* TMP117 0x48 (displej, I2C4)   [°C] */
    SENS_T49,       /* TMP117 0x49 (FPGA, I2C1)      [°C] */
    SENS_T4A,       /* TMP117 0x4A (FPGA, I2C1)      [°C] */
    SENS_ADS0,      /* ADS1115 AIN0                  [mV] */
    SENS_ADS1,      /* ADS1115 AIN1                  [mV] */
    SENS_ADS2,      /* ADS1115 AIN2 = 12V větev      [mV] */
    SENS_ADS3,      /* ADS1115 AIN3 = 5V větev       [mV] */
    SENS_COUNT
} sensor_id_t;

/* Stav + statistika jednoho skalárního senzoru. Jednotka dle sensor_id_t. */
typedef struct {
    float    last;        /* poslední hodnota; při chybě DRŽÍ poslední dobrou */
    float    min;         /* minimum z platných vzorků */
    float    max;         /* maximum z platných vzorků */
    float    mean;        /* aritmetický průměr (running) z platných vzorků */
    uint32_t samples;     /* počet platných vzorků od resetu (0 = ještě žádný) */
    uint32_t err_total;   /* celkový počet chyb čtení od bootu */
    uint16_t err_streak;  /* po sobě jdoucí chyby (0 = poslední čtení OK) */
    uint8_t  valid;       /* 1 = poslední čtení bylo platné */
    uint8_t  _pad;
} sensor_stat_t;

extern sensor_stat_t g_sensors[SENS_COUNT];

/* Zaznamená PLATNÝ vzorek: last=value, aktualizuje min/max/mean/samples,
 * valid=1, err_streak=0. Min/max/mean se lazy-inicializují prvním vzorkem. */
void sensor_update(sensor_id_t id, float value);

/* Zaznamená CHYBU čtení: valid=0, err_total++, err_streak++. 'last' zůstává
 * (poslední dobrá hodnota) → matematika/log mají ignorovat podle valid. */
void sensor_fail(sensor_id_t id);

/* Vynuluje statistiku (min/max/mean/samples) jednoho senzoru; last/valid/chyby nechá. */
void sensor_stat_reset(sensor_id_t id);

#endif /* INC_SENSOR_STAT_H_ */
