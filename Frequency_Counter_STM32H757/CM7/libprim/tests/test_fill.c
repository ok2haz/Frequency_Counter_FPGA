/**
 * @file test_fill.c
 * @brief Smoke tests for prim_fill_rect / prim_blit over a host RGB565 buffer.
 *
 * No DMA2D backend installed → software path. Asserts pixel invariants without
 * a reference image (PNG pixel-diff is a separate, post-bring-up step).
 */

#include <prim/prim.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 48

static prim_pixel_t buf[W * H];
static prim_fb_t fb;

static void setup(void)
{
    memset(buf, 0, sizeof(buf));
    prim_fb_init(&fb, buf, W, H, W * sizeof(prim_pixel_t));
    prim_set_target(&fb);
    prim_reset_clip();
}

static prim_pixel_t at(int x, int y) { return buf[y * W + x]; }

int main(void)
{
    setup();

    /* Opaque red fill of an inner rect. */
    prim_color_t red = PRIM_RGB(0xFF, 0x00, 0x00);
    prim_fill_rect((prim_rect_t){10, 8, 20, 16}, red, PRIM_BLEND_REPLACE);

    assert(at(15, 12) == prim_argb_to_565(red));   /* inside */
    assert(at(0, 0) == 0);                          /* outside untouched */
    assert(at(31, 12) == 0);                        /* just past right edge */

    /* Clip must bound writes. */
    setup();
    prim_set_clip((prim_rect_t){0, 0, 5, 5});
    prim_fill_rect((prim_rect_t){0, 0, W, H}, red, PRIM_BLEND_REPLACE);
    assert(at(4, 4) == prim_argb_to_565(red));
    assert(at(5, 5) == 0);                          /* outside clip */

    /* Blit copies RGB565 verbatim. */
    setup();
    static prim_pixel_t src[8 * 8];
    for (int i = 0; i < 8 * 8; i++) src[i] = (prim_pixel_t)(0x1234 + i);
    prim_blit((prim_rect_t){2, 2, 8, 8}, src, 8 * sizeof(prim_pixel_t));
    assert(at(2, 2) == src[0]);
    assert(at(9, 9) == src[8 * 8 - 1]);

    return 0;
}
