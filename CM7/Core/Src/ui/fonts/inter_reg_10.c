/*******************************************************************************
 * Size: 10 px
 * Bpp: 2
 * Opts: --font C:\Users\Latitude\STM32CubeIDE\workspace_2.1.0\H757_LED\H757_LED\tools\ttf\Inter-Regular.ttf --size 10 --bpp 2 --format lvgl --no-compress -r 0x20-0x7E -r 0xB1 -r 0xB7 -o C:\Users\Latitude\STM32CubeIDE\workspace_2.1.0\H757_LED\H757_LED\tools\..\CM7\Core\Src\ui\fonts\inter_reg_10.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef INTER_REG_10
#define INTER_REG_10 1
#endif

#if INTER_REG_10

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x30, 0xc3, 0xc, 0x30, 0x41, 0xc,

    /* U+0022 "\"" */
    0xdb, 0x6c, 0x80,

    /* U+0023 "#" */
    0x8, 0x50, 0x22, 0x7, 0xff, 0x5, 0x30, 0x20,
    0x82, 0xff, 0x82, 0x14, 0x8, 0x50,

    /* U+0024 "$" */
    0x2, 0x1, 0xf8, 0x32, 0x63, 0x20, 0x2e, 0x0,
    0x3d, 0x2, 0x37, 0x26, 0x2f, 0xc0, 0x20,

    /* U+0025 "%" */
    0x64, 0x14, 0xc8, 0x20, 0x88, 0x80, 0x64, 0x80,
    0x2, 0x18, 0x5, 0x22, 0x8, 0x22, 0x20, 0x29,

    /* U+0026 "&" */
    0x1f, 0x0, 0xc6, 0x2, 0x24, 0x7, 0x80, 0x3a,
    0x11, 0x8a, 0x86, 0xd, 0xb, 0xdc,

    /* U+0027 "'" */
    0xfc,

    /* U+0028 "(" */
    0x31, 0x89, 0x30, 0xc3, 0xc, 0x24, 0x60, 0xc0,

    /* U+0029 ")" */
    0x60, 0xc2, 0x5, 0x18, 0x61, 0x49, 0x31, 0x80,

    /* U+002A "*" */
    0x8, 0xb, 0x82, 0xe0, 0x64, 0x4, 0x0,

    /* U+002B "+" */
    0x0, 0x0, 0x20, 0x2, 0x3, 0xfe, 0x2, 0x0,
    0x20,

    /* U+002C "," */
    0x0, 0xc2, 0x14,

    /* U+002D "-" */
    0x7f,

    /* U+002E "." */
    0x10, 0xc0,

    /* U+002F "/" */
    0x4, 0x8, 0xc, 0x8, 0x14, 0x20, 0x30, 0x20,
    0x50, 0x80,

    /* U+0030 "0" */
    0x1f, 0x43, 0xc, 0x20, 0x66, 0x2, 0x60, 0x22,
    0x6, 0x30, 0xc1, 0xf8,

    /* U+0031 "1" */
    0x2c, 0x68, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,

    /* U+0032 "2" */
    0x1f, 0x43, 0xc, 0x0, 0x80, 0xc, 0x3, 0x0,
    0x90, 0x28, 0x3, 0xfd,

    /* U+0033 "3" */
    0x1f, 0x83, 0xc, 0x0, 0xc0, 0xb8, 0x0, 0x90,
    0x6, 0x30, 0x91, 0xf8,

    /* U+0034 "4" */
    0x2, 0xc0, 0x2c, 0xc, 0xc1, 0x8c, 0x30, 0xc7,
    0xff, 0x0, 0xc0, 0xc,

    /* U+0035 "5" */
    0x3f, 0xc3, 0x0, 0x30, 0x3, 0xf4, 0x10, 0xc0,
    0x9, 0x60, 0xc2, 0xf4,

    /* U+0036 "6" */
    0xf, 0x83, 0x9, 0x20, 0x6, 0xb8, 0x70, 0x96,
    0x6, 0x30, 0x91, 0xf8,

    /* U+0037 "7" */
    0x7f, 0xc0, 0xc, 0x2, 0x40, 0x30, 0x6, 0x0,
    0xc0, 0x18, 0x3, 0x0,

    /* U+0038 "8" */
    0x1f, 0x83, 0xc, 0x30, 0xc1, 0xf8, 0x30, 0xd6,
    0x6, 0x30, 0x91, 0xf8,

    /* U+0039 "9" */
    0x1f, 0x43, 0xc, 0x60, 0x53, 0xe, 0x1f, 0x60,
    0x5, 0x30, 0xc1, 0xf4,

    /* U+003A ":" */
    0x0, 0xc0, 0x0, 0x0, 0x43, 0x0,

    /* U+003B ";" */
    0x0, 0xc0, 0x0, 0x0, 0xc2, 0x14,

    /* U+003C "<" */
    0x0, 0x50, 0x78, 0x38, 0x2, 0xd0, 0x2, 0xc0,
    0x1,

    /* U+003D "=" */
    0xff, 0x40, 0xf, 0xf4,

    /* U+003E ">" */
    0x80, 0x1e, 0x0, 0x74, 0x1d, 0x74, 0x10, 0x0,

    /* U+003F "?" */
    0x2e, 0x18, 0x60, 0x18, 0x1c, 0xc, 0x1, 0x0,
    0x40, 0x30,

    /* U+0040 "@" */
    0x2, 0xb8, 0x1, 0xc0, 0x70, 0x31, 0xe9, 0x82,
    0x31, 0xcc, 0x52, 0xc, 0x85, 0x20, 0xc8, 0x63,
    0x1c, 0x83, 0x1e, 0xb4, 0x18, 0x0, 0x0, 0x6f,
    0x80,

    /* U+0041 "A" */
    0x7, 0x0, 0x29, 0x0, 0xcc, 0x6, 0x30, 0x24,
    0x90, 0xff, 0xc6, 0x3, 0x24, 0x9,

    /* U+0042 "B" */
    0x3f, 0xc3, 0x6, 0x30, 0x53, 0xfc, 0x30, 0x63,
    0x3, 0x30, 0x33, 0xfc,

    /* U+0043 "C" */
    0xb, 0xd0, 0x90, 0xd3, 0x0, 0x58, 0x0, 0x60,
    0x0, 0xc0, 0x13, 0x43, 0x42, 0xf4,

    /* U+0044 "D" */
    0x3f, 0xc0, 0xc1, 0xc3, 0x2, 0x4c, 0x6, 0x30,
    0x18, 0xc0, 0x93, 0x7, 0xf, 0xe0,

    /* U+0045 "E" */
    0x3f, 0xd3, 0x0, 0x30, 0x3, 0x0, 0x3f, 0xc3,
    0x0, 0x30, 0x3, 0xfd,

    /* U+0046 "F" */
    0x3f, 0xd3, 0x0, 0x30, 0x3, 0x0, 0x3f, 0xc3,
    0x0, 0x30, 0x3, 0x0,

    /* U+0047 "G" */
    0xb, 0xd0, 0xd0, 0xd3, 0x0, 0x58, 0x0, 0x60,
    0xfc, 0xc0, 0x33, 0x42, 0x42, 0xf8,

    /* U+0048 "H" */
    0x30, 0x18, 0xc0, 0x63, 0x1, 0x8f, 0xfe, 0x30,
    0x18, 0xc0, 0x63, 0x1, 0x8c, 0x6,

    /* U+0049 "I" */
    0x33, 0x33, 0x33, 0x33,

    /* U+004A "J" */
    0x0, 0xc0, 0x30, 0xc, 0x3, 0x0, 0xd4, 0x36,
    0x18, 0xbc,

    /* U+004B "K" */
    0x30, 0x30, 0xc3, 0x3, 0x34, 0xe, 0x80, 0x3b,
    0x0, 0xc7, 0x3, 0x9, 0xc, 0xc,

    /* U+004C "L" */
    0x30, 0x3, 0x0, 0x30, 0x3, 0x0, 0x30, 0x3,
    0x0, 0x30, 0x3, 0xfc,

    /* U+004D "M" */
    0x34, 0x7, 0xe, 0x2, 0xc3, 0xc0, 0xf0, 0xe4,
    0x6c, 0x33, 0x23, 0xc, 0xcc, 0xc3, 0x1d, 0x30,
    0xc3, 0xc,

    /* U+004E "N" */
    0x34, 0x18, 0xf0, 0x63, 0x91, 0x8c, 0xc6, 0x31,
    0x98, 0xc3, 0x63, 0x7, 0x8c, 0xa,

    /* U+004F "O" */
    0xb, 0xd0, 0x24, 0x24, 0x30, 0xc, 0x60, 0xc,
    0x60, 0xc, 0x30, 0xc, 0x34, 0x24, 0xb, 0xd0,

    /* U+0050 "P" */
    0x3f, 0xc3, 0x6, 0x30, 0x33, 0x6, 0x3f, 0x83,
    0x0, 0x30, 0x3, 0x0,

    /* U+0051 "Q" */
    0xb, 0xd0, 0x24, 0x24, 0x30, 0xc, 0x60, 0xc,
    0x60, 0xc, 0x30, 0x4c, 0x34, 0xb4, 0xb, 0xf0,
    0x0, 0x4,

    /* U+0052 "R" */
    0x3f, 0xc0, 0xc1, 0x83, 0x3, 0xc, 0x18, 0x3f,
    0xc0, 0xc3, 0x3, 0x9, 0xc, 0xc,

    /* U+0053 "S" */
    0x1f, 0x83, 0x6, 0x30, 0x2, 0xd0, 0x1, 0xd0,
    0x3, 0x30, 0x61, 0xf8,

    /* U+0054 "T" */
    0x7f, 0xf0, 0x20, 0x2, 0x0, 0x20, 0x2, 0x0,
    0x20, 0x2, 0x0, 0x20,

    /* U+0055 "U" */
    0x30, 0x18, 0xc0, 0x63, 0x1, 0x8c, 0x6, 0x30,
    0x18, 0xc0, 0x63, 0x43, 0x2, 0xf4,

    /* U+0056 "V" */
    0x90, 0x25, 0x80, 0xc3, 0x3, 0x9, 0x14, 0x8,
    0xc0, 0x33, 0x0, 0xa4, 0x0, 0xc0,

    /* U+0057 "W" */
    0x90, 0xa0, 0x56, 0xf, 0x8, 0x30, 0xb0, 0xc3,
    0x16, 0x48, 0x26, 0x19, 0x41, 0x70, 0xa0, 0xe,
    0xf, 0x0, 0xd0, 0xa0,

    /* U+0058 "X" */
    0x70, 0x30, 0x92, 0x40, 0xdc, 0x1, 0xc0, 0x7,
    0x40, 0x33, 0x3, 0x46, 0x18, 0xc,

    /* U+0059 "Y" */
    0x60, 0x30, 0xc1, 0x81, 0x8c, 0x3, 0xa0, 0x3,
    0x0, 0xc, 0x0, 0x30, 0x0, 0xc0,

    /* U+005A "Z" */
    0x7f, 0xe0, 0xc, 0x1, 0x80, 0x30, 0x9, 0x1,
    0x80, 0x30, 0x7, 0xfe,

    /* U+005B "[" */
    0xf3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xc3, 0xc0,

    /* U+005C "\\" */
    0x40, 0x90, 0x60, 0x30, 0x30, 0x24, 0x18, 0x8,
    0xc, 0x9,

    /* U+005D "]" */
    0x74, 0x61, 0x86, 0x18, 0x61, 0x86, 0x19, 0xd0,

    /* U+005E "^" */
    0x4, 0xb, 0x43, 0x21, 0x48,

    /* U+005F "_" */
    0xff, 0x40,

    /* U+0060 "`" */
    0x10, 0x80,

    /* U+0061 "a" */
    0x2f, 0x4, 0x30, 0x6d, 0xd3, 0x51, 0xcf, 0xb0,

    /* U+0062 "b" */
    0x20, 0x2, 0x0, 0x2b, 0x83, 0x9, 0x20, 0x62,
    0x6, 0x30, 0x92, 0xb8,

    /* U+0063 "c" */
    0x1f, 0x43, 0x8, 0x50, 0x5, 0x0, 0x30, 0x81,
    0xf4,

    /* U+0064 "d" */
    0x0, 0x90, 0x9, 0x1f, 0x93, 0xd, 0x50, 0x95,
    0x9, 0x30, 0xd1, 0xf9,

    /* U+0065 "e" */
    0x1f, 0x42, 0xc, 0x7f, 0xd5, 0x0, 0x30, 0x81,
    0xf4,

    /* U+0066 "f" */
    0x1d, 0x30, 0xbd, 0x30, 0x30, 0x30, 0x30, 0x30,

    /* U+0067 "g" */
    0x1f, 0x93, 0xd, 0x50, 0x95, 0x9, 0x30, 0xd2,
    0xf9, 0x10, 0x92, 0xf8,

    /* U+0068 "h" */
    0x20, 0x2, 0x0, 0x2b, 0x43, 0xc, 0x20, 0x82,
    0x8, 0x20, 0x82, 0x8,

    /* U+0069 "i" */
    0x60, 0x22, 0x22, 0x22,

    /* U+006A "j" */
    0x18, 0x0, 0x82, 0x8, 0x20, 0x82, 0x18, 0xc0,

    /* U+006B "k" */
    0x20, 0x2, 0x0, 0x21, 0x82, 0x70, 0x3c, 0x3,
    0xa0, 0x23, 0x42, 0xc,

    /* U+006C "l" */
    0x22, 0x22, 0x22, 0x22,

    /* U+006D "m" */
    0x2b, 0x6d, 0x30, 0xc3, 0x20, 0xc3, 0x20, 0xc3,
    0x20, 0xc3, 0x20, 0xc3,

    /* U+006E "n" */
    0x2b, 0x43, 0xc, 0x20, 0x82, 0x8, 0x20, 0x82,
    0x8,

    /* U+006F "o" */
    0x1f, 0x43, 0xc, 0x50, 0x55, 0x5, 0x30, 0xc1,
    0xf4,

    /* U+0070 "p" */
    0x2b, 0x83, 0x9, 0x20, 0x62, 0x6, 0x30, 0x92,
    0xb8, 0x20, 0x2, 0x0,

    /* U+0071 "q" */
    0x1f, 0x93, 0xd, 0x50, 0x95, 0x9, 0x30, 0xd1,
    0xf9, 0x0, 0x90, 0x9,

    /* U+0072 "r" */
    0x2d, 0x30, 0x20, 0x20, 0x20, 0x20,

    /* U+0073 "s" */
    0x2f, 0x18, 0x13, 0x80, 0x1a, 0x11, 0x8b, 0xc0,

    /* U+0074 "t" */
    0x10, 0x30, 0xf8, 0x30, 0x30, 0x30, 0x30, 0x2c,

    /* U+0075 "u" */
    0x20, 0x82, 0x8, 0x20, 0x82, 0xc, 0x30, 0xc2,
    0xe8,

    /* U+0076 "v" */
    0x90, 0xc2, 0xc, 0x31, 0x42, 0x60, 0xb, 0x0,
    0xd0,

    /* U+0077 "w" */
    0x91, 0x82, 0x22, 0xc5, 0x32, 0x88, 0x26, 0x5c,
    0x1d, 0x38, 0xc, 0x34,

    /* U+0078 "x" */
    0x60, 0x82, 0x70, 0xe, 0x0, 0xe0, 0x22, 0x6,
    0xc,

    /* U+0079 "y" */
    0x90, 0xc2, 0xc, 0x31, 0x41, 0x70, 0xe, 0x0,
    0xd0, 0xc, 0x3, 0x40,

    /* U+007A "z" */
    0x7f, 0xc0, 0x90, 0x60, 0x30, 0x34, 0x1f, 0xf0,

    /* U+007B "{" */
    0xa, 0x8, 0x8, 0x18, 0x70, 0x14, 0x8, 0x8,
    0x8, 0xa,

    /* U+007C "|" */
    0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x80,

    /* U+007D "}" */
    0x70, 0x14, 0x14, 0x18, 0xa, 0x8, 0x14, 0x14,
    0x14, 0x60,

    /* U+007E "~" */
    0x2d, 0x22, 0x2d,

    /* U+00B1 "±" */
    0x8, 0x2, 0xf, 0xf8, 0x20, 0x0, 0x3f, 0xe0,

    /* U+00B7 "·" */
    0x0, 0xc0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 45, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 46, .box_w = 3, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 6, .adv_w = 75, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 9, .adv_w = 101, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 23, .adv_w = 103, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 38, .adv_w = 157, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 54, .adv_w = 103, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 68, .adv_w = 48, .box_w = 1, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 69, .adv_w = 58, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 77, .adv_w = 58, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 85, .adv_w = 80, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 92, .adv_w = 106, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 101, .adv_w = 46, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 104, .adv_w = 74, .box_w = 4, .box_h = 1, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 105, .adv_w = 46, .box_w = 3, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 107, .adv_w = 58, .box_w = 4, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 117, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 129, .adv_w = 65, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 137, .adv_w = 98, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 149, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 161, .adv_w = 103, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 173, .adv_w = 95, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 185, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 197, .adv_w = 91, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 209, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 221, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 233, .adv_w = 46, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 239, .adv_w = 48, .box_w = 3, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 245, .adv_w = 106, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 254, .adv_w = 106, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 258, .adv_w = 106, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 266, .adv_w = 82, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 276, .adv_w = 155, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 301, .adv_w = 110, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 315, .adv_w = 105, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 327, .adv_w = 117, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 341, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 355, .adv_w = 96, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 367, .adv_w = 94, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 379, .adv_w = 119, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 393, .adv_w = 119, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 407, .adv_w = 43, .box_w = 2, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 411, .adv_w = 91, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 421, .adv_w = 108, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 435, .adv_w = 90, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 447, .adv_w = 145, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 465, .adv_w = 121, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 479, .adv_w = 122, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 495, .adv_w = 102, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 507, .adv_w = 122, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 525, .adv_w = 103, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 539, .adv_w = 103, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 551, .adv_w = 103, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 563, .adv_w = 119, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 577, .adv_w = 110, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 591, .adv_w = 158, .box_w = 10, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 611, .adv_w = 109, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 625, .adv_w = 109, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 639, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 651, .adv_w = 58, .box_w = 3, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 659, .adv_w = 58, .box_w = 4, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 669, .adv_w = 58, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 677, .adv_w = 75, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 682, .adv_w = 73, .box_w = 5, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 684, .adv_w = 52, .box_w = 3, .box_h = 2, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 686, .adv_w = 90, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 694, .adv_w = 98, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 706, .adv_w = 91, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 715, .adv_w = 98, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 727, .adv_w = 93, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 736, .adv_w = 59, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 744, .adv_w = 98, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 756, .adv_w = 95, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 768, .adv_w = 39, .box_w = 2, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 772, .adv_w = 39, .box_w = 3, .box_h = 10, .ofs_x = -1, .ofs_y = -2},
    {.bitmap_index = 780, .adv_w = 88, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 792, .adv_w = 39, .box_w = 2, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 796, .adv_w = 140, .box_w = 8, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 808, .adv_w = 95, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 817, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 826, .adv_w = 98, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 838, .adv_w = 98, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 850, .adv_w = 60, .box_w = 4, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 856, .adv_w = 84, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 864, .adv_w = 52, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 872, .adv_w = 95, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 881, .adv_w = 90, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 890, .adv_w = 131, .box_w = 8, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 902, .adv_w = 87, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 911, .adv_w = 90, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 923, .adv_w = 88, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 931, .adv_w = 68, .box_w = 4, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 941, .adv_w = 53, .box_w = 2, .box_h = 13, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 948, .adv_w = 68, .box_w = 4, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 958, .adv_w = 106, .box_w = 6, .box_h = 2, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 961, .adv_w = 106, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 969, .adv_w = 46, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_1[] = {
    0x0, 0x6
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 177, .range_length = 7, .glyph_id_start = 96,
        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Pair left and right glyphs for kerning*/
static const uint8_t kern_pair_glyph_ids[] =
{
    3, 7,
    3, 13,
    3, 15,
    3, 21,
    7, 3,
    7, 8,
    7, 61,
    8, 7,
    8, 13,
    8, 15,
    8, 21,
    11, 7,
    11, 13,
    11, 15,
    11, 21,
    11, 33,
    11, 64,
    12, 19,
    12, 20,
    12, 24,
    12, 61,
    13, 3,
    13, 8,
    13, 17,
    13, 18,
    13, 20,
    13, 22,
    13, 23,
    13, 24,
    13, 25,
    13, 26,
    13, 32,
    13, 33,
    14, 19,
    14, 20,
    14, 24,
    14, 61,
    15, 3,
    15, 8,
    15, 17,
    15, 18,
    15, 20,
    15, 22,
    15, 23,
    15, 24,
    15, 25,
    15, 26,
    15, 32,
    15, 33,
    16, 13,
    16, 15,
    17, 13,
    17, 15,
    17, 24,
    17, 61,
    17, 64,
    19, 21,
    20, 11,
    20, 13,
    20, 15,
    20, 63,
    21, 11,
    21, 13,
    21, 15,
    21, 18,
    21, 63,
    22, 13,
    22, 15,
    23, 13,
    23, 15,
    23, 64,
    24, 4,
    24, 7,
    24, 13,
    24, 15,
    24, 17,
    24, 20,
    24, 21,
    24, 22,
    24, 23,
    24, 24,
    24, 25,
    24, 26,
    24, 27,
    24, 28,
    24, 29,
    24, 64,
    25, 11,
    25, 13,
    25, 15,
    25, 63,
    26, 13,
    26, 15,
    26, 24,
    26, 61,
    26, 64,
    27, 61,
    28, 61,
    30, 61,
    31, 24,
    31, 61,
    33, 13,
    33, 15,
    33, 16,
    33, 61,
    33, 64,
    61, 3,
    61, 8,
    61, 11,
    61, 12,
    61, 14,
    61, 16,
    61, 18,
    61, 30,
    61, 32,
    61, 33,
    61, 61,
    61, 63,
    61, 95,
    61, 96,
    61, 97,
    63, 7,
    63, 13,
    63, 15,
    63, 21,
    63, 33,
    63, 64,
    64, 11,
    64, 17,
    64, 18,
    64, 20,
    64, 21,
    64, 22,
    64, 23,
    64, 25,
    64, 26,
    64, 33,
    64, 61,
    64, 63,
    64, 93,
    95, 19,
    95, 20,
    95, 24,
    95, 61,
    96, 61,
    97, 19,
    97, 20,
    97, 24,
    97, 61
};

/* Kerning between the respective left and right glyphs
 * 4.4 format which needs to scaled with `kern_scale`*/
static const int8_t kern_pair_values[] =
{
    -5, -13, -13, -10, -5, -5, -10, -5,
    -13, -13, -10, -5, -22, -22, -7, -2,
    -11, -5, -2, -4, -8, -13, -13, -4,
    -14, -4, -2, -4, -2, -4, -1, -14,
    -7, -5, -2, -4, -8, -13, -13, -4,
    -14, -4, -2, -4, -2, -4, -1, -14,
    -7, -6, -6, -4, -4, -3, -2, -7,
    -2, -2, -4, -4, -2, -3, -5, -5,
    -3, -3, -4, -4, -5, -5, -7, -9,
    -7, -20, -20, -2, -3, -9, -2, -2,
    3, -2, -2, -5, -5, -15, -25, -2,
    -4, -4, -2, -4, -4, -3, -2, -7,
    -10, -10, -11, -12, -12, -7, -7, -6,
    -5, -6, -13, -13, -13, -5, -5, 3,
    -5, -8, -10, -5, -8, -13, -5, -8,
    -5, -5, -22, -22, -7, -2, -11, -11,
    -7, -17, -7, -9, -7, -7, -7, -7,
    -6, -13, -11, 5, -5, -2, -4, -8,
    -11, -5, -2, -4, -6
};

/*Collect the kern pair's data in one place*/
static const lv_font_fmt_txt_kern_pair_t kern_pairs =
{
    .glyph_ids = kern_pair_glyph_ids,
    .values = kern_pair_values,
    .pair_cnt = 149,
    .glyph_ids_size = 0
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_pairs,
    .kern_scale = 16,
    .cmap_num = 2,
    .bpp = 2,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t inter_reg_10 = {
#else
lv_font_t inter_reg_10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 13,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if INTER_REG_10*/

