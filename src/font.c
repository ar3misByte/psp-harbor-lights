/* font.c - drawing with the pixel fonts */
#include "font.h"
#include "gfx.h"
#include <string.h>

static const Glyph *glyph(const Font *f, unsigned char c)
{
    if (c < 32 || c > 126) c = '?';
    return &f->g[c - 32];
}

int text_w(const Font *f, const char *s, int scale)
{
    int w = 0;
    for (; *s; s++) w += glyph(f, (unsigned char)*s)->adv;
    return w * scale;
}

int text_h(const Font *f, int scale) { return (f->ascent + f->descent) * scale; }

static void draw_glyph(const Font *f, const Glyph *g, int px, int base, uint32_t col, int scale)
{
    int bpr = (g->w + 7) / 8;
    const unsigned char *d = f->data + g->off;
    for (int y = 0; y < g->h; y++) {
        for (int x = 0; x < g->w; x++) {
            if (d[y * bpr + x / 8] & (0x80 >> (x % 8))) {
                int xx = px + (g->xo + x) * scale, yy = base + (g->yo + y) * scale;
                if (scale == 1) gfx_pixel(xx, yy, col);
                else gfx_rect(xx, yy, scale, scale, col);
            }
        }
    }
}

void text(const Font *f, int x, int y, const char *s, uint32_t col, int scale)
{
    int base = y + f->ascent * scale;
    for (; *s; s++) {
        const Glyph *g = glyph(f, (unsigned char)*s);
        if (g->w) draw_glyph(f, g, x, base, col, scale);
        x += g->adv * scale;
    }
}

void text_sh(const Font *f, int x, int y, const char *s, uint32_t col, uint32_t shadow, int scale)
{
    text(f, x + scale, y + scale, s, shadow, scale);
    text(f, x, y, s, col, scale);
}

void text_center(const Font *f, int cx, int y, const char *s, uint32_t col, int scale)
{
    text(f, cx - text_w(f, s, scale) / 2, y, s, col, scale);
}

void text_right(const Font *f, int rx, int y, const char *s, uint32_t col, int scale)
{
    text(f, rx - text_w(f, s, scale), y, s, col, scale);
}

void text_fit(const Font *f, int x, int y, const char *s, uint32_t col, int scale, int maxw)
{
    if (text_w(f, s, scale) <= maxw) {
        text(f, x, y, s, col, scale);
        return;
    }
    char buf[128];
    int n = (int)strlen(s);
    if (n > 120) n = 120;
    int dots = text_w(f, "..", scale);
    while (n > 0) {
        memcpy(buf, s, n);
        buf[n] = 0;
        if (text_w(f, buf, scale) + dots <= maxw) break;
        n--;
    }
    strcpy(buf + n, "..");
    text(f, x, y, buf, col, scale);
}
