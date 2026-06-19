/*
 * app_uart.h
 *
 *  Created on: Jan 4, 2026
 *      Author: František Pospíšil
 */

#ifndef INC_APP_UART_H_
#define INC_APP_UART_H_

#include "cmsis_os.h"

/* RTOS queue pro UART RX */
extern osMessageQueueId_t UartRxQueueHandle;

#endif /* INC_APP_UART_H_ */
