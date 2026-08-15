/**
 * @file    bootled.h
 * @brief   Boot-time HW selftest diagnostika na LED_1 (PG3) — bez displeje.
 *
 * Rozjezd displeje ma spoustu prerekvizit (SDRAM, SPI2, I2C4, DSI, LTDC, ...) —
 * pokud nektera z nich selze, neni cim to ukazat NA displeji. Kazdy sledovany
 * krok si pri startu zapise svoje poradove cislo (bootled_step); pokud pak HAL
 * init selze a spadne do Error_Handler(), LED zabliká tolikrat, kolikaty krok
 * bezel — bez potreby UART terminalu pozna, kde se HW zaseklo.
 *
 * Happy path: bootled_step() je jen zapis do promenne, zadne zpozdeni startu.
 */
#ifndef BOOTLED_H
#define BOOTLED_H

#include <stdint.h>

/** Poradi sledovanych kroku (cislo = pocet bliknuti pri zaseknuti). */
enum {
    BOOTLED_STEP_FMC = 1,       /* SDRAM */
    BOOTLED_STEP_SPI2,          /* FPGA SPI */
    BOOTLED_STEP_I2C4,          /* Panel/Touch ATTINY */
    BOOTLED_STEP_DSIHOST,
    BOOTLED_STEP_LTDC,
    BOOTLED_STEP_USART1,        /* GPS/konzole */
    BOOTLED_STEP_RTC,
    BOOTLED_STEP_ADC3,
    BOOTLED_STEP_QUADSPI,       /* W25Q */
    BOOTLED_STEP_I2C1,          /* senzory na FPGA desce */
    BOOTLED_STEP_PANEL_PROBE,   /* ws_panel_probe (ATTINY) */
    BOOTLED_STEP_PANEL_POWERON, /* ws_panel_power_on */
    BOOTLED_STEP_DSI_START,     /* HAL_DSI_Start */
    BOOTLED_STEP_TC358762,      /* tc358762_init */
};

/** Zaznamena aktualni krok. Volat na zacatku kazdeho sledovaneho initu
 *  (USER CODE BEGIN <Peripheral>_Init 0 hook — prezije CubeMX regen). */
void bootled_step(uint8_t step);

/** Nenavratova diagnostika: donekonecna blika `step` (posledni zaznamenany
 *  bootled_step) krat + pauza. Vola se z Error_Handler() misto ticheho
 *  while(1){}. */
void bootled_fail(void) __attribute__((noreturn));

/** Jednorazove diagnosticke bliknuti bez zastaveni bootu — pro kroky s
 *  nefatalnim fallbackem (rozjezd displeje bez pripojeneho panelu smi
 *  pokracovat headless). */
void bootled_blink_once(uint8_t count);

#endif /* BOOTLED_H */
