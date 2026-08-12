/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : fatfs_platform.c
  * @brief          : fatfs_platform source file
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
#include "fatfs_platform.h"

uint8_t	BSP_PlatformIsDetected(void) {
    uint8_t status = SD_PRESENT;
    /* Check SD card detect pin */
    if(HAL_GPIO_ReadPin(SD_DETECT_GPIO_PORT, SD_DETECT_PIN) != GPIO_PIN_RESET)
    {
        status = SD_NOT_PRESENT;
    }
    /* USER CODE BEGIN 1 */
    /* ⚠️ Override detekce (UART `sd force on`). Card-detect je jen POMŮCKA — jestli
     * karta opravdu je, definitivně řekne až `HAL_SD_Init` v `BSP_SD_Init()`.
     * Když spínač v socketu není osazený/zapojený, nesmí zablokovat celou SD cestu.
     * Měřeno 2026-08-12: PE3 zůstal HIGH i se zasunutou kartou.
     * Deklarace ručně (tenhle generovaný soubor nemá USER CODE blok pro include);
     * proto vrací `int`, ne `bool`. Viz datalog.h / datalog_sd.c. */
    {
        extern int datalog_sd_detect_status(void);   /* 1 = karta pritomna */
        status = datalog_sd_detect_status() ? SD_PRESENT : SD_NOT_PRESENT;
    }
    /* USER CODE END 1 */
    return status;
}
