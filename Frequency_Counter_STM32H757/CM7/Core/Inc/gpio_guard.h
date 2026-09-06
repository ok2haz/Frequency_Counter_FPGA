/**
  ******************************************************************************
  * @file    gpio_guard.h
  * @brief   Hlidac konfigurace kritickych pinu na GPIOG (viz gpio_guard.c).
  ******************************************************************************
  */
#ifndef GPIO_GUARD_H
#define GPIO_GUARD_H

#include <stdint.h>

/** Zkontroluje a pripadne opravi PG8 (FMC_SDCLK) a PG11/PG13 (ETH TX).
 *  Vola se z defaultTask ~1x za sekundu. Levne: pri spravne konfiguraci
 *  jen precte dva registry a nic nezapise. */
void gpio_guard_tick(void);

extern volatile uint32_t g_gpio_guard_fix_sdclk;
extern volatile uint32_t g_gpio_guard_fix_txen;
extern volatile uint32_t g_gpio_guard_fix_total;

#endif /* GPIO_GUARD_H */
