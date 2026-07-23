/**
 * @file    ws_panel.c
 * @brief   Driver pro Atmel ATTINY MCU @ I2C 0x45 na Waveshare 43H-800480-IPS
 *
 * Vsechny hodnoty registru a init sekvence prevzaty z Linux kernel driveru:
 * https://github.com/raspberrypi/linux/blob/rpi-6.6.y/drivers/regulator/rpi-panel-attiny-regulator.c
 */
#include "ws_panel.h"
#include <stdio.h>

/* Helper: zapis bytu do registru ATTINY */
static bool ws_write_reg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(hi2c, WS_PANEL_I2C_ADDR,
                                                   buf, 2, 100);
    if (st != HAL_OK) {
        printf("ws_panel: write reg 0x%02X = 0x%02X FAILED (%d)\n",
               reg, value, st);
        return false;
    }
    return true;
}

/* Helper: cteni jednoho bytu z registru ATTINY */
static int ws_read_reg(I2C_HandleTypeDef *hi2c, uint8_t reg)
{
    uint8_t val = 0;
    /* Nejdriv poslat adresu registru */
    if (HAL_I2C_Master_Transmit(hi2c, WS_PANEL_I2C_ADDR, &reg, 1, 100) != HAL_OK) {
        return -1;
    }
    HAL_Delay(1);
    /* Pak precist hodnotu */
    if (HAL_I2C_Master_Receive(hi2c, WS_PANEL_I2C_ADDR, &val, 1, 100) != HAL_OK) {
        return -1;
    }
    return (int)val;
}

bool ws_panel_probe(I2C_HandleTypeDef *hi2c)
{
    /* Ping MCU - je vubec na sbernici? */
    if (HAL_I2C_IsDeviceReady(hi2c, WS_PANEL_I2C_ADDR, 3, 100) != HAL_OK) {
        printf("ws_panel: MCU 0x45 NEODPOVIDA na I2C\n");
        return false;
    }

    /* Precteme firmware ID - mela by byt nejaka nenulova hodnota */
    int id = ws_read_reg(hi2c, WS_REG_ID);
    if (id < 0) {
        printf("ws_panel: nelze precist REG_ID\n");
        return false;
    }
    printf("ws_panel: MCU 0x45 detekovan, FW ID = 0x%02X\n", id);

    /* Zacit s vypnutim - vsechny resety aktivni, backlight off, panel off */
    if (!ws_write_reg(hi2c, WS_REG_POWERON, 0)) return false;
    HAL_Delay(30);
    if (!ws_write_reg(hi2c, WS_REG_PWM, 0)) return false;

    return true;
}

bool ws_panel_power_on(I2C_HandleTypeDef *hi2c)
{
    /* Sekvence presne podle attiny_lcd_power_enable() v Linux driveru */

    /* 0. Master power ON. Probe predtim nastavil POWERON=0 (panel off).
     * U novejsiho ATTINY firmwaru spousti POWERON=1 celou interni
     * napajeci sekvenci; u starsiho (PORT-based) je to neskodny no-op. */
    if (!ws_write_reg(hi2c, WS_REG_POWERON, 1)) return false;
    HAL_Delay(20);

    /* 1. Vsechny resety aktivni */
    if (!ws_write_reg(hi2c, WS_REG_PORTC, 0)) return false;
    HAL_Delay(10);

    /* 2. Default orientation (horizontal flip on, jako Pi firmware) */
    if (!ws_write_reg(hi2c, WS_REG_PORTA, WS_PA_LCD_LR)) return false;
    HAL_Delay(10);

    /* 3. Main LCD power on */
    if (!ws_write_reg(hi2c, WS_REG_PORTB, WS_PB_LCD_MAIN)) return false;
    HAL_Delay(10);

    /* 4. Backlight enable + uvolnit reset bridge, LCD, touch */
    if (!ws_write_reg(hi2c, WS_REG_PORTC,
                      WS_PC_LED_EN | WS_PC_RST_BRIDGE_N |
                      WS_PC_RST_LCD_N | WS_PC_RST_TP_N)) return false;
    HAL_Delay(80);  /* Cas pro nahozeni vsech komponent */

    printf("ws_panel: power-on sekvence dokoncena\n");
    return true;
}

bool ws_panel_set_backlight(I2C_HandleTypeDef *hi2c, uint8_t brightness)
{
    /* REG_PWM = primy jas 0-255, BEZ inverze. Vola UiTask pri kazde zmene jasu
     * (jas +/-, auto-dim) -> ZADNY printf zde (byl by noise z hlidaneho UiTasku;
     * chyba zapisu se projevi navratovou hodnotou). */
    return ws_write_reg(hi2c, WS_REG_PWM, brightness);
}
