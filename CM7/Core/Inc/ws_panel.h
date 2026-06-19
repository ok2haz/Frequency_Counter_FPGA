/**
 * @file    ws_panel.h
 * @brief   Driver pro Atmel ATTINY MCU @ I2C 0x45 na Waveshare 43H-800480-IPS
 *
 * Panel obsahuje:
 *  - ATTINY MCU (I2C 0x45) - power management + backlight + reset signaly
 *  - Toshiba TC358762 DSI->DPI bridge (programovany pres DSI generic write)
 *  - 800x480 IPS LCD
 *  - (volitelne) Goodix touch (I2C 0x14)
 *
 * Zdroj: linux/drivers/regulator/rpi-panel-attiny-regulator.c (Marek Vasut, 2020)
 * https://github.com/raspberrypi/linux/blob/rpi-6.6.y/drivers/regulator/rpi-panel-attiny-regulator.c
 */
#ifndef WS_PANEL_H
#define WS_PANEL_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>

#define WS_PANEL_I2C_ADDR  (0x45 << 1)  /* HAL pouziva 8-bit adresovani */

/* ATTINY MCU registry */
#define WS_REG_ID            0x80
#define WS_REG_PORTA         0x81  /* LCD orientation */
#define WS_REG_PORTB         0x82  /* LCD/bridge power */
#define WS_REG_PORTC         0x83  /* Reset signaly + backlight enable */
#define WS_REG_POWERON       0x85
#define WS_REG_PWM           0x86  /* Backlight 0-255, NEinvertovano */
#define WS_REG_ADDR_L        0x8C  /* Indirect access do bridge */
#define WS_REG_ADDR_H        0x8D
#define WS_REG_WRITE_DATA_H  0x90
#define WS_REG_WRITE_DATA_L  0x91

/* Bity PORTA */
#define WS_PA_LCD_LR  (1u << 2)  /* horizontal flip - default ON */
#define WS_PA_LCD_UD  (1u << 3)  /* vertical flip */

/* Bity PORTB */
#define WS_PB_LCD_MAIN  (1u << 7)  /* main LCD power */

/* Bity PORTC */
#define WS_PC_LED_EN        (1u << 0)  /* backlight enable */
#define WS_PC_RST_TP_N      (1u << 1)  /* touch reset (active low) */
#define WS_PC_RST_LCD_N     (1u << 2)
#define WS_PC_RST_BRIDGE_N  (1u << 3)

/**
 * @brief  Probe MCU - overit, ze je na sbernici a precist FW ID.
 *         Vola se na zacatku, pred zacatkem DSI signalu.
 * @return true pokud MCU odpovida, false jinak
 */
bool ws_panel_probe(I2C_HandleTypeDef *hi2c);

/**
 * @brief  Power-on sekvence panel MCU.
 *         Pred volanim MUSI byt DSI host nakonfigurovan (CubeMX init).
 *         Po dokonceni teto funkce je bridge mimo reset a panel ma napajeni,
 *         ale jeste neprijima pixely - to dela tc358762_init() pres DSI.
 */
bool ws_panel_power_on(I2C_HandleTypeDef *hi2c);

/**
 * @brief  Nastavi jas podsviceni. NEINVERTOVANO.
 * @param  brightness 0 = vypnuto, 255 = max
 */
bool ws_panel_set_backlight(I2C_HandleTypeDef *hi2c, uint8_t brightness);

/**
 * @brief  Power-off sekvence. Backlight off, panel power off, bridge reset.
 */
bool ws_panel_power_off(I2C_HandleTypeDef *hi2c);

#endif /* WS_PANEL_H */
