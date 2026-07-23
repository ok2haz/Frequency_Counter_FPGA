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
#include "bootled.h"

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
  bootled_step(BOOTLED_STEP_RTC);
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

  /* Systemove nastaveni (jas/mute) z BKP_DR2. Neplatny magic -> default z freertos.c.
   * Platny magic = warm reset (BKP prezila) -> g_syscfg_bkp_valid=1 -> syscfg_load
   * (flash) NEpretahne (BKP je nejnovejsi). Neplatny = studeny start -> flash autorit. */
  uint32_t syscfg = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2);
  if ((syscfg & 0xFFFF0000u) == RTC_SYSCFG_MAGIC) {
    g_brightness  = (uint8_t)(syscfg & 0xFFu);
    g_sound_muted = (uint8_t)((syscfg >> 8) & 0x01u);
    g_autodim_en  = (uint8_t)((syscfg >> 9) & 0x01u);
    uint16_t dsec = (uint16_t)(((syscfg >> 10) & 0x3Fu) * 15u);   /* bity10:15 = s/15 */
    if (dsec >= 15u) g_autodim_sec = dsec;                        /* 0 -> ponech default 60 */
    g_syscfg_bkp_valid = 1;
  }

  /* Systemove nastaveni 2 (schema/jazyk/casova zona) z BKP_DR6. */
  uint32_t syscfg2 = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR6);
  if ((syscfg2 & 0xFFFF0000u) == RTC_SYSCFG2_MAGIC) {
    g_theme_light = (uint8_t)(syscfg2 & 0x01u);
    g_lang_en     = (uint8_t)((syscfg2 >> 1) & 0x01u);
    /* bity2:6 = casova zona kodovana (tz + 13): 1..27 = -12..+14 h.
     * 0 = zaznam z doby PRED touto featurou (bity byly 0) -> ponech default UTC.
     * bit7 = AUTO CET/CEST (ma prednost pred rucnim posunem). */
    uint32_t tzc = (syscfg2 >> 2) & 0x1Fu;
    if (tzc >= 1u && tzc <= 27u) g_tz_offset_h = (int8_t)((int32_t)tzc - 13);
    g_tz_auto = (uint8_t)((syscfg2 >> 7) & 0x01u);
    g_anim_enabled = (uint8_t)((syscfg2 >> 8) & 0x01u);   /* bit8 = animace ZAP/VYP (okno Animace) */
    g_digit_anim_enabled = (uint8_t)((syscfg2 >> 9) & 0x01u);  /* bit9 = zvyrazneni cislic ZAP/VYP */
  }

  /* Crash black-box (BKP_DR3..5, zapsal FreeRTOS hook pred IWDG resetem):
   * kind + jmeno tasku -> g_crash_text ("stack:UiTask" / "malloc fail"), pak
   * smazat (reportuje se jen jednou, do dalsiho crashe). */
  uint32_t cr = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3);
  if ((cr & 0xFFFF0000u) == RTC_CRASH_MAGIC) {
    char name[9];
    uint32_t n0 = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR4);
    uint32_t n1 = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR5);
    for (int i = 0; i < 4; i++) { name[i] = (char)(n0 >> (8 * i)); name[4 + i] = (char)(n1 >> (8 * i)); }
    name[8] = '\0';
    uint32_t kind = (cr & 0xFFu);
    if (kind == 1u)
      snprintf((char *)g_crash_text, sizeof(g_crash_text), "stack:%s", name);
    else if (kind == 3u)
      /* Zapsal watchdog_supervise tesne pred vyprsenim IWDG: task prestal koupat
       * (heartbeat > 2,5 s). Bez tohoto by byl prosty watchdog reset nemy. */
      snprintf((char *)g_crash_text, sizeof(g_crash_text), "stall:%s", name);
    else if (kind == 4u) {
      /* HardFault: DR4 (=n0) = stacknuty LR = CALLER (funkce, ktera udelala
       * spatny call -> addr2line na .elf), DR5 (=n1) = SCB->CFSR (typ). Ukaz
       * adresu + 1 pismeno typu: B=BusFault, U=UsageFault, M=MemManage. */
      char t = (n1 & 0x0000FF00u) ? 'B'
             : (n1 & 0xFFFF0000u) ? 'U'
             : (n1 & 0x000000FFu) ? 'M' : '?';
      snprintf((char *)g_crash_text, sizeof(g_crash_text), "HF@%08lX%c",
               (unsigned long)n0, t);
    }
    else
      snprintf((char *)g_crash_text, sizeof(g_crash_text), "malloc fail");
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR3, 0u);
  }

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

