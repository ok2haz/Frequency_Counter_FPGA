/*
 * freertos_shared.h
 *
 * Sdílený stav a prototypy pro FreeRTOS tasky rozdělené z freertos.c.
 * Globály jsou DEFINOVANÉ v freertos.c (USER CODE Variables); zde jen extern.
 * Task entry pointy jsou implementované v freertos_task_*.c.
 */

#ifndef INC_FREERTOS_SHARED_H_
#define INC_FREERTOS_SHARED_H_

#include <stdint.h>
#include "cmsis_os2.h"
#include "sensor_stat.h"   /* g_sensors[], sensor_update/fail (teploty + ADS1115) */

/* ── Synchronizace (definováno v freertos.c) ───────────────────────────── */
extern osMutexId_t i2c4MutexHandle;        /* I2C4: TMP117 0x48 + touch + backlight */
extern osMutexId_t i2c1MutexHandle;        /* I2C1: FPGA deska (TMP117 x2, ADS1115, Si5356) */
extern osMutexId_t uartTxMutexHandle;      /* serializace printf/_write */
/* QSPI (W25Q): defaultTask (syscfg auto-save) + UiTask (calib_save, JEDEC v oknech)
 * + UartTask (qspi* prikazy). ⚠️ Zamykej celou LOGICKOU operaci (w25q_store_write =
 * erase+payload+hlavicka), ne jednotliva w25q_* volani. Doporucene timeouty:
 * defaultTask kratky (~10 ms, nesmi zdrzet watchdog — pri neuspechu zkusi priste),
 * UiTask/UART delsi (erase az ~400 ms). */
extern osMutexId_t qspiMutexHandle;
extern osMessageQueueId_t UartRxQueueHandle;
extern osMessageQueueId_t GpsRxQueueHandle;   /* USART1 RX -> GpsTask (NEO-7M NMEA) */

/* ── Teploty / ADS1115 ──────────────────────────────────────────────────
 * Hodnota + platnost + statistika jsou v g_sensors[] (viz sensor_stat.h).
 * Zapisuje SensorsTask přes sensor_update()/sensor_fail(); čte UI + UART. */

/* ── Požadavek na obrazovku (UART -> UiTask): 3 = main, 4 = clear ──────── */
extern volatile uint8_t g_screen_req;

/* ── Kmitočet z FPGA (FpgaTask -> UiTask) ──────────────────────────────── */
extern volatile char    g_freq_text[48];
extern volatile char    g_freq_info[64];
extern volatile uint8_t g_freq_dirty;
extern volatile uint8_t g_freq_stale;      /* 1 = ztráta signálu -> UI ztlumí */

/* ── Stav SPI/FPGA (FpgaTask -> UiTask) ────────────────────────────────── */
extern volatile char    g_spi_text[64];
extern volatile uint8_t g_spi_ok;          /* 1 = link živá -> zeleně */
extern volatile uint8_t g_spi_dirty;

/* ── Si5356 reference (zapisuje SensorsTask z I2C1, čte diagnostika) ────── */
extern volatile uint8_t g_si5356_status;   /* reg 218: bit0 SYS_CAL, bit2 LOS_CLKIN, bit4 PLL_LOL */
extern volatile uint8_t g_si5356_ok;       /* 1 = status úspěšně přečten */

/* ── RTC (zapisuje defaultTask přes rtc_app_tick, čte UART/UI) ───────────────
 * RTC běží z LSE (32.768 kHz), disciplinuje se z GPS UTC. Text "YYYY-MM-DD HH:MM:SS".
 * g_rtc_synced: 1 = už srovnán z GPS, 0 = volný běh od bootu. */
extern volatile char    g_rtc_text[24];
extern volatile uint8_t g_rtc_synced;
/* Lokalni cas dle casove zony (Nastaveni): rtc_app_tick aplikuje g_tz_offset_h
 * na UTC (vc. prehoupnuti data) -> g_rtc_text_local + g_tz_label ("UTC"/"UTC+2").
 * Hlavni obrazovka + screensaver ctou local; GPS okno + diag zustavaji UTC. */
extern volatile char    g_rtc_text_local[24];
extern volatile char    g_tz_label[8];
extern volatile int8_t  g_tz_offset_h;    /* -12..+14 h, persist BKP_DR6 (UiTask pise, defaultTask cte) */
extern volatile uint8_t g_tz_auto;        /* 1 = AUTO CET/CEST (EU pravidlo), persist BKP_DR6 bit7 */

/* ── Uložené UI nastavení (persist v RTC BKP_DR1) ───────────────────────────
 * bit0 mode, bit1 chan, bity2:3 gate, bit4 running. UiTask (screen_main_button_action)
 * ho přepakuje při změně tlačítka + nastaví dirty; defaultTask (rtc_save_uicfg_if_dirty)
 * zapíše do BKP. Načtení z BKP dělá MX_RTC_Init před schedulerem. */
extern volatile uint8_t g_ui_cfg;
extern volatile uint8_t g_ui_cfg_dirty;

