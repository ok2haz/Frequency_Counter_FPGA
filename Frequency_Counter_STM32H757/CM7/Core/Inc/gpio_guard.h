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

/* Zamek kolem konfigurace SDILENYCH GPIO (HSEM 1). Best-effort: pri neuspechu
 * se pokracuje bez nej, protoze deadlock pri bootu by byl horsi nez zavod.
 * Pouzij ho vsude, kde se `HAL_GPIO_Init` vola ZA BEHU na portu, kam sahá
 * i druhe jadro (GPIOA/B/C/G). Viz #208. */
void gpio_cfg_lock(void);
void gpio_cfg_unlock(void);

/* Rozpad oprav po jednotlivych pinech — bez nej neslo poznat, jestli zavod
 * postihuje jeden pin, nebo cely port. Poradi = tabulka `GG_PINS` ve
 * `freertos.c`; jmena vraci `gpio_guard_pin_name`. */
extern volatile uint16_t g_gpio_guard_fix_pin[];
uint32_t    gpio_guard_pin_count(void);
const char *gpio_guard_pin_name(uint32_t i);

extern volatile uint32_t g_gpio_guard_fix_sdclk;
extern volatile uint32_t g_gpio_guard_fix_txen;
extern volatile uint32_t g_gpio_guard_fix_total;

#endif /* GPIO_GUARD_H */
