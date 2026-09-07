/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : FMC.h
  * Description        : This file provides code for the configuration
  *                      of the FMC peripheral.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#ifndef __FMC_H
#define __FMC_H
#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern SDRAM_HandleTypeDef hsdram1;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_FMC_Init(void);
void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef* hsdram);
void HAL_SDRAM_MspDeInit(SDRAM_HandleTypeDef* hsdram);

/* USER CODE BEGIN Prototypes */

/* Inicializacni sekvence SDRAM dle JEDEC. Vraci 0 pri uspechu, jinak CISLO
 * KROKU, ktery selhal (1 CLK_ENABLE, 2 PALL, 3 AUTOREFRESH, 4 LOAD_MODE,
 * 5 ProgramRefreshRate, 6 SDRTR se nepotvrdil).
 * ⚠️ Do 2026-09-07 se navratove hodnoty vsech kroku ZAHAZOVALY, takze se
 * sekvence mohla tise nedokoncit — a presne to vypadalo jako "po teplem resetu
 * OK, po studenem ne".
 * ⚠️ Lze spustit ZNOVU za behu (UART `sdraminit`) — to je pokus, ktery odlisi
 * chybu v CASOVANI prvni inicializace od jine priciny. Bezi z UartTasku;
 * behem nej muze displej kratce probliknout (LTDC cte tutez SDRAM). */
uint8_t fmc_sdram_init_sequence(void);
extern volatile uint8_t  g_fmc_init_fail;   /* 0 = sekvence prosla cela */
extern volatile uint32_t g_fmc_init_runs;

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__FMC_H */

/**
  * @}
  */

/**
  * @}
  */
