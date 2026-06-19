/**
 * @file    tc358762.c
 * @brief   Toshiba TC358762 DSI->DPI bridge init
 *
 * Bridge registry programovane pres DSI generic write:
 *   format: 6 bytu [addr_L][addr_H][data0][data1][data2][data3]
 *   (16-bit adresa, 32-bit data, little endian)
 *
 * Zdroj registru a sekvenci:
 * https://github.com/raspberrypi/linux/blob/rpi-6.6.y/drivers/gpu/drm/bridge/tc358762.c
 */
#include "tc358762.h"
#include <stdio.h>
#include <string.h>

/* === DSI / PPI registry === */
#define TC_DSI_LANEENABLE       0x0210
  #define TC_LANEENABLE_CLEN  (1u << 0)
  #define TC_LANEENABLE_L0EN  (1u << 1)
  #define TC_LANEENABLE_L1EN  (1u << 2)  /* nepouziva se! */

#define TC_PPI_D0S_CLRSIPOCOUNT 0x0164
#define TC_PPI_D1S_CLRSIPOCOUNT 0x0168
#define TC_PPI_D0S_ATMR         0x0144
#define TC_PPI_D1S_ATMR         0x0148
#define TC_PPI_LPTXTIMECNT      0x0114
#define TC_SPICMR               0x0450
#define TC_SYSCTRL              0x0464
#define TC_PPI_STARTPPI         0x0104
#define TC_DSI_STARTDSI         0x0204

/* === LCD output control === */
#define TC_LCDCTRL              0x0420
#define TC_LCDCTRL_VTGEN        (1u << 4)   /* Video Timing Generator enable */
#define TC_LCDCTRL_UNK6         (1u << 6)   /* Unknown bit - Linux vzdy nastavi */
#define TC_LCDCTRL_RGB888       (1u << 8)   /* 24-bit RGB888 mode */
#define TC_LCDCTRL_HSPOL        (1u << 17)  /* HSync polarity (set = active-low) */
#define TC_LCDCTRL_DEPOL        (1u << 18)  /* DE polarity (set = active-low) */
#define TC_LCDCTRL_VSPOL        (1u << 19)  /* VSync polarity (set = active-low) */
#define TC_LCDCTRL_VSDELAY(x)   (((x) & 0xFFF) << 20)

/* === LCD timing registry (urcuje co bridge generuje smerem k panelu) ===
 * Tyto registry NEJSOU v Linux RPi init sekvenci - bridge tam pouziva
 * defaultni hodnoty, ktere jsou nastavene Pi GPU firmwarem pred bootem.
 * V STM32 setupu zadny pre-init neni, takze musime tyto registry napsat
 * sami, aby bridge generoval timing odpovidajici nasemu panelu. */
#define TC_LCD_HS_HBP           0x0424  /* low16 = HSync width, high16 = HBP */
#define TC_LCD_HDISP_HFP        0x0428  /* low16 = HDisplay,    high16 = HFP */
#define TC_LCD_VS_VBP           0x042C  /* low16 = VSync width, high16 = VBP */
#define TC_LCD_VDISP_VFP        0x0430  /* low16 = VDisplay,    high16 = VFP */

/* Helper: zapis 32-bit hodnoty na 16-bit registr pres DSI generic write */
static bool tc_write(DSI_HandleTypeDef *hdsi, uint16_t addr, uint32_t val)
{
    /* 6 payload bytu + 2 padding: HAL_DSI_LongWrite cte payload po 4-byte
     * slovech a u 6-byte payloadu sahne 2 byty za konec pole (stack over-read).
     * Padding to drzi v mezich; word count zustava 6. */
    uint8_t data[8] = {0};
    data[0] = (uint8_t)(addr & 0xFF);
    data[1] = (uint8_t)(addr >> 8);
    data[2] = (uint8_t)(val & 0xFF);
    data[3] = (uint8_t)(val >> 8);
    data[4] = (uint8_t)(val >> 16);
    data[5] = (uint8_t)(val >> 24);

    /* DSI long write - WC = 6 payload bytu (NE sizeof(data)!) */
    HAL_StatusTypeDef st = HAL_DSI_LongWrite(hdsi, 0,                          /* VC=0 */
                                              DSI_GEN_LONG_PKT_WRITE,
                                              6,
                                              data[0], &data[1]);
    if (st != HAL_OK) {
        printf("tc358762: write 0x%04X = 0x%08lX FAILED (%d)\n",
               addr, (unsigned long)val, st);
        return false;
    }
    return true;
}

