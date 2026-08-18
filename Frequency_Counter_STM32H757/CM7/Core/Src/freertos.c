/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>            /* printf v MX_FREERTOS_Init */
#include "freertos_shared.h"  /* sdilene globaly + task prototypy (tasky jsou ve freertos_task_*.c) */
#include "gps.h"              /* gps_init/gps_feed_char — drain v defaultTask */
#include "rtc.h"              /* rtc_app_tick — sync RTC z GPS v defaultTask */
#include "syscfg.h"           /* syscfg_flash_tick — zrcadlo nastaveni do W25Q flash */
#include "datalog.h"          /* datalog_init/tick — zaznam stability do W25Q DATA (TODO #6) */
#include "flightrec.h"        /* flightrec_init/tick — kontext pred resetem (TODO #18) */
#include "sd_export.h"        /* sd_export_tick — detekce SD karty + auto-unmount (#28) */
#include "alarm.h"            /* alarm_tick — zvukovy alarm (SIGNAL_LOST/GPS) */
#include "watchdog.h"         /* watchdog_supervise — IWDG refresh dle heartbeatu */
#include "fpga_freq.h"        /* fpga_freq_*_selftest — run_selftests() */
#include "screens/screen_main.h"   /* screen_main_selftest — run_selftests() */
#include "app_gpsdo.h"        /* app_gpsdo_selftest (Maidenhead lokator) */
#include "meas_math.h"        /* meas_math_selftest — Math/limity (#43/#44) */
#include "meas_present.h"     /* mp_selftest — prezentace mereni (#67) */
#include "scpi.h"             /* scpi_selftest — SCPI parser (#25) */
#include "setup.h"            /* setup_selftest — sanitizace sestavy */
#include "autocal.h"          /* autocal_selftest — verdikt self-checku */
#include "ipc_shared.h"       /* ipc_init/publish/service/selftest — IPC CM7<->CM4 (#19/#20) */
#include "usb_console.h"      /* usb_console_tx_pump — dopumpovani CDC TX ringu */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* Makra tasku (RX_BUF_SIZE, RAM/SDRAM test adresy, …) jsou v jejich
 * freertos_task_*.c, kde se používají. */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* Pozn.: RxBuffer/RxIndex a ram_buf/sdram_buf jsou privatni ve freertos_task_uart.c.
 * Globaly nize jsou sdilene (extern ve freertos_shared.h) -> definice zustavaji zde. */
/* Senzory: 3× TMP117 (0x48 I2C4 displej, 0x49/0x4A I2C1 FPGA deska) + 4× ADS1115
 * (AIN0/1 primo, AIN2=12V vetev, AIN3=5V vetev po prepoctu delice). Hodnota +
 * platnost + statistika (min/max/avg) + citace chyb -> viz sensor_stat.h.
 * Zapisuje SensorsTask pres sensor_update()/sensor_fail(). Nulova init je OK
 * (samples=0 -> min/max/mean se lazy-inicializuji prvnim platnym vzorkem). */
sensor_stat_t g_sensors[SENS_COUNT] = {0};

/* Popisná metadata senzorů (label + jednotka) — jediný zdroj pravdy (UART `sensors`,
 * příp. UI). Pořadí = sensor_id_t (viz sensor_stat.h). */
const sensor_desc_t g_sensor_desc[SENS_COUNT] = {
  { "TMP117 0x48",   "C"  },   /* SENS_T48   */
  { "TMP117 0x49",   "C"  },   /* SENS_T49   */
  { "TMP117 0x4A",   "C"  },   /* SENS_T4A   */
  { "ADS AIN0",      "mV" },   /* SENS_ADS0  */
  { "ADS AIN1",      "mV" },   /* SENS_ADS1  */
  { "ADS AIN2(12V)", "mV" },   /* SENS_ADS2  */
  { "ADS AIN3(5V)",  "mV" },   /* SENS_ADS3  */
  { "MCU jadro",     "C"  },   /* SENS_CORE_T */
  { "VREF",          "mV" },   /* SENS_VDDA (VREF+) */
  { "VBAT",          "mV" },   /* SENS_VBAT  */
};

