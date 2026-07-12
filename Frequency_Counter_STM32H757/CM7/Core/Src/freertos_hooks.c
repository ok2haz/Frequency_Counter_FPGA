/*
 * freertos_hooks.c
 *
 * Run-time stats časová báze (DWT) + diagnostické hooky FreeRTOS
 * (stack overflow, malloc failed) — vyčleněno z freertos.c.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "freertos_shared.h"

/* === Run-time stats casova baze: DWT cycle counter (480 MHz) ===
 * Volano pres port makra v FreeRTOSConfig.h
 * (portCONFIGURE_TIMER_FOR_RUN_TIME_STATS / portGET_RUN_TIME_COUNTER_VALUE). */
void RunTimeStats_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
uint32_t RunTimeStats_GetCount(void)
{
  return DWT->CYCCNT;
}

/* ──────────────────────────────────────────────────────────────────────────
 *  FreeRTOS diagnostické hooky (configCHECK_FOR_STACK_OVERFLOW=2,
 *  configUSE_MALLOC_FAILED_HOOK=1). Drive tiché přepsání paměti → teď se to
 *  zachytí. Globály jsou viditelné v debuggeru; po zachycení se zastaví IRQ a
 *  zacyklí (breakpoint sem), aby chyba nezmizela.
 *  Pozn.: s IWDG (watchdog.c) spin = žádné heartbeaty → HW reset do ~4 s
 *  (24/7 auto-recovery); s debuggerem na haltu IWDG zmrzne (DEBUG freeze).
 * ────────────────────────────────────────────────────────────────────────── */
volatile char     g_overflow_task[configMAX_TASK_NAME_LEN] = {0};
volatile uint32_t g_malloc_failed = 0;

/* Crash black-box: pred spinem zapis kind + jmeno tasku do BKP_DR3..5 (prezije
 * IWDG reset; MX_RTC_Init to po bootu precte -> g_crash_text -> Health okno).
 * Prime zapisy RTC->BKPxR (HAL tu nevolat); DBP uz je povoleny od RTC initu,
 * pro jistotu se nahodi znovu. kind: 1 = stack overflow, 2 = malloc fail. */
static void crash_blackbox(uint32_t kind, const char *name)
{
  PWR->CR1 |= PWR_CR1_DBP;
  uint32_t n0 = 0, n1 = 0;
  for (int i = 0; i < 8 && name && name[i]; i++) {
    if (i < 4) n0 |= (uint32_t)(uint8_t)name[i] << (8 * i);
    else       n1 |= (uint32_t)(uint8_t)name[i] << (8 * (i - 4));
  }
  RTC->BKP4R = n0;
  RTC->BKP5R = n1;
  RTC->BKP3R = 0xC7A50000u | (kind & 0xFFu);   /* RTC_CRASH_MAGIC (rtc.h) — magic az naposled */
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  for (int i = 0; i < configMAX_TASK_NAME_LEN - 1 && pcTaskName[i]; i++) {
    g_overflow_task[i] = pcTaskName[i];
  }
  crash_blackbox(1u, pcTaskName);
  __disable_irq();
  /* spin: bez heartbeatu IWDG resetne do ~4 s -> boot ohlasi crash z BKP */
  for (;;) { /* breakpoint zde: g_overflow_task = jméno tasku co přetekl stack */ }
}

void vApplicationMallocFailedHook(void)
{
  g_malloc_failed++;
  crash_blackbox(2u, "");
  __disable_irq();
  for (;;) { /* breakpoint zde: došel FreeRTOS heap (configTOTAL_HEAP_SIZE) */ }
}
