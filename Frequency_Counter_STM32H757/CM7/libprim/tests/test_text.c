/**
 * @file test_text.c
 * @brief Smoke test for text rendering with a tiny inline font (no libui).
 */

#include <prim/prim.h>
#include <assert.h>
#include <string.h>

#define W 32
#define H 16

static prim_pixel_t buf[W * H];
static prim_fb_t fb;

/* One glyph 'X' (codepoint 0x58): 4x4 fully-opaque box, bpp8. */
static const uint8_t glyph_bmp[16] = {
    255, 255, 255, 255,  255, 255, 255, 255,
    255, 255, 255, 255,  255, 255, 255, 255,
};
static const prim_glyph_t glyphs[1] = {
    {0x58, 0, 4, 4, 0, 4, 6},   /* cp, off, w, h, ox, oy(top above baseline), adv */
};
static const prim_font_t font = {
    .bitmap_data = glyph_bmp, .glyphs = glyphs, .glyph_count = 1,
    .line_height = 8, .baseline = 6, .ascent = 6, .descent = 2, .bpp = 8,
};

int main(void)
{
    memset(buf, 0, sizeof(buf));
    prim_fb_init(&fb, buf, W, H, W * sizeof(prim_pixel_t));
    prim_set_target(&fb);
    prim_reset_clip();

    /* Width = advance per glyph. */
    assert(prim_text_width("XX", &font) == 12);
    assert(prim_text_width("", &font) == 0);
    assert(prim_text_height(&font) == 8);

    /* Draw 'X' at baseline y=10; glyph top = 10 - oy(4) = 6. */
    prim_color_t white = PRIM_RGB(0xFF, 0xFF, 0xFF);
    prim_draw_text((prim_point_t){2, 10}, "X", &font, white, PRIM_ALIGN_LEFT);
    assert(buf[6 * W + 2] != 0);     /* top-left of the box painted */
    assert(buf[9 * W + 5] != 0);     /* bottom-right of the 4x4 box */
    assert(buf[0] == 0);             /* origin untouched */

    return 0;
}
