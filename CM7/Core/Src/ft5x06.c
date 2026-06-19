/**
 * @file    ft5x06.c
 * @brief   Driver pro FocalTech FT5x06 touch @ I2C 0x38
 *
 * Reference: drivers/input/touchscreen/edt-ft5x06.c v Linux kernelu
 *
 * Datasheet FT5306/FT5316 (kompatibilni rodina):
 *   https://www.crystalfontz.com/controllers/FocalTech/FT5316/
 */
#include "ft5x06.h"
#include <stdio.h>

/* I2C timeout v ms */
#define FT5X06_TIMEOUT 100

/* === Adresy registru === */
#define FT5X06_REG_DEVICE_MODE   0x00   /* 0x00=working, 0x40=factory test */
#define FT5X06_REG_GEST_ID       0x01   /* gesture detection (swipe, atd.) */
#define FT5X06_REG_TD_STATUS     0x02   /* low 4 bits = pocet doteku 0..5 */
#define FT5X06_REG_TOUCH1_XH     0x03   /* prvni dotek - X high + event flag */
/* TOUCH1: 0x03=XH+event, 0x04=XL, 0x05=YH+id, 0x06=YL, 0x07=weight, 0x08=area */
/* TOUCH2: 0x09-0x0E, TOUCH3: 0x0F-0x14, ...                                 */
#define FT5X06_REG_THRESHOLD     0x80   /* touch threshold */
#define FT5X06_REG_FIRMWARE_ID   0xA6   /* firmware verze */
#define FT5X06_REG_VENDOR_ID     0xA8   /* vendor ID, FocalTech typicky 0x11 */

/* === Helpers === */
static HAL_StatusTypeDef ft5x06_read_reg(I2C_HandleTypeDef *hi2c,
                                          uint8_t reg, uint8_t *data, uint16_t len)
{
    return HAL_I2C_Mem_Read(hi2c, FT5X06_I2C_ADDR, reg,
                             I2C_MEMADD_SIZE_8BIT, data, len, FT5X06_TIMEOUT);
}

/* === Public API === */
bool ft5x06_probe(I2C_HandleTypeDef *hi2c)
{
    /* Overit, ze zarizeni reaguje na I2C */
    if (HAL_I2C_IsDeviceReady(hi2c, FT5X06_I2C_ADDR, 3, FT5X06_TIMEOUT) != HAL_OK) {
        printf("ft5x06: zarizeni 0x38 NEODPOVIDA na I2C\n");
        return false;
    }

    /* Cist identifikacni registry */
    uint8_t vendor_id = 0, firmware_id = 0;

    if (ft5x06_read_reg(hi2c, FT5X06_REG_VENDOR_ID, &vendor_id, 1) != HAL_OK) {
        printf("ft5x06: nelze precist VENDOR_ID\n");
        return false;
    }
    if (ft5x06_read_reg(hi2c, FT5X06_REG_FIRMWARE_ID, &firmware_id, 1) != HAL_OK) {
        printf("ft5x06: nelze precist FIRMWARE_ID\n");
        return false;
    }

    printf("ft5x06: detekovan, VENDOR=0x%02X, FW=0x%02X\n", vendor_id, firmware_id);

    /* Volitelne: nastavit threshold (default je obvykle OK) */
    /* uint8_t thresh = 22;
     * HAL_I2C_Mem_Write(hi2c, FT5X06_I2C_ADDR, FT5X06_REG_THRESHOLD,
     *                   I2C_MEMADD_SIZE_8BIT, &thresh, 1, FT5X06_TIMEOUT); */

    return true;
}

/* Naparsuje touch frame (31 bytu od TD_STATUS). Parsujeme jen 1. bod.
 * Cely frame se MUSI cist (ne jen 7 bytu) - jinak FT5x06 pri multitouchi
 * drzi I2C a dalsi transakce zatuhne. Viz edt-ft5x06. */
bool ft5x06_parse(const uint8_t *frame, ft5x06_touch_t *touch)
{
    if (!touch || !frame) return false;

    touch->valid = false;
    touch->num_touches = 0;

    uint8_t num = frame[0] & 0x0F;
    /* 0 = zadny dotek; 0x0F sentinel / 6+ nevalidni (max 5). */
    if (num == 0 || num > 5) {
        return true;
    }
    touch->num_touches = num;

    /* TOUCH1_XH..TOUCH1_YL = bytes 1..4 */
    touch->event = (frame[1] >> 6) & 0x03;
    touch->x     = ((uint16_t)(frame[1] & 0x0F) << 8) | frame[2];
    touch->id    = (frame[3] >> 4) & 0x0F;
    touch->y     = ((uint16_t)(frame[3] & 0x0F) << 8) | frame[4];
    touch->valid = true;

    return true;
}

/* Blokujici (polling) cteni - pouzivaji ho UART prikazy touch/touchloop.
 * Dotykove UI cte asynchronne pres IT (viz touch_ui.c). */
bool ft5x06_read_touch(I2C_HandleTypeDef *hi2c, ft5x06_touch_t *touch)
{
    if (!touch) return false;

    uint8_t buf[FT5X06_FRAME_LEN] = {0};
    if (ft5x06_read_reg(hi2c, FT5X06_REG_TD_STATUS, buf, sizeof(buf)) != HAL_OK) {
        touch->valid = false;
        touch->num_touches = 0;
        return false;
    }
    return ft5x06_parse(buf, touch);
}
