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

/* ── Uložení na SD kartu (doporučená cesta, 2026-08-15) ──────────────────────
 * Řeší tři slabiny USB varianty najednou:
 *   1) **tearing** — snímek se nejdřív zkopíruje z front bufferu do SDRAM scratche
 *      (~750 kB, jednotky ms), takže flip UiTasku uprostřed exportu už nevadí,
 *   2) **spolehlivost** — FatFs zapíše celý soubor, nic se nezahazuje při zahlcení,
 *   3) **praktičnost** — vytáhneš kartu a máš `SHOTnnn.BMP`, bez terminálových triků.
 *
 * ⚠️ BLOKUJE (kopie + zápis ~1,15 MB = jednotky sekund) → VÝHRADNĚ z UartTasku,
 * který není hlídaný watchdogem. Kartu si namountuje sám, když je potřeba.
 * @return 0 = OK (jméno v `name_out`), <0 = chyba (viz `sd_export_state_str()`). */
int screenshot_save_sd(char *name_out, unsigned name_sz);
