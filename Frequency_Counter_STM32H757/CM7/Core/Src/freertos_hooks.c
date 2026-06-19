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
 * ────────────────────────────────────────────────────────────────────────── */
volatile char     g_overflow_task[configMAX_TASK_NAME_LEN] = {0};
volatile uint32_t g_malloc_failed = 0;

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  for (int i = 0; i < configMAX_TASK_NAME_LEN - 1 && pcTaskName[i]; i++) {
    g_overflow_task[i] = pcTaskName[i];
  }
  __disable_irq();
  for (;;) { /* breakpoint zde: g_overflow_task = jméno tasku co přetekl stack */ }
}

void vApplicationMallocFailedHook(void)
{
  g_malloc_failed++;
  __disable_irq();
  for (;;) { /* breakpoint zde: došel FreeRTOS heap (configTOTAL_HEAP_SIZE) */ }
}
