#ifndef INC_USB_CONSOLE_H_
#define INC_USB_CONSOLE_H_

#include <stdint.h>

/*
 * USB CDC (Virtual COM Port) konzole — TX ring buffer + RX most do UartRxQueue.
 *
 * Prepinac USE_USB_CDC_CONSOLE:
 *   0 = konzole na USART1 (vychozi, jak ted; _write -> HAL_UART_Transmit)
 *   1 = konzole na USB CDC (_write -> usb_console_tx; USART1 volny pro GPS)
 *
 * ⚠️ Na 1 prepni AZ PO CubeMX regenu (USB_DEVICE/CDC) — drive se neslinkuje
 * (chybi CDC_Transmit_FS). Do te doby se modul kompiluje jako prazdne stuby.
 * Postup: viz USB_CDC_PLAN.md.
 */
#ifndef USE_USB_CDC_CONSOLE
#define USE_USB_CDC_CONSOLE 1
#endif

/* TX: zaradi data do ring bufferu (NEBLOKUJICI — CDC_Transmit_FS neceka). */
void usb_console_tx(const uint8_t *data, uint16_t len);

/* Vyprazdni co je v ringu do CDC. Vola usb_console_tx; po regenu lze pripojit
 * i na CDC TxComplete callback pro spolehlivy drain bez dalsiho printf. */
void usb_console_tx_pump(void);

/* RX: zavolat z generovaneho CDC_Receive_FS — nacpe bajty do UartRxQueue
 * (stejna fronta jako USART1 RX -> UartTask parser beze zmeny). */
void usb_console_on_rx(const uint8_t *data, uint32_t len);

/* Kolik bajtu konzole ZAHODILA: TX pri plnem ring bufferu (host neodebira),
 * RX pri plne `UartRxQueue` (rozpadly prikaz). Tichá ztráta bez počítadla je
 * vada sama o sobě (L-0017) — oboje vypisuje `status`, řádek KONZOLE.
 * ⚠️ TX zahazuje PRICHAZEJICI bajt, ne nejstarsi: nejstarsi muze byt prave
 * vysilany (viz `s_pending` v usb_console.c, audit F-0127). */
uint32_t usb_console_tx_dropped(void);
uint32_t usb_console_rx_dropped(void);

#endif /* INC_USB_CONSOLE_H_ */
