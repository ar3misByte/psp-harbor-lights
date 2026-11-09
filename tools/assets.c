/* assets.c - renders the XMB icon (ICON0) and background (PIC1) from the coast art */
#include "scene.h"
#include "gfx.h"
#include "pal.h"
#include "font.h"
#include <stdio.h>
static uint32_t lo[BUF_W * SCENE_H], fb[BUF_W * 272];
static void save(const char *p, int w, int h)
{
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = fb[y * BUF_W + x];
            unsigned char q[3] = {C_R(c), C_G(c), C_B(c)};
            fwrite(q, 1, 3, f);
        }
    fclose(f);
}
int main(void)
{
    scene_init();
    SceneState s = {0};
    s.n = 8; s.sel = -1; s.t = 2.5f; s.level = 10; s.hour = 19; s.minute = 0;
    int cols[8] = {0, 4, 1, 2, 5, 7, 3, 6};
    for (int i = 0; i < 8; i++) { s.m[i].color = cols[i]; s.m[i].style = i % 4; s.m[i].power = 1; }
    scene_draw(lo, &s);
    gfx_set_target(fb);
    scene_blit2x(fb, lo);
    text(&font_title, 300 + 2, 40 + 2, "HARBOR", P_NIGHT, 3);
    text(&font_title, 300, 40, "HARBOR", P_YELLOW, 3);
    text(&font_title, 300 + 2, 70 + 2, "LIGHTS", P_NIGHT, 3);
    text(&font_title, 300, 70, "LIGHTS", P_GOLD, 3);
    text(&font_body, 302, 98, "keep your goals shining", P_WHITE, 1);
    save("pic1.ppm", 480, 272);
    /* icon: a tight crop of the bay, at native pixel size */
    s.hour = 20; s.minute = 0;
    scene_draw(lo, &s);
    for (int y = 0; y < 80; y++)
        for (int x = 0; x < 144; x++) fb[y * BUF_W + x] = lo[(y + 34) * BUF_W + (x + 6)];
    gfx_set_target(fb);
    gfx_clip(0, 0, 144, 80);
    gfx_rect_alpha(88, 1, 54, 21, P_NIGHT, 190);
    gfx_rect(88, 22, 54, 1, P_GOLD);
    text(&font_small, 92 + 1, 3 + 1, "HARBOR", P_NIGHT, 1);
    text(&font_small, 92, 3, "HARBOR", P_YELLOW, 1);
    text(&font_small, 96 + 1, 12 + 1, "LIGHTS", P_NIGHT, 1);
    text(&font_small, 96, 12, "LIGHTS", P_GOLD, 1);
    save("icon0.ppm", 144, 80);
    return 0;
}