/* Mutex pro sdileni I2C4 sbernice (TMP117 task + dotykove UI + backlight) */
osMutexId_t i2c4MutexHandle;

/* Mutex pro I2C1 sbernici (sensor task + scan1 prikaz) */
osMutexId_t i2c1MutexHandle;

/* Mutex pro UART TX (printf z vice tasku se nesmi michat) - pouziva _write v main.c */
osMutexId_t uartTxMutexHandle;
/* QSPI (W25Q) — od 2026-07-18 na flash sahaji TRI kontexty: defaultTask
 * (syscfg_flash_tick auto-save), UiTask (calib_save na ULOZIT, w25q_read_jedec
 * v oknech Pamet/Datalog) a UartTask (qspiid/qspitest/storetest/qspispeed).
 * ⚠️ Zamyka se na urovni LOGICKE operace, ne jednotlivych w25q_* volani —
 * w25q_store_write = erase + zapis payloadu + zapis hlavicky a ty MUSI probehnout
 * jako celek (jinak by soubezny zapis rozbil power-safe poradi). */
osMutexId_t qspiMutexHandle;

/* Pozadavek na obrazovku: UART nastavi, UiTask obslouzi (libprim/libui neni
 * thread-safe -> kresli VYHRADNE UiTask). 3 = "screen main", 4 = "clear". */
volatile uint8_t g_screen_req  = 0;

/* Pozadavek na reset Allan/Histogram/Trend akumulace (UART "meas reset" ->
 * UiTask, viz screen_main_stats_reset). Stejny duvod jako g_screen_req. */
volatile uint8_t g_stats_reset_req = 0;

/* Naformatovany kmitocet z FPGA (FpgaTask zapise, UiTask vykresli) */
volatile char    g_freq_text[48] = "----------,-----Hz";
volatile char    g_freq_info[64] = "";                 /* vedlejsi udaje (gate/edges/ch) */
volatile uint8_t g_freq_dirty = 1;
volatile uint8_t g_freq_stale = 0;                     /* 1 = FPGA SIGNAL_LOST flag nebo mrtvy link -> UI ztlumi, alarm pipne */

/* Stav SPI + komunikace s FPGA (FpgaTask zapise, UiTask vykresli) */
volatile char    g_spi_text[64] = "SPI WAIT";
volatile uint8_t g_spi_ok    = 0;                      /* 1 = link ziva -> zelene */
volatile uint8_t g_spi_dirty = 1;

/* Si5356 reference (SensorsTask zapise z I2C1, diagnostika cte) */
volatile uint8_t g_si5356_status = 0;
volatile uint8_t g_si5356_ok     = 0;

/* RTC cas (defaultTask zapise pres rtc_app_tick, UART/UI cte).
 * ⚠️ UI ctenari (UiTask: screen_main/rtc_time_date, app_gpsdo diag/gps) ctou BEZ
 * zamku — ZAMERNE: torn read je mozny jen v ~200ns okne zapisu 1x/s a projevi se
 * nejvyse 100ms kosmetickym glitchem casu (dalsi tick prekresli spravne). Zadna
 * korupce (ctenari kopiruji do lokalu + nulou ukoncuji). UART cesta cte pod
 * taskENTER_CRITICAL (tam vadi i jednorazovy vypis). */
volatile char    g_rtc_text[24]  = "---------- --:--:--";   /* presny tvar: [0..9]=datum [11..18]=cas */
volatile uint8_t g_rtc_synced    = 0;
/* Lokalni cas dle zvolene zony (rtc_app_tick aplikuje g_tz_offset_h na UTC,
 * vc. prehoupnuti data). Hlavni obrazovka + screensaver; GPS okno/diag = UTC. */
volatile char    g_rtc_text_local[24] = "---------- --:--:--";

