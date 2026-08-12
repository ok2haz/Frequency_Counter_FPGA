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

  /* ⚠️ Retezec prepisuje defaultTask (`strncpy` v rtc_app_tick, 1 Hz) bez zamku,
   * takze cteni z UartTasku muze zastihnout pulku stare a pulku nove hodnoty.
   * Nejhorsi realny dopad je razitko o sekundu vedle, pres pulnoc ale i o den.
   * Zamek se sem nehodi (get_fattime vola FatFs zevnitr f_write). Staci precist
   * dvakrat a shodnout se: pisar tiká 1x za sekundu, takze dve cteni tesne za
   * sebou prakticky nemuzou padnout do dvou ruznych zapisu. Kdyz se lisi,
   * bereme druhou kopii — ta uz je za zapisem. */
  /* Kopiruje se jen 19 znaku formatu + NUL, ne cele pole: `g_rtc_text_local`
   * ma vlastni `extern` deklaraci ve CTYRECH souborech (generovane soubory
   * nemaji USER CODE blok pro include) a nesouhlas velikosti by linker
   * NEODHALIL. Cist min, nez je deklarovano, je proti tomu imunni. */
  char snap[20];
  for (int attempt = 0; attempt < 3; attempt++) {
    char again[20];
    unsigned i;
    for (i = 0; i < sizeof snap;  i++) snap[i]  = g_rtc_text_local[i];
    for (i = 0; i < sizeof again; i++) again[i] = g_rtc_text_local[i];
    for (i = 0; i < sizeof snap;  i++) if (snap[i] != again[i]) break;
    if (i == sizeof snap) break;   /* dve po sobe jdouci cteni shodna -> stabilni */
  }

  const char *s = snap;
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
