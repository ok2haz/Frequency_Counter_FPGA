/*
 * freertos_task_fpga.c
 *
 * SPI2 čítač kmitočtu z FPGA (StartFpgaTask) — vyčleněno z freertos.c.
 * Inicializuje SPI, polluje ~20 Hz, nové měření naformatuje do g_freq_text
 * a stav linky do g_spi_text pro UiTask. Protokol viz CLAUDE.md / fpga_freq.c.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"

#include <string.h>
#include <stdbool.h>

#include "fpga_freq.h"
#include "freertos_shared.h"

void StartFpgaTask(void *argument)
{
  /* Kontrakt bod 1/8: nebudit SPI piny, dokud FPGA nedokonci konfiguraci z flash
   * po power-on/resetu. CS uz drzime vysoko (idle); jeste pridame prodlevu, aby
   * GW1NR-9 stihl nabootovat drive, nez na nej zacneme clockovat. */
  osDelay(250);

  fpga_freq_init();

  fpga_meas_t m;
  uint32_t fails = 0;
  uint8_t  lost  = 0;
  for (;;) {
    if (fpga_freq_poll(&m)) {
      fails = 0;
      /* Vyber zobrazovany zdroj: /4 (nejlepsi rozliseni) dokud bez chyby a pod ~380 MHz,
       * jinak /16 (vyssi rozsah). freq*_x100000 uz ma delicku zahrnutou -> jen /100000. */
      int use16 = 0;
      uint64_t v = fpga_freq_select(&m, &use16);
      char buf[48], ibuf[64];
      fpga_freq_format_val(v, buf, sizeof(buf));
      fpga_freq_format_info(&m, use16, ibuf, sizeof(ibuf));
      taskENTER_CRITICAL();
      strncpy((char *)g_freq_text, buf, sizeof(g_freq_text) - 1);
      g_freq_text[sizeof(g_freq_text) - 1] = '\0';
      strncpy((char *)g_freq_info, ibuf, sizeof(g_freq_info) - 1);
      g_freq_info[sizeof(g_freq_info) - 1] = '\0';
      g_freq_dirty = 1;
      taskEXIT_CRITICAL();
    } else if (!fpga_freq_link_ok()) {
      /* zadny platny ramec -> FPGA mozna bootl pozdeji / resetoval; po ~3 s znovu START
       * (20 Hz polling -> 60 iteraci = ~3 s) */
      if (++fails >= 60) { fpga_freq_restart(); fails = 0; }
    } else {
      fails = 0;   /* link zije, jen zatim neni nove mereni */
    }

    /* Ztrata signalu: autoritativni z FPGA (error_flags bit1 SIGNAL_LOST, watchdog ~2.5 s)
     * nebo mrtvy link. Nahrazuje drivejsi SEQ-staleness heuristiku - ta falesne hlasila
     * stale u nizkych kmitoctu, kde se reciproke okno legitimne protahne. Pri ztrate UI
     * ztlumi kmitocet na sedou. */
    uint8_t l = (!fpga_freq_link_ok() || fpga_freq_signal_lost()) ? 1 : 0;
    if (l != lost) {
      lost = l;
      taskENTER_CRITICAL();
      g_freq_stale = l;
      g_freq_dirty = 1;     /* prekreslit kmitocet v jine barve (ztlumeny / zluty) */
      taskEXIT_CRITICAL();
    }

    /* Stav SPI/komunikace -> displej (prekreslit jen pri zmene textu/linky) */
    char sbuf[64];
    fpga_freq_format_status(sbuf, sizeof(sbuf));
    uint8_t ok = fpga_freq_link_ok() ? 1 : 0;
    taskENTER_CRITICAL();
    if (ok != g_spi_ok || strncmp((const char *)g_spi_text, sbuf, sizeof(g_spi_text)) != 0) {
      strncpy((char *)g_spi_text, sbuf, sizeof(g_spi_text) - 1);
      g_spi_text[sizeof(g_spi_text) - 1] = '\0';
      g_spi_ok = ok;
      g_spi_dirty = 1;
    }
    taskEXIT_CRITICAL();

    osDelay(50);   /* ~20 Hz cteni */
  }
}
