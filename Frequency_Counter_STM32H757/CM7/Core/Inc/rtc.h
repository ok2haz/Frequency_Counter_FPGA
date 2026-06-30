/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.h
  * @brief   This file contains all the function prototypes for
  *          the rtc.c file
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
#ifndef __RTC_H__
#define __RTC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern RTC_HandleTypeDef hrtc;

/* USER CODE BEGIN Private defines */
/* Magic v BKP_DR0 = "RTC uz byl srovnan z GPS". Pouziva ho guard v MX_RTC_Init
 * (USER CODE Check_RTC_BKUP) i rtc_app_tick — proto je tady, ne v .c. */
#define RTC_SYNC_MAGIC   0x32F2u
/* USER CODE END Private defines */

void MX_RTC_Init(void);

/* USER CODE BEGIN Prototypes */
/* Aplikacni vrstva (regen-safe, telo v rtc.c USER CODE 1):
 * volat periodicky z defaultTask. Throttle ~1 Hz uvnitr. Dela:
 *   1) sync RTC z GPS UTC (prvni fix hned, pak re-sync po RTC_RESYNC_MS),
 *   2) naformatuje aktualni RTC cas do g_rtc_text + nastavi g_rtc_synced.
 * VESKERY pristup k RTC registrum je VYHRADNE odsud (defaultTask) -> UART/UI
 * ctou jen sdilene g_rtc_text/g_rtc_synced (zadna cross-task HAL_RTC kolize). */
void rtc_app_tick(void);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __RTC_H__ */

