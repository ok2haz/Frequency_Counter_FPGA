/**
 * @file    si5356.h
 * @brief   Silicon Labs Si5356A clock generator @ I2C1 0x70 (FPGA deska).
 *
 * Aplikuje kompletni register map z ClockBuilder Pro (CBPro) + oficialni
 * SiLabs "apply" proceduru (disable outputs -> soft reset -> enable outputs).
 * Register map je prevzata z uzivatelova I2C trace (cil: 100 MHz + fazovy posun);
 * frekvence/faze jsou DANE TOUTO MAPOU (tj. CBPro konfiguraci), driver je jen aplikuje.
 *
 * Pri zmene konfigurace: v CBPro nastav vystupy/fazi, exportuj "C Code Header"
 * a nahrad obsah REGMAP[] v si5356.c (format {addr, val, mask} je stejny).
 */
#ifndef SI5356_H
#define SI5356_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define SI5356_I2C_ADDR   (0x70u << 1)   /* 7-bit 0x70 -> HAL 8-bit */

/** Aplikuje register map + apply proceduru. Volat jednou (boot) na I2C1.
 *  @return true pokud vsechny I2C zapisy ACKovaly. */
bool si5356_init(I2C_HandleTypeDef *hi2c);

/** Precte status reg 218 (0xDA): bit0=SYS_CAL(probiha kalibrace),
 *  bit2=LOS_CLKIN(chybi vstupni hodiny), bit4=PLL_LOL(PLL neni zamceny). */
bool si5356_read_status(I2C_HandleTypeDef *hi2c, uint8_t *status);

#endif /* SI5356_H */