/* Sitova konfigurace — vychozi: DHCP zapnuty, staticke hodnoty jen jako rozumny
 * vychozi bod, kdyby ho uzivatel vypnul. Viz freertos_shared.h. */
volatile uint8_t  g_net_dhcp = 1u;
volatile uint32_t g_net_ip   = 0xC0A80164u;   /* 192.168.1.100 */
volatile uint32_t g_net_mask = 0xFFFFFF00u;   /* 255.255.255.0 */
volatile uint32_t g_net_gw   = 0xC0A80101u;   /* 192.168.1.1 */
volatile char    g_tz_label[8]   = "UTC";   /* "UTC" / "UTC+2" / "CET" / "CEST" (pise rtc_app_tick) */
volatile int8_t  g_tz_offset_h   = 0;       /* rucni posun od UTC [h], -12..+14, persist BKP_DR6 */
volatile uint8_t g_tz_auto       = 0;       /* 1 = AUTO CET/CEST (EU pravidlo), persist BKP_DR6 bit7 */

/* Ulozene UI nastaveni (persist v BKP_DR1): default = {FREQ, CH B, GATE 1s, RUN}
 * = bit1(chan=1) | bit2(gate=1) | bit4(run=1) = 0x16. MX_RTC_Init ho prepise
 * z BKP, pokud tam je platny magic (warm reset s drzenou backup domenou). */
volatile uint8_t g_ui_cfg        = 0x16;
volatile uint8_t g_ui_cfg_dirty  = 0;

/* Systemove nastaveni (jas + mute + auto-dim), persist v BKP_DR2. MX_RTC_Init prepise z BKP. */
volatile uint8_t g_brightness    = 200;   /* backlight PWM (ATTINY) */
/* Ulozeny vysledek self-survey (persist syscfg flash; viz freertos_shared.h). */
volatile uint8_t  g_survey_valid  = 0;
volatile uint32_t g_survey_n      = 0;
volatile double   g_survey_lat = 0.0, g_survey_lon = 0.0;
volatile float    g_survey_alt = 0.0f, g_survey_spread = 0.0f;
volatile uint8_t g_sound_muted   = 0;     /* 0 = zvuk zapnut */
volatile uint8_t g_autodim_en    = 1;     /* 1 = auto-dim po necinnosti zapnut */
volatile uint16_t g_autodim_sec  = 300;   /* prodleva auto-dim [s] = 5 min default (preset 15..600) */
volatile uint8_t g_theme_light   = 0;     /* 0 = tmave schema (default) */
volatile uint8_t g_lang_en       = 0;     /* 0 = cesky (default) */
volatile uint8_t g_anim_enabled  = 1;     /* 1 = animace zapnute (default), persist BKP_DR6 bit8 */
volatile uint16_t g_fx_enabled   = FX_ALL;  /* bitmaska grafickych efektu (okno Animace->EFEKTY),
                                             * persist JEN v syscfg flash blobu (viz freertos_shared.h) */
volatile uint8_t g_sys_cfg_dirty = 0;
volatile uint8_t g_reboot_req    = 0;     /* Menu->Restart -> defaultTask udela NVIC_SystemReset */
/* 1 = pri bootu byla platna syscfg BKP (warm reset, BKP prezila) -> syscfg_load
 * NEpretahne z flash (BKP je nejnovejsi). 0 = studeny start (BKP smazana
 * power-cyclem) -> flash je autoritativni. Nastavuje MX_RTC_Init (DR2 magic). */
volatile uint8_t g_syscfg_bkp_valid = 0;

/* Diagnostika resetu: RCC->RSR zachycene v main.c; crash black-box z BKP (rtc.c);
 * vysledek boot selftestu (defaultTask). Health okno + UART je zobrazuji. */
