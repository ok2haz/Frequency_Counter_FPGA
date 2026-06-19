/**
 * @file 01_hello_rect.c
 * @brief Minimal libprim example: draw shapes into an RGB565 buffer, save PPM.
 *
 * No DMA2D backend (software path). Output: hello_rect.ppm (P6, RGB888).
 */

#include <prim/prim.h>
#include <stdio.h>

#define W 320
#define H 240

static prim_pixel_t fbmem[W * H];

static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        prim_color_t c = prim_565_to_argb(fbmem[i]);
        unsigned char rgb[3] = {PRIM_R(c), PRIM_G(c), PRIM_B(c)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(void)
{
    prim_fb_t fb;
    prim_fb_init(&fb, fbmem, W, H, W * sizeof(prim_pixel_t));
    prim_set_target(&fb);

    prim_fill_rect((prim_rect_t){0, 0, W, H},
                   PRIM_RGB(0x06, 0x09, 0x0E), PRIM_BLEND_REPLACE);
    prim_fill_rect_rounded((prim_rect_t){40, 40, 240, 160}, 16,
                           PRIM_RGB(0x0B, 0x10, 0x18), PRIM_BLEND_OVER);
    prim_stroke_rect_rounded((prim_rect_t){40, 40, 240, 160}, 16, 1,
                             PRIM_RGB(0x15, 0x20, 0x30));
    prim_fill_circle((prim_point_t){160, 120}, 36, PRIM_RGB(0x38, 0xBD, 0xF8));
    prim_draw_line((prim_point_t){50, 200}, (prim_point_t){270, 60}, 2,
                   PRIM_RGB(0x34, 0xD3, 0x99));

    save_ppm("hello_rect.ppm");
    printf("wrote hello_rect.ppm\n");
    return 0;
}
