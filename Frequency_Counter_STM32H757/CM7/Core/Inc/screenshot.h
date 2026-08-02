#pragma once
/**
 * @file    screenshot.h
 * @brief   Export obrazovky (framebuffer -> BMP) přes konzoli. ROZPRACOVÁNO.
 *
 * Vyčte aktuálně zobrazený front buffer (RGB565 800×480, `prim_stm32_front_addr`),
 * převede na 24-bit BGR a odešle jako BMP (bottom-up) přes `usb_console_tx`
 * (USB CDC). Volá se z UART příkazu `screenshot` (UartTask — není hlídán
 * watchdogem, smí blokovat).
 *
 * ⚠️ EXPERIMENTÁLNÍ: ~1,15 MB přes USB CDC = jednotky sekund; tok řízen jen
 * chunk+pump+osDelay (best-effort), při zahlcení ringu může zahodit. Vyžaduje
 * USB CDC konzoli (`USE_USB_CDC_CONSOLE=1`). Příjem na PC: uložit binárně do
 * `.bmp` (např. `cat /dev/ttyACM0 > shot.bmp` po odeslání příkazu).
 */
void screenshot_emit_bmp(void);