volatile uint32_t g_reset_rsr    = 0;
volatile char     g_reset_text[12] = "---";
volatile uint8_t  g_reset_bad    = 0;
volatile uint8_t  g_cm4_absent   = 0;  /* 1 = CM4 (D2) nenabehl pri bootu -> bezime degradovane, viz main.c */
volatile uint8_t  g_cm4_alive    = 0;  /* 1 = CM4 heartbeat v IPC roste (defaultTask z ipc_cm4_alive) */
volatile uint16_t g_rtc_set_y = 0;   /* rucni nastaveni RTC (SCPI) — viz freertos_shared.h */
volatile uint8_t  g_rtc_set_mo = 0, g_rtc_set_d = 0;
volatile uint8_t  g_rtc_set_h = 0, g_rtc_set_mi = 0, g_rtc_set_s = 0;
volatile uint8_t  g_rtc_set_pend = 0;
volatile uint8_t  g_ui_cfg_req      = 0;  /* SCPI -> UiTask: pozadovany stav mereni */
volatile uint8_t  g_ui_cfg_req_pend = 0;  /* 1 = ceka na aplikaci (viz freertos_shared.h) */
volatile uint8_t  g_cm4_cpu_pct  = 0;  /* CM4 vlastni zatez [%] z IPC heartbeatu -> header "CM4:xx%" */
volatile uint32_t g_cm4_stall_count = 0;  /* pocet hran CM4 alive->dead (stall:CM4, defaultTask) */
volatile char     g_crash_text[16] = "";
volatile uint32_t g_crash_cfsr = 0;   /* SCB->CFSR z posledniho HardFaultu */
volatile uint32_t g_crash_bfar = 0;   /* SCB->BFAR = adresa, ktera fault zpusobila */
volatile uint32_t g_crash_lr   = 0;   /* stacknute LR = odkud se skocilo (caller) */
volatile uint32_t g_crash_hfsr = 0;   /* SCB->HFSR (VECTTBL/FORCED/DEBUGEVT) */
volatile uint8_t  g_selftest_res = 0;
volatile uint8_t  g_selftest_detail[SELFTEST_N] = {0};  /* per-test 0=--- 1=PASS 2=FAIL (poradi viz freertos_shared.h) */

/* Pure-logic unit testy (zadny HW). Boot (defaultTask) + UART "selftest" +
 * okno Selftest (UiTask, tlacitko SPUSTIT). Nastavi g_selftest_res (souhrn) +
 * g_selftest_detail[] (per-test, okno Selftest).
 *
 * ⚠️ NENI REENTRANTNI (2026-08-12): nektere testy drzi velke buffery jako
 * `static`, aby nepretekly stack malych tasku — `gps_selftest` (gsv_state_t +
 * gps_sat_t[24]) a `scpi_selftest` (2x scpi_src_t), stejne jako odjakziva
 * `ipc_selftest`. Bez toho `gps_selftest` protrhl stack defaultTasku a prepsal
 * FreeRTOS heap (viz #10). Puvodni komentar tvrdil "zadny sdileny stav" — to uz
 * neplati, takze souběh TRI volajicich musi hlidat tenhle zamek.
 * Souběžne volani se neceka: vrati posledni znamy vysledek (testy trvaji ~ms,
 * kolize je stejne teoreticka a blokovat UiTask by bylo horsi).
 *
 * ⚠️ VOLAT AZ ZA BEZICIM SCHEDULEREM. Vsechna tri volani to splnuji
 * (StartDefaultTask, UartTask "selftest", UiTask SPUSTIT). Pred
 * vTaskStartScheduler ma port `uxCriticalNesting` inicializovane na
 * 0xaaaaaaaa, takze by ho `taskEXIT_CRITICAL()` snizil na nenulu a
 * prerusen by uz NIKDY nepovolil -> HAL_Delay/SysTick by zamrzly.
 * (Scheduler ho pri startu nuluje, proto je za nim par enter/exit v poradku.) */
