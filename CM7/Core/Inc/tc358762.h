/**
 * @file    tc358762.h
 * @brief   Driver pro Toshiba TC358762 DSI->DPI bridge
 *
 * Bridge je na Waveshare 43H-800480-IPS PCB, programuje se pres DSI
 * generic write (NE pres I2C). DSI Host musi byt JIZ SPUSTENY (HAL_DSI_Start),
 * predtim nez vola se tc358762_init().
 *
 * Zdroj: linux/drivers/gpu/drm/bridge/tc358762.c (Marek Vasut, 2020)
 * https://github.com/raspberrypi/linux/blob/rpi-6.6.y/drivers/gpu/drm/bridge/tc358762.c
 */
#ifndef TC358762_H
#define TC358762_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>

/**
 * @brief  Provede 11 DSI generic writes pro inicializaci bridge.
 *         Po dokonceni teto funkce zacne bridge prevadet DSI->DPI signal
 *         a panel zacne dostavat pixely z LTDC.
 *
 * @param  hdsi  Inicializovany DSI Host handle (po HAL_DSI_Start!)
 * @return true pri uspechu
 */
bool tc358762_init(DSI_HandleTypeDef *hdsi);

#endif /* TC358762_H */
