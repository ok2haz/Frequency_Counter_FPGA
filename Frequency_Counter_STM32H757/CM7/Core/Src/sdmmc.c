/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sdmmc.c
  * @brief   This file provides code for the configuration
  *          of the SDMMC instances.
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
/* Includes ------------------------------------------------------------------*/
#include "sdmmc.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

SD_HandleTypeDef hsd1;

/* SDMMC1 init function */

void MX_SDMMC1_SD_Init(void)
{

  /* USER CODE BEGIN SDMMC1_Init 0 */
  /* ⚠️⚠️ ZAMERNY EARLY RETURN — NEODSTRANOVAT. Init SD se dela LAZY v `sd_probe()`
   * (datalog_sd.c), ne tady.
   *
   * DUVOD: telo teto funkce nize vola `Error_Handler()`, kdyz `HAL_SD_Init` selze —
   * a ten na CM7 dela `__disable_irq(); bootled_fail();`, tedy NEKONECNE blikani
   * LED_1 bez displeje a bez konzole. `HAL_SD_Init` pritom selze VZDY, kdyz NENI
   * VLOZENA KARTA. Boot bez karty by tak pristroj uplne zabil.
   *
   * To jde proti cele filozofii bootu tohoto projektu: chybejici HW = degradovany
   * beh, ne smrt (panel ma `goto display_skip`, CM4 ma `g_cm4_absent`). SD musi byt
   * stejne — bez karty se proste loguje dal do W25Q.
   *
   * Tenhle `return` je v USER CODE bloku, takze **prezije regeneraci CubeMX**.
   * ⚠️⚠️ ALE HANDLE SE VYPLNIT MUSI (nalez 2026-08-12 pres GDB): `HAL_SD_MspInit()`
   * zacina `if (sdHandle->Instance == SDMMC1)`, takze s `Instance == NULL` se
   * NEZAPNOU hodiny SDMMC1 ani nenakonfiguruji piny — `hsd1.Init` zustalo cele
   * nulove a registry @0x52007000 byly same nuly. Holy `return;` tedy rozbil celou
   * SD cestu. Plneni struktury na HW nesaha, takze boot bez karty zustava bezpecny.
   *
   * ⚠️ Hodnoty MUSI sedet s generovanymi nize (zdroj pravdy = .ioc). Pri zmene
   * konfigurace SDMMC1 v CubeMX je srovnej — i v `sd_probe()` v datalog_sd.c.
   * ⚠️ ClockDiv=4 -> SDMMC_CK = 64 MHz / (2 x 4) = 8 MHz (bring-up, 2026-08-13).
   * Snizeno z 2 (16 MHz) zamerne: deska ma na SD VDD jen C75 100n (chybi bulk
   * 4,7-10 uF) a na CK neni seriovy tlumici odpor ~22-33 R, takze pri 16 MHz
   * hrozi prekmity. HW sice pri 16 MHz odpovida (CardState=TRANSFER), ale
   * SOUVISLY PRENOS se nikdy neprokazal — `f_mount` zatim selhava. Az `sd test`
   * projde, jde vratit na 2 a overit znovu. ⚠️ Hodnota je i v `.ioc`. */
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd1.Init.ClockDiv            = 4;
  return;   /* skutecny HAL_SD_Init az v BSP_SD_Init() pri mountu */
  /* USER CODE END SDMMC1_Init 0 */

  /* USER CODE BEGIN SDMMC1_Init 1 */

  /* USER CODE END SDMMC1_Init 1 */
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  hsd1.Init.ClockDiv = 4;
  if (HAL_SD_Init(&hsd1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SDMMC1_Init 2 */

  /* USER CODE END SDMMC1_Init 2 */

}

void HAL_SD_MspInit(SD_HandleTypeDef* sdHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(sdHandle->Instance==SDMMC1)
  {
  /* USER CODE BEGIN SDMMC1_MspInit 0 */

  /* USER CODE END SDMMC1_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SDMMC;
    PeriphClkInitStruct.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* SDMMC1 clock enable */
    __HAL_RCC_SDMMC1_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    /**SDMMC1 GPIO Configuration
    PC8     ------> SDMMC1_D0
    PC9     ------> SDMMC1_D1
    PC10     ------> SDMMC1_D2
    PC11     ------> SDMMC1_D3
    PC12     ------> SDMMC1_CK
    PD2     ------> SDMMC1_CMD
    */
    GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_SDIO1;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_SDIO1;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* USER CODE BEGIN SDMMC1_MspInit 1 */

  /* USER CODE END SDMMC1_MspInit 1 */
  }
}

void HAL_SD_MspDeInit(SD_HandleTypeDef* sdHandle)
{

  if(sdHandle->Instance==SDMMC1)
  {
  /* USER CODE BEGIN SDMMC1_MspDeInit 0 */

  /* USER CODE END SDMMC1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_SDMMC1_CLK_DISABLE();

    /**SDMMC1 GPIO Configuration
    PC8     ------> SDMMC1_D0
    PC9     ------> SDMMC1_D1
    PC10     ------> SDMMC1_D2
    PC11     ------> SDMMC1_D3
    PC12     ------> SDMMC1_CK
    PD2     ------> SDMMC1_CMD
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12);

    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);

  /* USER CODE BEGIN SDMMC1_MspDeInit 1 */

  /* USER CODE END SDMMC1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