int run_selftests(void)
{
  static volatile uint8_t s_running;
  taskENTER_CRITICAL();
  if (s_running) { taskEXIT_CRITICAL(); return g_selftest_res == 1; }
  s_running = 1;
  taskEXIT_CRITICAL();

  uint8_t r[SELFTEST_N];
  r[0] = fpga_freq_crc_selftest()    ? 1 : 2;
  r[1] = fpga_freq_select_selftest() ? 1 : 2;
  r[2] = gps_selftest()              ? 1 : 2;
  r[3] = screen_main_selftest()      ? 1 : 2;
  r[4] = app_gpsdo_selftest()        ? 1 : 2;
  r[5] = rtc_selftest()              ? 1 : 2;   /* kalendar (tz prehoupnuti) + EU DST hranice */
  r[6] = datalog_selftest()          ? 1 : 2;   /* serializace 32B zaznamu + CRC + kalendar->unix */
  r[7] = meas_math_selftest()        ? 1 : 2;   /* Math Mx+B + NULL + limit pass/fail vektory (#43/#44) */
  r[8] = setup_selftest()            ? 1 : 2;   /* sanitizace slotu sestavy (clamp jas/dim/zona/M) */
  r[9] = autocal_selftest()          ? 1 : 2;   /* verdikt self-checku (PASS/WARN/FAIL pasma) */
  r[10] = mp_selftest()              ? 1 : 2;   /* prezentace: perioda/nominal/jednotky/statistika/TFOM (#67) */
  r[11] = scpi_selftest()            ? 1 : 2;   /* SCPI parser: case/kratka-dlouha forma/hierarchie (#25) */
  r[12] = ipc_selftest()             ? 1 : 2;   /* IPC seqlock parita + cmd/resp ring push/pop/wrap (#19/#20) */
  int pass = 0;
  for (int i = 0; i < SELFTEST_N; i++) { g_selftest_detail[i] = r[i]; if (r[i] == 1) pass++; }
  int ok = (pass == SELFTEST_N);
  g_selftest_res = ok ? 1 : 2;
  printf("SELFTEST: %d/%d %s\n", pass, SELFTEST_N, ok ? "PASS" : "FAIL");
  if (!ok) {
    /* Bez tohohle je "12/13 FAIL" nedohledatelne — cislo indexu je mapa do
     * komentaru u r[] vyse i do okna Selftest (g_selftest_detail). */
    printf("  FAIL:");
    for (int i = 0; i < SELFTEST_N; i++) if (r[i] != 1) printf(" #%d", i);
    printf("\n");
    if (r[11] != 1)   /* SCPI je ~90 assertu v jedne navratove hodnote */
      printf("  SCPI: prvni chyba na scpi.c:%d\n", scpi_selftest_fail_line());
  }
  s_running = 0;                 /* ⚠️ uvolnit zamek — bez toho by se testy spustily JEN JEDNOU */
  return ok;
}

/* RTOS zdravi (UiTask zapise, diagnostika cte) */
volatile uint32_t g_rtos_heap_free = 0;
volatile uint32_t g_rtos_heap_min  = 0;
volatile uint32_t g_rtos_cpu_pct   = 0;
volatile uint32_t g_uptime_s       = 0;

/* Manualne vytvorene tasky (NEJSOU v .ioc): handle + attributes drzime v USER CODE,
 * jinak by je CubeMX regen smazal (osThreadNew jsou v USER CODE RTOS_THREADS). */
