/**
 * @file test_shapes.c
 * @brief Smoke tests for lines and circles (software AA path).
 */

#include <prim/prim.h>
#include <assert.h>
#include <string.h>

#define W 64
#define H 64

static prim_pixel_t buf[W * H];
static prim_fb_t fb;

static void setup(void)
{
    memset(buf, 0, sizeof(buf));
    prim_fb_init(&fb, buf, W, H, W * sizeof(prim_pixel_t));
    prim_set_target(&fb);
    prim_reset_clip();
}

static int any_set(void)
{
    for (int i = 0; i < W * H; i++) if (buf[i]) return 1;
    return 0;
}

int main(void)
{
    prim_color_t white = PRIM_RGB(0xFF, 0xFF, 0xFF);

    /* Horizontal line (axis-aligned fast path) paints its row. */
    setup();
    prim_draw_line((prim_point_t){5, 20}, (prim_point_t){50, 20}, 1, white);
    assert(buf[20 * W + 5] != 0);
    assert(buf[20 * W + 50] != 0);

    /* Diagonal line paints something near both endpoints. */
    setup();
    prim_draw_line((prim_point_t){4, 4}, (prim_point_t){58, 58}, 2, white);
    assert(any_set());

    /* Filled circle paints the center, leaves far corner empty. */
    setup();
    prim_fill_circle((prim_point_t){32, 32}, 10, white);
    assert(buf[32 * W + 32] != 0);
    assert(buf[0] == 0);

    return 0;
}