/* Pocet dni v mesici (gregoriansky kalendar vc. prestupnych let). */
static uint8_t rtc_month_days(uint16_t y, uint8_t m)
{
  static const uint8_t md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return md[m - 1];
}

/* Aplikuje posun casove zony [h] na UTC datum+cas, vc. prehoupnuti pres
 * pulnoc/mesic/rok obema smery. Posun je celociselny pocet hodin -> minuty
 * a sekundy se nemeni. */
static void rtc_apply_tz(uint16_t *y, uint8_t *mo, uint8_t *d, int *h, int tz)
{
  *h += tz;
  if (*h >= 24) {
    *h -= 24;
    if (++(*d) > rtc_month_days(*y, *mo)) { *d = 1; if (++(*mo) > 12) { *mo = 1; (*y)++; } }
  } else if (*h < 0) {
    *h += 24;
    if (--(*d) < 1) { if (--(*mo) < 1) { *mo = 12; (*y)--; } *d = rtc_month_days(*y, *mo); }
  }
}

/* Den v tydnu (Sakamotova metoda), 0 = nedele. Gregoriansky kalendar. */
static int rtc_dow(int y, int m, int d)
{
  static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

int rtc_cest_active(uint16_t y, uint8_t month, uint8_t day, uint8_t hour_utc)
{
  /* EU: CEST od posledni nedele brezna 01:00 UTC do posledni nedele rijna
   * 01:00 UTC. Posledni nedele mesice s L dny = L - dow(y, m, L). */
  if (month > 3 && month < 10) return 1;    /* duben..zari = vzdy CEST */
  if (month < 3 || month > 10) return 0;    /* listopad..unor = vzdy CET */
  int last_sun = 31 - rtc_dow((int)y, (int)month, 31);   /* brezen i rijen maji 31 dni */
  if (month == 3)
    return (day > last_sun) || (day == last_sun && hour_utc >= 1);
  /* month == 10 */
  return (day < last_sun) || (day == last_sun && hour_utc < 1);
}

bool rtc_selftest(void)
{
  int ok = 1;
  /* rtc_apply_tz: prehoupnuti pres pulnoc / mesic / rok / prestupny unor. */
  struct { uint16_t y; uint8_t mo, d; int h, tz;
           uint16_t ey; uint8_t emo, ed; int eh; } V[] = {
    { 2026, 12, 31, 23, +2,  2027,  1,  1,  1 },   /* pres rok nahoru */
    { 2026,  1,  1,  0, -1,  2025, 12, 31, 23 },   /* pres rok dolu */
    { 2026,  2, 28, 23, +2,  2026,  3,  1,  1 },   /* neprestupny unor */
    { 2024,  2, 28, 23, +2,  2024,  2, 29,  1 },   /* prestupny unor */
    { 2026,  3,  1,  0, -1,  2026,  2, 28, 23 },   /* dolu do unora */
    { 2026,  7, 15, 12,  0,  2026,  7, 15, 12 },   /* nulovy posun */
  };
  for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++) {
    uint16_t y = V[i].y; uint8_t mo = V[i].mo, d = V[i].d; int h = V[i].h;
    rtc_apply_tz(&y, &mo, &d, &h, V[i].tz);
    if (y != V[i].ey || mo != V[i].emo || d != V[i].ed || h != V[i].eh) ok = 0;
  }
  /* rtc_cest_active: hranice DST 2026 (posledni nedele = 29.3. / 25.10.). */
  ok &= (rtc_cest_active(2026,  3, 29, 0) == 0);   /* tesne pred prechodem */
  ok &= (rtc_cest_active(2026,  3, 29, 1) == 1);   /* prechod na CEST */
  ok &= (rtc_cest_active(2026,  7, 15, 12) == 1);  /* leto */
  ok &= (rtc_cest_active(2026, 10, 25, 0) == 1);   /* tesne pred navratem */
  ok &= (rtc_cest_active(2026, 10, 25, 1) == 0);   /* navrat na CET */
  ok &= (rtc_cest_active(2026,  1, 10, 12) == 0);  /* zima */
  return ok != 0;
}

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

  /* Lokalni cas dle zvolene zony (Nastaveni): rucni posun g_tz_offset_h, nebo
   * AUTO CET/CEST (EU pravidlo z UTC data). Cele hodiny, prehoupnuti data. */
  int tz; char tzl[8];
  if (g_tz_auto) {
    int cest = rtc_cest_active((uint16_t)(2000u + d.Year), d.Month, d.Date, t.Hours);
    tz = cest ? 2 : 1;
    snprintf(tzl, sizeof(tzl), "%s", cest ? "CEST" : "CET");
  } else {
    tz = (int)g_tz_offset_h;
    if (tz == 0) snprintf(tzl, sizeof(tzl), "UTC");
    else         snprintf(tzl, sizeof(tzl), "UTC%+d", tz);
  }
  uint16_t ly = (uint16_t)(2000u + d.Year);
  uint8_t  lm = d.Month, ld = d.Date;
  int      lh = (int)t.Hours;
  rtc_apply_tz(&ly, &lm, &ld, &lh, tz);
  char lbuf[32];
  snprintf(lbuf, sizeof(lbuf), "%04u-%02u-%02u %02u:%02u:%02u",
           (unsigned)ly, (unsigned)lm, (unsigned)ld, (unsigned)lh, t.Minutes, t.Seconds);

  taskENTER_CRITICAL();
  strncpy((char *)g_rtc_text, buf, sizeof(g_rtc_text) - 1);
  g_rtc_text[sizeof(g_rtc_text) - 1] = '\0';
  strncpy((char *)g_rtc_text_local, lbuf, sizeof(g_rtc_text_local) - 1);
  g_rtc_text_local[sizeof(g_rtc_text_local) - 1] = '\0';
  strncpy((char *)g_tz_label, tzl, sizeof(g_tz_label) - 1);
  g_tz_label[sizeof(g_tz_label) - 1] = '\0';
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