osThreadId_t UiTaskHandle;
const osThreadAttr_t UiTask_attributes = {
  .name = "UiTask",
  .stack_size = 2048 * 4,                /* 8 KB: render plne obrazovky (libprim/libui) se do 4 KB nevejde */
  .priority = (osPriority_t) osPriorityBelowNormal,
};
osThreadId_t FpgaTaskHandle;
const osThreadAttr_t FpgaTask_attributes = {
  .name = "FpgaTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* GpsRxQueue: USART1 RX (ISR) -> defaultTask drain. 256 B ~= 266 ms @ 9600.
 * GPS NMEA parsujeme v idle defaultTask (NE vlastni task) — heap (15 KB) uz
 * nepojme 6. task; defaultTask stejne jen idloval. */
osMessageQueueId_t GpsRxQueueHandle;
const osMessageQueueAttr_t GpsRxQueue_attributes = {
  .name = "GpsRxQueue"
};
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 640 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for UartTask */
osThreadId_t UartTaskHandle;
const osThreadAttr_t UartTask_attributes = {
  .name = "UartTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for I2C4Task */
osThreadId_t I2C4TaskHandle;
const osThreadAttr_t I2C4Task_attributes = {
  .name = "I2C4Task",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for UartRxQueue */
osMessageQueueId_t UartRxQueueHandle;
const osMessageQueueAttr_t UartRxQueue_attributes = {
  .name = "UartRxQueue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
/* Manualne vytvorene tasky (NEJSOU v .ioc) — prototypy zde v USER CODE, aby je
 * regen nesmazal (driv byly v generovane sekci -> USB regen je odstranil). */
void StartUiTask(void *argument);
void StartFpgaTask(void *argument);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartUartTask(void *argument);
void StartI2C4(void *argument);

extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  i2c4MutexHandle  = osMutexNew(NULL);   /* chrani I2C4 (TMP117 + touch + backlight) */
  i2c1MutexHandle  = osMutexNew(NULL);   /* chrani I2C1 (FPGA deska: TMP117 x2, ADS1115) */
  uartTxMutexHandle = osMutexNew(NULL);  /* serializuje printf/_write */
  qspiMutexHandle  = osMutexNew(NULL);   /* chrani W25Q (syscfg/calib/UART prikazy) */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of UartRxQueue */
  UartRxQueueHandle = osMessageQueueNew (64, sizeof(uint8_t), &UartRxQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  GpsRxQueueHandle = osMessageQueueNew(256, sizeof(uint8_t), &GpsRxQueue_attributes);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of UartTask */
  UartTaskHandle = osThreadNew(StartUartTask, NULL, &UartTask_attributes);

  /* creation of I2C4Task */
  I2C4TaskHandle = osThreadNew(StartI2C4, NULL, &I2C4Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  UiTaskHandle = osThreadNew(StartUiTask, NULL, &UiTask_attributes);
  if (UiTaskHandle == NULL) {
    printf("[ERR] UiTask se nevytvoril - malo FreeRTOS heapu (configTOTAL_HEAP_SIZE)\n");
  }
  FpgaTaskHandle = osThreadNew(StartFpgaTask, NULL, &FpgaTask_attributes);
  if (FpgaTaskHandle == NULL) {
    printf("[ERR] FpgaTask se nevytvoril - malo FreeRTOS heapu\n");
  }
  /* GpsTask zrusen — GPS NMEA se drainuje v defaultTask (heap setreni). */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN StartDefaultTask */
  gps_init();   /* USART1 -> 9600 8N1 + nahodi RX IT (NEO-7M) */
  run_selftests();   /* boot selftest (pure-logic, ~ms); FAIL -> cerveny indikator v Health */
  /* Datalog az TADY (ne v main.c): potrebuje bezici scheduler kvuli qspiMutexHandle.
   * Na poradi vuci syscfg_load (UiTask) NEZALEZI — init jen najde pozici zapisu,
   * priznak zap/vyp nastavuje syscfg_load pozdeji pres datalog_set_enabled. */
  datalog_init();
  flightrec_init();            /* kontext pred resetem (#18) — po w25q_init */
  ipc_init();   /* orazitkuj IPC snapshot v SRAM4 (magic/verze) — CM4 ho po bootu overi (#19/#20) */
  /* Infinite loop */
  for(;;)
  {
    /* Drain GPS RX fronty (ISR -> GpsRxQueue) a krmeni NMEA parseru. Non-blocking
     * get; pri 9600 ~5 B/5ms, fronta 256 B ma velkou rezervu. */
    uint8_t gc;
    while (osMessageQueueGet(GpsRxQueueHandle, &gc, NULL, 0) == osOK) {
      gps_feed_char((char)gc);
    }
    rtc_app_tick();   /* sync RTC z GPS UTC + format g_rtc_text (throttle 1 Hz uvnitr) */
    rtc_save_uicfg_if_dirty();   /* persist UI nastaveni (mode/chan/gate/run) do BKP pri zmene */
    rtc_save_syscfg_if_dirty();  /* persist systemove nastaveni (jas/mute) do BKP pri zmene */
    syscfg_flash_tick();         /* zrcadlo nastaveni do W25Q flash (debounced, prezije power-cycle) */
    datalog_tick();              /* zaznam stability do W25Q DATA (throttle 10 s uvnitr) */
    flightrec_tick();            /* flight recorder: 1x/s do RAM (do flash az pri poruse) */
    sd_export_tick();            /* SD: detekce karty + auto-unmount (LEVNY; mount/export = UartTask) */
    alarm_tick();     /* zvukovy alarm: hrana OK->SIGNAL_LOST / ztrata GPS locku (respektuje mute) */
    ipc_publish();    /* CM7 -> CM4 snapshot do SRAM4 (seqlock, event-driven uvnitr) (#19/#20) */
    ipc_service();    /* zpracuj pripadne prikazy z CM4 (cmd ring) */
    g_cm4_alive = (uint8_t)ipc_cm4_alive();   /* CM4 heartbeat liveness -> CPU blok "4:OK/--/off" */
    g_cm4_cpu_pct = g_cm4_alive ? (uint8_t)ipc_cm4_cpu_pct() : 0u;   /* -> header "CM4:xx%" */
    /* stall:CM4 detekce — hrany heartbeatu. CM4 stall NEresetuje CM7 (NAVRH §11.4):
     * CM4 se zotavi vlastnim IWDG2, CM7 to jen pozoruje + loguje + pocita. NEsahá na
     * crash black-box (ten je "pricina posledniho resetu CM7"). Armuje se az po 1. ozivu. */
    {
      static uint8_t s_cm4_prev, s_cm4_ever;
      if (g_cm4_alive && !s_cm4_prev) {                 /* dead->alive */
        if (s_cm4_ever) printf("[CM4] obnoveno (IPC heartbeat opet roste)\n");
        s_cm4_ever = 1;
      } else if (!g_cm4_alive && s_cm4_prev && s_cm4_ever) {   /* alive->dead = stall */
        g_cm4_stall_count++;
        printf("[CM4] stall:CM4 — heartbeat zamrzl (%lu.), CM4 IWDG2 by se mel zotavit\n",
               (unsigned long)g_cm4_stall_count);
      }
      s_cm4_prev = g_cm4_alive;
    }
    watchdog_supervise();  /* IWDG refresh jen kdyz UiTask+FpgaTask koply (jinak reset) */
    if (g_reboot_req) {                    /* Menu -> Restart: persist stihne dobehnout vyse */
      osDelay(50);
      NVIC_SystemReset();                  /* softwarovy reset (uz se nevraci) */
    }
    usb_console_tx_pump();   /* dopumpuj CDC TX ring (ocasek po USBD_BUSY by jinak
                              * cekal az na dalsi printf); prazdny ring = okamzity return */
    osDelay(10);   /* 100 Hz: GPS drain (9600 bd ~10 B/tick, fronta 256 hl.) + rtc + usb pump */
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartUartTask */
/**
* @brief Function implementing the UartTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUartTask */
void StartUartTask(void *argument)
{
  /* USER CODE BEGIN StartUartTask */
  UartTask_run(argument);   /* implementace ve freertos_task_uart.c (regen-safe) */
  /* USER CODE END StartUartTask */
}

/* USER CODE BEGIN Header_StartI2C4 */
/**
* @brief Function implementing the I2C4Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartI2C4 */
void StartI2C4(void *argument)
{
  /* USER CODE BEGIN StartI2C4 */
  SensorsTask_run(argument);   /* implementace ve freertos_task_sensors.c (regen-safe) */
  /* USER CODE END StartI2C4 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* USER CODE END Application */

