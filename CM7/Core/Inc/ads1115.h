/**
 * @file    ads1115.h
 * @brief   TI ADS1115 16-bit ADC @ I2C 0x48 (na FPGA desce, I2C1).
 *          Single-shot, single-ended kanaly AIN0..AIN3, PGA +-4.096 V, 128 SPS.
 */
#ifndef ADS1115_H
#define ADS1115_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define ADS1115_I2C_ADDR   (0x48u << 1)

/** Spusti single-shot konverzi single-ended kanalu (0..3). */
bool ads1115_start(I2C_HandleTypeDef *hi2c, uint8_t ch);

/** Precte vysledek posledni konverze (raw int16). */
bool ads1115_read_raw(I2C_HandleTypeDef *hi2c, int16_t *raw);

/** Prevod raw -> mV pri PGA +-4.096 V (LSB = 0.125 mV). */
static inline int32_t ads1115_raw_to_mv(int16_t raw)
{
    return ((int32_t)raw * 4096) / 32768;   /* = raw/8 mV */
}

#endif /* ADS1115_H */
