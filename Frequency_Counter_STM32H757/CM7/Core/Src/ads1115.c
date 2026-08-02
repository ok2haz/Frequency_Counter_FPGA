/**
 * @file    ads1115.c
 * @brief   TI ADS1115 ADC driver. Viz ads1115.h.
 */
#include "ads1115.h"

#define ADS_REG_CONVERSION   0x00
#define ADS_REG_CONFIG       0x01
#define ADS_TIMEOUT          50

bool ads1115_start(I2C_HandleTypeDef *hi2c, uint8_t ch, ads1115_pga_t pga)
{
    if (ch > 3 || pga > ADS1115_PGA_0V256) return false;

    /* Config: OS=1 (start) | MUX=100+ch (AINch vs GND) | PGA (per kanal)
     *       | MODE=1 (single-shot) | DR=100 (128 SPS) | COMP_QUE=11 (disable). */
    uint16_t cfg = (1u << 15)
                 | ((uint16_t)(0x04u + ch) << 12)
                 | ((uint16_t)pga << 9)
                 | (1u << 8)
                 | (4u << 5)
                 | 0x0003u;

    uint8_t d[2] = { (uint8_t)(cfg >> 8), (uint8_t)(cfg & 0xFF) };
    return HAL_I2C_Mem_Write(hi2c, ADS1115_I2C_ADDR, ADS_REG_CONFIG,
                             I2C_MEMADD_SIZE_8BIT, d, 2, ADS_TIMEOUT) == HAL_OK;
}

bool ads1115_read_raw(I2C_HandleTypeDef *hi2c, int16_t *raw)
{
    uint8_t b[2];
    if (HAL_I2C_Mem_Read(hi2c, ADS1115_I2C_ADDR, ADS_REG_CONVERSION,
                         I2C_MEMADD_SIZE_8BIT, b, 2, ADS_TIMEOUT) != HAL_OK) {
        return false;
    }
    *raw = (int16_t)(((uint16_t)b[0] << 8) | b[1]);
    return true;
}