/* Ulozi systemove nastaveni (jas + mute + auto-dim) do BKP_DR2 jen pri zmene. Viz rtc.h. */
void rtc_save_syscfg_if_dirty(void)
{
  if (!g_sys_cfg_dirty) return;
  g_sys_cfg_dirty = 0;
  uint32_t v = RTC_SYSCFG_MAGIC | (uint32_t)g_brightness
             | ((uint32_t)(g_sound_muted ? 1u : 0u) << 8)
             | ((uint32_t)(g_autodim_en ? 1u : 0u) << 9)
             | (((uint32_t)(g_autodim_sec / 15u) & 0x3Fu) << 10);   /* bity10:15 = s/15 */
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, v);
  uint32_t v2 = RTC_SYSCFG2_MAGIC
              | ((uint32_t)(g_theme_light ? 1u : 0u))
              | ((uint32_t)(g_lang_en ? 1u : 0u) << 1)
              | ((((uint32_t)((int32_t)g_tz_offset_h + 13)) & 0x1Fu) << 2)   /* bity2:6 = tz+13 (1..27) */
              | ((uint32_t)(g_tz_auto ? 1u : 0u) << 7)                       /* bit7 = AUTO CET/CEST */
              | ((uint32_t)(g_anim_enabled ? 1u : 0u) << 8)                  /* bit8 = animace ZAP/VYP */
              | ((uint32_t)(g_digit_anim_enabled ? 1u : 0u) << 9);           /* bit9 = zvyrazneni cislic */
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR6, v2);
}
/* USER CODE END 1 */

