/**
 * @file    ads1115.h
 * @brief   TI ADS1115 16-bit ADC @ I2C 0x48 (na FPGA desce, I2C1).
 *          Single-shot, single-ended kanaly AIN0..AIN3, 128 SPS.
 *          PGA je PER KANAL (MUX i PGA jsou ve stejnem Config registru, ktery
 *          se stejne prepisuje pred kazdou konverzi -> per-kanal PGA je zdarma).
 */
#ifndef ADS1115_H
#define ADS1115_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define ADS1115_I2C_ADDR   (0x48u << 1)

/* PGA[2:0] (FSR). ⚠️ Vstup ADC nikdy nesmi prekrocit VDD+0.3 (=3.6V) bez ohledu
 * na zvoleny FSR — vetsi FSR nez VDD jen zbytecne ztraci rozliseni. */
typedef enum {
    ADS1115_PGA_6V144 = 0,   /* +-6.144 V (0.1875 mV/LSB) */
    ADS1115_PGA_4V096 = 1,   /* +-4.096 V (0.125  mV/LSB) — AD8307 (RF_Level) */
    ADS1115_PGA_2V048 = 2,   /* +-2.048 V (0.0625 mV/LSB) — delice mapovane na ~2.0V */
    ADS1115_PGA_1V024 = 3,   /* +-1.024 V */
    ADS1115_PGA_0V512 = 4,   /* +-0.512 V */
    ADS1115_PGA_0V256 = 5,   /* +-0.256 V (0110/0111 taky 0.256) */
} ads1115_pga_t;

/** Spusti single-shot konverzi single-ended kanalu (0..3) s danym PGA. */
bool ads1115_start(I2C_HandleTypeDef *hi2c, uint8_t ch, ads1115_pga_t pga);

/** Precte vysledek posledni konverze (raw int16). */
bool ads1115_read_raw(I2C_HandleTypeDef *hi2c, int16_t *raw);

/** Full-scale [mV] pro dany PGA (napeti odpovidajici raw = 32768). */
static inline int32_t ads1115_pga_fs_mv(ads1115_pga_t pga)
{
    switch (pga) {
        case ADS1115_PGA_6V144: return 6144;
        case ADS1115_PGA_4V096: return 4096;
        case ADS1115_PGA_2V048: return 2048;
        case ADS1115_PGA_1V024: return 1024;
        case ADS1115_PGA_0V512: return 512;
        default:                return 256;   /* 0V256 */
    }
}

/** Prevod raw -> mV na pinu ADC pro dany PGA (LSB = FS/32768). */
static inline int32_t ads1115_raw_to_mv(int16_t raw, ads1115_pga_t pga)
{
    return ((int32_t)raw * ads1115_pga_fs_mv(pga)) / 32768;
}

#endif /* ADS1115_H */