bool tc358762_init(DSI_HandleTypeDef *hdsi)
{
    printf("tc358762: init...\n");

    /* Lane enable: jen clock + data lane 0 (NE D1!) */
    if (!tc_write(hdsi, TC_DSI_LANEENABLE,
                  TC_LANEENABLE_L0EN | TC_LANEENABLE_CLEN)) return false;

    /* PPI timing parametry */
    if (!tc_write(hdsi, TC_PPI_D0S_CLRSIPOCOUNT, 5)) return false;
    if (!tc_write(hdsi, TC_PPI_D1S_CLRSIPOCOUNT, 5)) return false;
    if (!tc_write(hdsi, TC_PPI_D0S_ATMR, 0)) return false;
    if (!tc_write(hdsi, TC_PPI_D1S_ATMR, 0)) return false;
    if (!tc_write(hdsi, TC_PPI_LPTXTIMECNT, 3)) return false;  /* 0x03 = hodnota z RPi tc358762.c */

    /* SPI master vypnut */
    if (!tc_write(hdsi, TC_SPICMR, 0x00)) return false;

    /* LCD timing registry pro VTGEN.
     *
     * MUSI byt naprogramovane na STM32: Linux je nezapisuje, protoze Pi GPU
     * firmware je predkonfiguruje pred bootem. U nas zadny pre-init neni, takze
     * VTGEN by jel z reset-defaultu -> delka radku != 850 px naseho datoveho
     * toku -> fazovy drift kazdy radek -> diagonalni skew (svisle pruhy se
     * "naklani"). Vodorovne pruhy (testY) skew neukazou, protoze posun
     * jednobarevneho radku je neviditelny.
     *
     * Hodnoty PRESNE z rpi-6.6.y panel modeline (clock 25.979 MHz, total 850x510):
     *   H: HSW=2, HBP=47, HDISP=800, HFP=1   (800+1+2+47 = 850)
     *   V: VSW=2, VBP=21, VDISP=480, VFP=7   (480+7+2+21 = 510)
     * Registr pakuje [high16 | low16] dle komentaru u definic vyse. */
    if (!tc_write(hdsi, TC_LCD_HS_HBP,    (47u << 16) | 2u))   return false; /* HBP<<16 | HSW */
    if (!tc_write(hdsi, TC_LCD_HDISP_HFP, (1u  << 16) | 800u)) return false; /* HFP<<16 | HDISP */
    if (!tc_write(hdsi, TC_LCD_VS_VBP,    (21u << 16) | 2u))   return false; /* VBP<<16 | VSW */
    if (!tc_write(hdsi, TC_LCD_VDISP_VFP, (7u  << 16) | 480u)) return false; /* VFP<<16 | VDISP */
    printf("tc358762: LCD timing 800x480 (rpi modeline, total 850x510) naprogramovan\n");

    /* LCDCTRL: jako Linux 0x00100150, ALE s VYNULOVANYM RGB888 bitem (bit 8).
     *
     * Prechod na RGB565: DSI vstup uz neni RGB888, takze bridge musi unpackovat
     * 16bpp pixely (a expandovat na 24-bit DPI panelu). 0x00100150 & ~0x100
     * = 0x00100050. Zbyle bity: VSDELAY=1, UNK6, VTGEN. */
    if (!tc_write(hdsi, TC_LCDCTRL, 0x00100050)) return false;
    printf("tc358762: LCDCTRL = 0x00100050 (RGB888 bit OFF - RGB565 vstup)\n");

    /* SYSCTRL - LCDC enable */
    if (!tc_write(hdsi, TC_SYSCTRL, 0x040F)) return false;

    HAL_Delay(100);

    /* Spustit PPI a DSI ve bridge */
    if (!tc_write(hdsi, TC_PPI_STARTPPI, 0x01)) return false;
    if (!tc_write(hdsi, TC_DSI_STARTDSI, 0x01)) return false;

    HAL_Delay(100);

    printf("tc358762: init OK, bridge bezi\n");
    return true;
}
