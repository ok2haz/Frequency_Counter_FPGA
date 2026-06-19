/**
 * @file    ft5x06.h
 * @brief   Driver pro FocalTech FT5x06 capacitive touch controller @ I2C 0x38
 *
 * Reference: Linux drivers/input/touchscreen/edt-ft5x06.c
 *
 * Pouziti:
 *   ft5x06_probe(&hi2c4);                     // jednou pri startu
 *   ft5x06_touch_t t;
 *   ft5x06_read_touch(&hi2c4, &t);            // polling
 *   if (t.valid) printf("X=%u Y=%u\n", t.x, t.y);
 */
#ifndef FT5X06_H
#define FT5X06_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 7-bit adresa 0x38, HAL pouziva 8-bit format (posun o 1 vlevo) */
#define FT5X06_I2C_ADDR    (0x38u << 1)

/* Pocatecni registr touch framu (TD_STATUS). Cti od nej 31 bytu (cely frame). */
#define FT5X06_REG_TD_STATUS  0x02
#define FT5X06_FRAME_LEN      31

/* Event flags v poli "touch[N].event" */
#define FT5X06_EVENT_DOWN     0x00   /* Press Down (zacatek doteku) */
#define FT5X06_EVENT_UP       0x01   /* Lift Up (puzdejnuti)        */
#define FT5X06_EVENT_CONTACT  0x02   /* Contact (drzeni)            */
#define FT5X06_EVENT_NONE     0x03   /* No event                    */

/* Touch report - data pro jeden dotekovy bod */
typedef struct {
    bool     valid;     /* true = doteku registrovano */
    uint16_t x;         /* X souradnice (0..799 pro nas panel)    */
    uint16_t y;         /* Y souradnice (0..479)                  */
    uint8_t  event;     /* viz FT5X06_EVENT_*                     */
    uint8_t  id;        /* track ID (0..4 pro multi-touch)        */
    uint8_t  num_touches; /* kolik doteku celkem (0..5)           */
} ft5x06_touch_t;

/**
 * @brief Inicializace + verifikace pritomnosti FT5x06 na I2C.
 *        Cte vendor + firmware ID a vypise je do logu.
 * @return true pokud zarizeni odpovida
 */
bool ft5x06_probe(I2C_HandleTypeDef *hi2c);

/**
 * @brief Precist aktualni stav doteku (prvni touch point).
 *        Pri zadnem doteku nastavi touch->valid = false.
 * @return true pokud I2C cteni proslo
 */
bool ft5x06_read_touch(I2C_HandleTypeDef *hi2c, ft5x06_touch_t *touch);

/**
 * @brief Naparsuje touch frame (FT5X06_FRAME_LEN bytu od TD_STATUS) do touch.
 *        Pouziva se po asynchronnim (IT/DMA) ctení, kdy data uz jsou v bufferu.
 * @return true (parsing vzdy projde; touch->valid dle obsahu)
 */
bool ft5x06_parse(const uint8_t *frame, ft5x06_touch_t *touch);

#ifdef __cplusplus
}
#endif

#endif /* FT5X06_H */
