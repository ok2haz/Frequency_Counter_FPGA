/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.c
  * @brief   This file provides code for the configuration
  *          of the I2C instances.
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
#include "i2c.h"

/* USER CODE BEGIN 0 */
#include "bootled.h"
/* USER CODE END 0 */

I2C_HandleTypeDef hi2c4;

/* I2C4 init function */
void MX_I2C4_Init(void)
{

  /* USER CODE BEGIN I2C4_Init 0 */
  bootled_step(BOOTLED_STEP_I2C4);
  /* USER CODE END I2C4_Init 0 */

  /* USER CODE BEGIN I2C4_Init 1 */

  /* USER CODE END I2C4_Init 1 */
  hi2c4.Instance = I2C4;
  hi2c4.Init.Timing = 0x70303AEE;
  hi2c4.Init.OwnAddress1 = 0;
  hi2c4.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c4.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c4.Init.OwnAddress2 = 0;
  hi2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c4.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c4.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c4, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c4, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C4_Init 2 */

  /* USER CODE END I2C4_Init 2 */

}

void HAL_I2C_MspInit(I2C_HandleTypeDef* i2cHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(i2cHandle->Instance==I2C4)
  {
  /* USER CODE BEGIN I2C4_MspInit 0 */

  /* USER CODE END I2C4_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
    PeriphClkInitStruct.I2c4ClockSelection = RCC_I2C4CLKSOURCE_D3PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_GPIOH_CLK_ENABLE();
    /**I2C4 GPIO Configuration
    PH11     ------> I2C4_SCL
    PH12     ------> I2C4_SDA
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C4;
    HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

    /* I2C4 clock enable */
    __HAL_RCC_I2C4_CLK_ENABLE();
  /* USER CODE BEGIN I2C4_MspInit 1 */
    /* Pozn.: I2C4 dotek se cte pollingem (blokujici HAL pod mutexem), takze
     * EV/ER preruseni NEjsou potreba a zamerne se nepovoluji. */
  /* USER CODE END I2C4_MspInit 1 */
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* i2cHandle)
{

  if(i2cHandle->Instance==I2C4)
  {
  /* USER CODE BEGIN I2C4_MspDeInit 0 */

  /* USER CODE END I2C4_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_I2C4_CLK_DISABLE();

    /**I2C4 GPIO Configuration
    PH11     ------> I2C4_SCL
    PH12     ------> I2C4_SDA
    */
    HAL_GPIO_DeInit(GPIOH, GPIO_PIN_11);

    HAL_GPIO_DeInit(GPIOH, GPIO_PIN_12);

  /* USER CODE BEGIN I2C4_MspDeInit 1 */

  /* USER CODE END I2C4_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/* I2C1 na FPGA desce: TMP117 0x49/0x4A, ADS1115 0x48, Si5356A 0x70/0x71.
 * Piny PB8=SCL, PB9=SDA (AF4). Self-contained (GPIO+clock zde), aby to prezilo
 * CubeMX regeneraci. Timing jako I2C4, stejny kernel 120 MHz -> ~50 kHz
 * (NE 100 kHz, jak tu stalo do 2026-09-09; prepocet je v CLAUDE.md). */
I2C_HandleTypeDef hi2c1;

void MX_I2C1_Init(void)
{
  bootled_step(BOOTLED_STEP_I2C1);
  GPIO_InitTypeDef gpio = {0};
  RCC_PeriphCLKInitTypeDef pclk = {0};

  pclk.PeriphClockSelection = RCC_PERIPHCLK_I2C123;
  pclk.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
  HAL_RCCEx_PeriphCLKConfig(&pclk);

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_I2C1_CLK_ENABLE();

  gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;     /* PB8 SCL, PB9 SDA */
  gpio.Mode = GPIO_MODE_AF_OD;
  gpio.Pull = GPIO_NOPULL;                /* externi pull-upy na FPGA desce */
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &gpio);

  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x70303AEE;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
    Error_Handler();
  }
  HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);
  HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0);
}

/* ── I2C4: dve provozni rychlosti podle ciloveho zarizeni (2026-09-13) ──────
 * ATTINY na desce ma CPU 1 MHz a jako bit-bang I2C slave nestiha nad ~75 kHz
 * (zmereno 2026-09-10/12, docs/audit/2026-09-10_i2c.md — cista tabulka az do
 * 100 kHz, koleno mezi 75 a 100 kHz, ATTINY tam NACKuje 2,3-8 %; FT5x06 a
 * TMP117 na 100 kHz cista nula). Bezpecna rezerva = 50 kHz (I2C4.Timing v
 * .ioc, generovano vyse v MX_I2C4_Init — TAM se to nemeni).
 * FT5x06 (dotyk) a TMP117 (teplota) jsou skutecne I2C periferie, ne bit-bang
 * slave — ATTINY limit na ne neplati, bezi na vyssim takte.
 * ⚠️ 200 kHz je HYPOTEZA, NE zmerene cislo: cista tabulka v auditu sahaji jen
 * do 100 kHz, vse >=125 kHz je oznaceno jako kontaminovane zavesenou
 * sbernici (vada MERICIHO NASTROJE `i2cspeed`/TODO #244, ne cipu — viz
 * "Co tim porad neni zodpovezeno" v audit dokumentu). Overit pred plnou
 * duverou: `i2cspeed` s MALYM N (~25) na krok 200 kHz, cistene na FT5x06 a
 * TMP117 samotne (bez soucasne aktivni ATTINY zateze). ⬜ neovereno na HW.
 * Konstanty `I2C4_TIMING_*` jsou v i2c.h (verejne, potrebuje main.c i oba
 * tasky); `I2C4_TIMING_FAST_200KHZ` = hodnota z tabulky I2CSP_STEP pro
 * 200 kHz (freertos_task_uart.c). */

/* Prepne TIMINGR I2C4 na pozadovanou rychlost — ale JEN kdyz uz na ni neni
 * (touch i TMP117 chteji tutez rychlost, takze v provozu se skoro nikdy
 * neprepina; cenu DeInit+Init platime jen pri zapisu do ATTINY). Bezpecny
 * vzor jako `i2c4_recover()`/`i2csp_set_timing` (freertos_task_uart.c):
 * NIKDY `MX_I2C4_Init` (ma `Error_Handler()` trap), jen DeInit + zmena
 * `Init.Timing` + Init. ⚠️ Volat VZDY pod `i2c4MutexHandle` — `hi2c4` neni
 * thread-safe a dve jadra/tasky by si TIMINGR mohly prepsat pod rukama. */
void i2c4_speed_select(uint32_t timing)
{
    if (hi2c4.Init.Timing == timing) {
        return;
    }
    HAL_I2C_DeInit(&hi2c4);
    hi2c4.Init.Timing = timing;
    hi2c4.State = HAL_I2C_STATE_RESET;
    HAL_I2C_Init(&hi2c4);
}
/* USER CODE END 1 */

