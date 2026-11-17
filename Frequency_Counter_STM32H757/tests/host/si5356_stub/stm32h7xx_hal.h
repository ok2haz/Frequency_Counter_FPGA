/* Atrapa HAL pro host test si5356.c (tests/host/t_si5356_gen.c): jen typy a funkce,
 * ktere si5356.c pouziva. I2C funkce implementuje test (registrova pamet cipu). */
#ifndef STM32H7XX_HAL_STUB_H
#define STM32H7XX_HAL_STUB_H
#include <stdint.h>

typedef struct { int dummy; } I2C_HandleTypeDef;
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
#define I2C_MEMADD_SIZE_8BIT 1u
#define __DMB() do { } while (0)

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t dev, uint16_t reg, uint16_t sz,
                                    uint8_t *d, uint16_t n, uint32_t tmo);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t dev, uint16_t reg, uint16_t sz,
                                   uint8_t *d, uint16_t n, uint32_t tmo);
void HAL_Delay(uint32_t ms);
#endif
