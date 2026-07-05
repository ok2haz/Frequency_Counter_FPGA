/*
 * usb_console.c — USB CDC konzole: neblokujici TX (ring buffer) + RX most do
 * UartRxQueue. Aktivuje se prepinacem USE_USB_CDC_CONSOLE (usb_console.h) AZ po
 * CubeMX regenu (USB_DEVICE/CDC). Do te doby = prazdne stuby (build-safe).
 *
 * Proc ring buffer: CDC_Transmit_FS NENI blokujici jako HAL_UART_Transmit —
 * vraci USBD_BUSY, dokud predchozi USB paket neodejde. Bez fronty by se printf
 * ztracel. _write (main.c) jen plni ring; pump() ho dava do CDC.
 */
#include "usb_console.h"

#if USE_USB_CDC_CONSOLE

#include "usbd_cdc_if.h"   /* CDC_Transmit_FS, USBD_OK / USBD_BUSY */
#include "cmsis_os2.h"

extern osMessageQueueId_t UartRxQueueHandle;   /* freertos.c — fronta konzole (USB CDC RX; USART1 jde do GpsRxQueue) */

#define TXRING_SZ   1024u            /* mocnina 2 */
#define TXRING_MASK (TXRING_SZ - 1u)
static uint8_t s_tx[TXRING_SZ];
static volatile uint16_t s_head;     /* zapis (_write) */
static volatile uint16_t s_tail;     /* odeslano do CDC */

void usb_console_tx_pump(void)
{
  /* ⚠️ Volano ze DVOU kontextu: defaultTask (bez zamku) i _write->usb_console_tx
   * (pod uartTxMutex). Bez serializace by dva soubezne pumpy videly stejny tail,
   * poslaly stejny blok 2x a rozjely s_tail. PRIMASK kriticka sekce = plna
   * mutualni exkluze; funguje i PRED spustenim scheduleru (early printf z main
   * USER CODE 2) a nezavisi na FreeRTOS. CDC_Transmit_FS je neblokujici (jen
   * naprogramuje endpoint / vrati USBD_BUSY) -> maskovani IRQ na par us je OK. */
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  uint16_t used = (uint16_t)(s_head - s_tail);
  if (used) {
    /* CDC bere souvisly buffer -> posli blok od tail do konce ringu (max do wrapu). */
    uint16_t t     = (uint16_t)(s_tail & TXRING_MASK);
    uint16_t chunk = (uint16_t)(TXRING_SZ - t);
    if (chunk > used) chunk = used;
    if (CDC_Transmit_FS(&s_tx[t], chunk) == USBD_OK)
      s_tail = (uint16_t)(s_tail + chunk);
    /* USBD_BUSY -> data zustanou v ringu, zkusi se pri dalsim pump(). */
  }
  __set_PRIMASK(primask);
}

void usb_console_tx(const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++) {
    if ((uint16_t)(s_head - s_tail) >= TXRING_MASK) {   /* plny ring */
      usb_console_tx_pump();
      if ((uint16_t)(s_head - s_tail) >= TXRING_MASK) {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        s_tail++;                                       /* drop nejstarsi (console) — atomicky vuci pump() */
        __set_PRIMASK(primask);
      }
    }
    s_tx[s_head & TXRING_MASK] = data[i];
    s_head++;
  }
  usb_console_tx_pump();
}

void usb_console_on_rx(const uint8_t *data, uint32_t len)
{
  for (uint32_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    osMessageQueuePut(UartRxQueueHandle, &b, 0u, 0u);   /* ISR-safe (timeout 0) */
  }
}

#else  /* ── USB jeste neaktivni: prazdne stuby (kompiluje se naprazdno) ── */

void usb_console_tx(const uint8_t *data, uint16_t len) { (void)data; (void)len; }
void usb_console_tx_pump(void) { }
void usb_console_on_rx(const uint8_t *data, uint32_t len) { (void)data; (void)len; }

#endif
