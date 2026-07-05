/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
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
#include "rtc.h"

/* USER CODE BEGIN 0 */
#include "FreeRTOS.h"          /* taskENTER_CRITICAL kolem zapisu g_rtc_text */
#include "task.h"
#include "gps.h"               /* gps_get() — disciplinujeme RTC z GPS UTC */
#include "freertos_shared.h"   /* g_rtc_text / g_rtc_synced (def. ve freertos.c) */
#include <stdio.h>
#include <string.h>

#define RTC_RESYNC_MS   600000u   /* re-sync z GPS kazdych 10 min (drift LSE ~ppm) */
#define RTC_TICK_MS       1000u   /* throttle rtc_app_tick: max 1x/s */

/* Stav synchronizace (privatni, mutuje VYHRADNE defaultTask v rtc_app_tick). */
static uint8_t  s_synced    = 0;   /* 1 = RTC srovnan z GPS (jinak volny beh od bootu) */
static uint32_t s_last_sync = 0;   /* HAL_GetTick posledniho syncu */
static uint32_t s_last_tick = 0;   /* throttle */
static uint8_t  s_bkup_read = 0;   /* 1 = uz jsme po bootu precetli BKP_DR0 */

/* Sanity GPS casu pred zapisem do RTC (parser muze dat 0 pole pri necitelne vete). */
static int gps_time_sane(const gps_data_t *g)
{
  return g->valid &&
         g->year  >= 2024 && g->year  <= 2099 &&
         g->month >= 1    && g->month <= 12 &&
         g->day   >= 1    && g->day   <= 31 &&
         g->hour  <= 23   && g->minute <= 59 && g->second <= 60;
}
/* USER CODE END 0 */

RTC_HandleTypeDef hrtc;

/* RTC init function */
void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
  /* Nacti ulozene UI nastaveni (mode/chan/gate/running) z BKP_DR1 -> g_ui_cfg.
   * Bezi v main() pred schedulerem, takze g_ui_cfg je platny drive nez UiTask
   * poprve kresli (screen_main_init ho rozbali do st). Cteni BKP nevyzaduje DBP;
   * neplatny magic -> g_ui_cfg zustane default z freertos.c. */
  uint32_t uicfg = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1);
  if ((uicfg & 0xFFFFFF00u) == RTC_UICFG_MAGIC) g_ui_cfg = (uint8_t)(uicfg & 0xFFu);

  /* Pokud RTC uz bezel a byl srovnan z GPS (magic v BKP_DR0) a backup domena
   * prezila reset (VBAT/napajeni drzi), NEPREPISUJ cas defaultni 0:00 hodnotou
   * nize — RTC si nese spravny cas dal. Pri studenem startu (BKP=0) se guard
   * minul a defaultni cas se nastavi, dokud ho GPS nesrovna. */
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_SYNC_MAGIC) {
    return;
  }
  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0;
  sTime.Minutes = 0;
  sTime.Seconds = 0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JUNE;
  sDate.Date = 29;
  sDate.Year = 26;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();
  /* USER CODE BEGIN RTC_MspInit 1 */

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* Srovna RTC podle aktualniho GPS fixu (pokud je platny a nadesel cas re-syncu).
 * Vola se jen z rtc_app_tick (defaultTask). */
static void rtc_try_sync(void)
{
  gps_data_t g;
  gps_get(&g);
  if (!gps_time_sane(&g)) return;

  uint32_t now = HAL_GetTick();
  if (s_synced && (now - s_last_sync) < RTC_RESYNC_MS) return;   /* prvni fix hned, pak 10 min */

  RTC_TimeTypeDef t = {0};
  RTC_DateTypeDef d = {0};
  t.Hours   = g.hour;  t.Minutes = g.minute;  t.Seconds = g.second;
  t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  t.StoreOperation = RTC_STOREOPERATION_RESET;
  d.Year    = (uint8_t)(g.year - 2000);
  d.Month   = g.month;
  d.Date    = g.day;
  d.WeekDay = RTC_WEEKDAY_MONDAY;     /* nepouzivame, HAL ale chce platnou hodnotu */

  if (HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return;
  if (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) return;
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_SYNC_MAGIC);   /* prezije warm reset */
  s_synced = 1;
  s_last_sync = now;
}

void rtc_app_tick(void)
{
  uint32_t now = HAL_GetTick();
  if ((now - s_last_tick) < RTC_TICK_MS) return;   /* throttle ~1 Hz */
  s_last_tick = now;

  /* Po bootu zjisti, jestli RTC uz drzi platny cas z minula (backup domena). */
  if (!s_bkup_read) {
    s_bkup_read = 1;
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_SYNC_MAGIC) s_synced = 1;
  }

  rtc_try_sync();

  /* Aktualni RTC cas -> g_rtc_text. POZOR: GetTime MUSI predchazet GetDate
   * (cteni TR odemkne shadow registry, jinak by se DR "zaseklo"). */
  RTC_TimeTypeDef t;
  RTC_DateTypeDef d;
  if (HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return;
  if (HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) return;

  char buf[32];   /* realne 19 zn., ale buf > worst-case 25 -> bez -Wformat-truncation */
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
           2000u + d.Year, d.Month, d.Date, t.Hours, t.Minutes, t.Seconds);

  taskENTER_CRITICAL();
  strncpy((char *)g_rtc_text, buf, sizeof(g_rtc_text) - 1);
  g_rtc_text[sizeof(g_rtc_text) - 1] = '\0';
  g_rtc_synced = s_synced;
  taskEXIT_CRITICAL();
}

/* Ulozi UI nastaveni do BKP_DR1 jen pri zmene. Vola defaultTask (jediny kontext
 * pristupu k RTC/BKP). Dirty se cisti PRED zapisem -> soubezna zmena z UiTasku
 * se neztrati (persistne se pri pristim ticku). */
void rtc_save_uicfg_if_dirty(void)
{
  if (!g_ui_cfg_dirty) return;
  g_ui_cfg_dirty = 0;
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, RTC_UICFG_MAGIC | (uint32_t)g_ui_cfg);
}
/* USER CODE END 1 */

