/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.h
  * @brief   This file contains all the function prototypes for
  *          the i2c.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __I2C_H__
#define __I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern I2C_HandleTypeDef hi2c4;

/* USER CODE BEGIN Private defines */
extern I2C_HandleTypeDef hi2c1;   /* FPGA deska: TMP117 0x49/0x4A, ADS1115 0x48, Si5356A 0x70/0x71 */
/* USER CODE END Private defines */

void MX_I2C4_Init(void);

/* USER CODE BEGIN Prototypes */
void MX_I2C1_Init(void);

/* TRI provozni rychlosti I2C4, jedna per zarizeni — zdůvodnění v i2c.c
 * u definice. Volat VZDY pod i2c4MutexHandle. */
#define I2C4_TIMING_ATTINY_50KHZ     0x70303AEEu   /* 0x45 ATTINY  — bit-bang, CPU 1 MHz */
#define I2C4_TIMING_TOUCH_75KHZ      0x7030279Fu   /* 0x38 FT5x06  — periferie */
#define I2C4_TIMING_TMP117_400KHZ    0x30700E3Bu   /* 0x48 TMP117  — periferie */
void i2c4_speed_select(uint32_t timing);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __I2C_H__ */