/* ── Systemove nastaveni (persist v RTC BKP_DR2) ────────────────────────────
 * Jas displeje (0-255, backlight PWM pres ATTINY), globalni mute zvuku a auto-dim
 * (ztlumeni po necinnosti). UiTask (okno Nastaveni) meni + nastavi dirty;
 * defaultTask (rtc_save_syscfg_if_dirty) zapise do BKP. Nacteni z BKP dela
 * MX_RTC_Init pred schedulerem. */
extern volatile uint8_t g_brightness;    /* jas 0-255 (default 200) */
extern volatile uint8_t g_sound_muted;   /* 1 = zvuk vypnut (default 0) */
extern volatile uint8_t g_autodim_en;    /* 1 = auto-dim po necinnosti (default 1) */
extern volatile uint16_t g_autodim_sec;  /* prodleva auto-dim [s] (default 60, preset 15..600) */
extern volatile uint8_t g_theme_light;   /* 0 = tmave schema (default), 1 = svetle (BKP_DR6) */
extern volatile uint8_t g_lang_en;       /* 0 = cesky (default), 1 = english (BKP_DR6) */
extern volatile uint8_t g_sys_cfg_dirty; /* 1 = zmena -> defaultTask ulozi do BKP */
extern volatile uint8_t g_reboot_req;    /* 1 = softwarovy restart (Menu->Restart); provede defaultTask */
extern volatile uint8_t g_syscfg_bkp_valid; /* 1 = syscfg BKP platna pri bootu (warm reset) -> syscfg_load netahne z flash */

/* ── Diagnostika resetu + selftest (24/7 auto-recovery reporting) ───────────
 * g_reset_rsr = RCC->RSR zachycene v main.c (pak RMVF clear -> priste cerstve).
 * g_crash_text = crash black-box z BKP_DR3..5 ("stack:UiTask" / "malloc fail"),
 * prazdny = zadny zaznam. g_selftest_res: 0 = nespusten, 1 = PASS, 2 = FAIL. */
extern volatile uint32_t g_reset_rsr;
extern volatile char     g_reset_text[12];  /* dekodovana pricina ("WATCHDOG!"...) */
extern volatile uint8_t  g_reset_bad;       /* 1 = IWDG/WWDG (cervene v Health) */
extern volatile char     g_crash_text[16];
extern volatile uint8_t  g_selftest_res;
/* Per-test vysledky selftestu (0=nespusten, 1=PASS, 2=FAIL). Poradi = poradi
 * volani v run_selftests: [0]=CRC16, [1]=hystereze /4<->/16, [2]=GPS parser,
 * [3]=format/histogram (screen_main), [4]=Maidenhead lokator (app_gpsdo),
 * [5]=kalendar/DST (rtc_selftest).
 * Zobrazuje okno Selftest (menu) — pri zmene poradi aktualizuj i jeho popisky. */
#define SELFTEST_N 6
extern volatile uint8_t  g_selftest_detail[SELFTEST_N];
/* g_cm4_absent = 1: CM4 (domena D2) nenabehl behem boot handshake (prazdna/vadna
 * bank2 nebo BCM4=0 v option bytes). Displej bezi na CM7 -> pokracujeme degradovane
 * misto tiche tmy (Error_Handler). Ukazuje se v Health karte System + SYS pill amber. */
extern volatile uint8_t  g_cm4_absent;
int run_selftests(void);   /* pure-logic testy (boot + UART "selftest"); 1 = vse OK */

/* ── RTOS zdraví (zapisuje UiTask ~2×/s, čte diagnostika) ───────────────── */
extern volatile uint32_t g_rtos_heap_free; /* xPortGetFreeHeapSize() [B] */
extern volatile uint32_t g_rtos_heap_min;  /* min-ever-free heap [B] */
extern volatile uint32_t g_rtos_cpu_pct;   /* zátěž CPU [%] (100 - idle) */
extern volatile uint32_t g_uptime_s;       /* doba běhu [s] */

/* ── Task implementace ─────────────────────────────────────────────────── */
/* CubeMX generuje StartUartTask/StartI2C4 stuby ve freertos.c; jejich USER CODE
 * tělo jen zavolá tyto implementace -> CubeMX regen build NErozbije (žádná
 * duplicita symbolu, jména se nekryjí s generovanými StartXxx). */
void UartTask_run(void *argument);         /* freertos_task_uart.c */
void SensorsTask_run(void *argument);      /* freertos_task_sensors.c */

/* Ručně vytvořené tasky (osThreadNew v USER CODE RTOS_THREADS) -> CubeMX je
 * negeneruje, definované přímo v split souborech. */
void StartUiTask(void *argument);          /* freertos_task_ui.c */
void StartFpgaTask(void *argument);        /* freertos_task_fpga.c */

/* ── Run-time stats časová báze (freertos_hooks.c) ─────────────────────── */
void RunTimeStats_Init(void);
uint32_t RunTimeStats_GetCount(void);

#endif /* INC_FREERTOS_SHARED_H_ */
