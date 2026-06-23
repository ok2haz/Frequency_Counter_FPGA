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
extern osMessageQueueId_t UartRxQueueHandle;

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

/* ── Touch event (SensorsTask cte FT5x06 @ I2C4, UiTask drainuje -> render) ── */
extern volatile uint32_t g_touch_xy;       /* (x<<16)|y, display-space (zrcadleno) */
extern volatile uint8_t  g_touch_seq;      /* ++ pri kazdem novem DOWN doteku */

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
