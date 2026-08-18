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
#include <stdbool.h>   /* rtc_selftest */
/* USER CODE END Includes */

extern RTC_HandleTypeDef hrtc;

/* USER CODE BEGIN Private defines */
/* Magic v BKP_DR0 = "RTC uz byl srovnan z GPS". Pouziva ho guard v MX_RTC_Init
 * (USER CODE Check_RTC_BKUP) i rtc_app_tick — proto je tady, ne v .c. */
#define RTC_SYNC_MAGIC   0x32F2u
/* BKP_DR1: horni 3 bajty = magic (platne ulozene UI nastaveni), spodni bajt = packed
 * config (bit0 mode, bit1 chan, bity2:3 gate, bit4 running). Persist pres warm reset. */
#define RTC_UICFG_MAGIC  0x5AC0DE00u
/* BKP_DR2: horni 16 bitu = magic, spodni 16 = systemove nastaveni
 * (bity7:0 jas 0-255, bit8 mute, bit9 auto-dim en, bity10:15 auto-dim prodleva
 * v nasobcich 15 s). Persist pres warm reset. */
#define RTC_SYSCFG_MAGIC 0x53C00000u
/* BKP_DR3..DR5: crash black-box (zapisuji FreeRTOS hooky pred spinem -> IWDG
 * reset; MX_RTC_Init po bootu precte, vystavi g_crash_text a smaze).
 * DR3 = magic | kind (1 = stack overflow, 2 = malloc fail), DR4+DR5 = 8 znaku
 * jmena tasku (little-endian po 4). */
#define RTC_CRASH_MAGIC  0xC7A50000u
/* BKP_DR6: systemove nastaveni 2 (DR2 payload je plny) — bit0 svetle schema,
 * bit1 english, bity2:6 casova zona (tz+13 -> 1..27 = -12..+14 h; 0 = legacy
 * zaznam -> UTC), bit7 = AUTO CET/CEST (EU pravidlo, ignoruje bity2:6),
 * bit8 = animace ZAP/VYP (okno Animace), bit9 = zvyrazneni cislic ZAP/VYP.
 * Persist pres warm reset.
 * ⚠️ Magic zvednut 0x53C10000 -> 0x53C20000 kdyz pribyly anim bity 8/9:
 * legacy zaznam (magic 0x53C1) mel bity 8/9 = 0 a NEmel legacy-guard jako
 * casova zona -> pri warm resetu vypnul animace (default je ZAP). Bump zajisti,
 * ze stary zaznam se odmitne cely -> uplatni se defaulty (anim ON). Jednorazovy
 * dusledek: prvni warm reset po teto zmene vrati i schema/jazyk/zonu na default
 * (na cold bootu jsou stejne autoritativni z flash SCF4). */
#define RTC_SYSCFG2_MAGIC 0x53C20000u
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

/* Ulozi UI nastaveni (g_ui_cfg) do BKP_DR1, jen pokud g_ui_cfg_dirty (setri BKP).
 * Volat z defaultTask (jediny kontext pristupu k RTC/BKP). */
void rtc_save_uicfg_if_dirty(void);

/* Ulozi systemove nastaveni (jas/mute/auto-dim) do BKP_DR2, jen pokud
 * g_sys_cfg_dirty. Volat z defaultTask (jediny kontext pristupu k RTC/BKP). */
void rtc_save_syscfg_if_dirty(void);

/* EU pravidlo letniho casu: CEST (UTC+2) od posledni nedele brezna 01:00 UTC
 * do posledni nedele rijna 01:00 UTC, jinak CET (UTC+1). Cista funkce (bez HW)
 * -> pouziva ji rtc_app_tick, okno Nastaveni (zivy label) i selftest.
 * @return 1 = plati CEST, 0 = CET. Vstup = UTC datum + hodina. */
int rtc_cest_active(uint16_t y, uint8_t month, uint8_t day, uint8_t hour_utc);

/* Pure-logic selftest kalendarni matematiky (rtc_apply_tz prehoupnuti pres
 * pulnoc/mesic/rok/prestupny unor + rtc_cest_active hranice DST). Bez HW. */
bool rtc_selftest(void);

/* ══════════════ Disciplinace LSE podle GPS (mereni driftu vlastniho krystalu) ══
 * RTC bezi z krystalu LSE 32,768 kHz a kazdych 10 minut se srovnava podle GPS.
 * Ta odchylka, kterou pritom zahazoval, JE MERENI — drift vlastni casove
 * zakladny v ppm. Je to funkcni miniatura celeho GPSDO (zmer chybu proti GNSS,
 * aplikuj korekci, drz holdover), jen na RTC misto OCXO — a nepotrebuje ani
 * FPGA, ani jediny novy soucastku.
 *
 * ── Jak se to meri ─────────────────────────────────────────────────────────
 * GPS dava jen CELE sekundy, takze se faze snima na HRANE GPS sekundy: v tom
 * okamziku se precte sub-sekundovy registr RTC (SynchPrediv=255 -> 1/256 s =
 * 3,9 ms). Drift = (faze_ted - faze_referencni) / uplynuly_cas.
 * ⚠️ NMEA veta dorazi s latenci po skutecne sekunde (UART + parsovani), ale ta
 * je pribliznе KONSTANTNI, takze se v ROZDILU dvou bodu vyrusi. Prave proto se
 * meri rozdil dvou fazi, ne absolutni faze.
 * ⚠️ Jedno 10minutove okno ma sum ~10-20 ppm (rozliseni + jitter defaultTasku).
 * Pouzitelne cislo dava az BEZICI PRUMER: ~1,5 ppm po 6 h, ~1 ppm po dni. */

/** Bezici prumer driftu LSE [ppm]; kladne = RTC bezi NAPRED. */
float    rtc_lse_ppm(void);
/** Posledni jednotlive okno [ppm] (sumove, jen orientacne). */
float    rtc_lse_ppm_last(void);
/** Pocet zmerenych oken v prumeru (0 = jeste nic; duveryhodne od ~12). */
uint32_t rtc_lse_windows(void);
/** Aktualne zapsana korekce do RTC_CALR [ppm]; 0 = zadna. */
float    rtc_lse_calib_ppm(void);
/** Zahodi namerenou statistiku a zacne merit znovu (UART `rtc cal reset`). */
void     rtc_lse_reset(void);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __RTC_H__ */

