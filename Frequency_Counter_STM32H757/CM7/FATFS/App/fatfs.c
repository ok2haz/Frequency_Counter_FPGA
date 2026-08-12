/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
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
#include "fatfs.h"

uint8_t retSD;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */

/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the SD driver ###########################*/
  retSD = FATFS_LinkDriver(&SD_Driver, SDPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  /* Casove razitko souboru z RTC (disciplinovaneho z GPS). Bez tohoto by mely
   * vsechny exporty na PC neplatne datum (1980).
   *
   * ⚠️ Vola se z UartTasku (uvnitr f_write/f_close), takze NESMI sahat na
   * HAL_RTC — veskery pristup k RTC registrum je vyhrazen defaultTasku
   * (viz CLAUDE.md „RTC / Vlakno"). Ctem proto uz naformatovany retezec
   * `g_rtc_text_local`, presne jak to dela UI.
   * Lokalni cas je spravne: FAT razitka jsou definovana v lokalnim case a
   * Windows je zobrazuje bez prepoctu.
   * Extern deklarace rucne — tenhle generovany soubor nema USER CODE blok
   * pro include (stejny vzor jako ve fatfs_platform.c). */
  extern volatile char    g_rtc_text_local[24];   /* "YYYY-MM-DD HH:MM:SS" */
  extern volatile uint8_t g_rtc_synced;

  if (!g_rtc_synced || g_rtc_text_local[0] < '0' || g_rtc_text_local[0] > '9') {
    /* RTC jeste nesrovnano z GPS -> pevne 2020-01-01 00:00:00.
     * (0 = rok 1980, coz nektere nastroje hlasi jako poskozeny zaznam.) */
    return ((DWORD)(2020 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
  }

  const volatile char *s = g_rtc_text_local;
  #define D2(i)  (uint32_t)(((s[i] - '0') * 10) + (s[(i) + 1] - '0'))
  uint32_t year = (uint32_t)(((s[0]-'0')*1000) + ((s[1]-'0')*100) + ((s[2]-'0')*10) + (s[3]-'0'));
  uint32_t mon  = D2(5),  day = D2(8);
  uint32_t hh   = D2(11), mi  = D2(14), ss = D2(17);
  #undef D2

  if (year < 1980u) year = 1980u;          /* FAT epocha */
  if (mon < 1u || mon > 12u) mon = 1u;
  if (day < 1u || day > 31u) day = 1u;

  return ((DWORD)(year - 1980u) << 25) | ((DWORD)mon << 21) | ((DWORD)day << 16)
       | ((DWORD)hh << 11) | ((DWORD)mi << 5) | ((DWORD)(ss / 2u));
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
