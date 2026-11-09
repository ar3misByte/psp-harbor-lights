/* scene.c - the pixel-art lighthouse coast
 *
 * A bay at the edge of a cliff town: sea, sandy beach with waves washing in,
 * a pier, palms, boats and gulls. Every goal is a lighthouse. The sky follows
 * the PSP clock: day, dusk, night and dawn.
 */
#include "scene.h"
#include "gfx.h"
#include "pal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const uint32_t module_colors[NUM_MODULE_COLORS] = {P_RED, P_ORANGE, P_GOLD, P_GREEN, P_CYAN,
                                                   P_SKY, P_MAGENTA, P_PINK, P_PEACH, P_LIGHT_GREY};
const char *module_color_names[NUM_MODULE_COLORS] = {"Red", "Orange", "Gold", "Green", "Cyan",
                                                      "Blue", "Purple", "Pink", "Peach", "Silver"};
const char *module_style_names[NUM_MODULE_STYLES] = {"Classic", "Tall tower", "Keeper's cottage", "Pier light"};

#define HORIZON 64
#define PI 3.14159265f

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static int ox, oy, sc = 1;
static void mp(int x, int y, uint32_t c) { gfx_rect(ox + x * sc, oy + y * sc, sc, sc, c); }
static void mr(int x, int y, int w, int h, uint32_t c) { gfx_rect(ox + x * sc, oy + y * sc, w * sc, h * sc, c); }

static unsigned int seed = 7654321;
static int rnd(int n)
{
    seed = seed * 1103515245u + 12345u;
    return (int)((seed >> 16) % (unsigned)n);
}
static const unsigned char bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static int dith(int x, int y, int level16) { return bayer[(y & 3) * 4 + (x & 3)] < level16; }

/* ------------------------------------------------------------------ */
/* time of day                                                         */

enum { P_DAY, P_DUSK, P_NIGHT_T, P_DAWN };

typedef struct {
    uint32_t sky[4];          /* top .. horizon */
    uint32_t sea_far, sea, sea_near, sea_hi;
    uint32_t sand, sand2, wet, foam;
    uint32_t grass, grass2, rock, rock2;
    uint32_t cloud, cloud2;
    int beam_alpha;
    int lights_on;            /* town windows */
} Look;

static Look looks[4];
static int cur_period = -1;

static void init_looks(void)
{
    looks[P_DAY] = (Look){{P_SKY, P_SKY, HEX(0x5cc7ef), P_CYAN},
                          P_SKY, P_BLUE, HEX(0x1a7fb5), P_CYAN,
                          P_SAND, P_TAN, P_COPPER, P_WHITE,
                          P_GREEN, P_MID_GREEN, P_GREY, P_SLATE,
                          P_WHITE, P_LIGHT_GREY, 45, 0};
    looks[P_DUSK] = (Look){{P_PLUM, P_MAGENTA, P_PINK, P_PEACH},
                           P_MAGENTA, P_PLUM, P_DARK_SLATE, P_PINK,
                           P_PEACH, P_COPPER, P_CLAY, P_SAND,
                           P_MID_GREEN, P_DARK_GREEN, P_SLATE, P_DARK_SLATE,
                           P_PINK, P_MAGENTA, 100, 1};
    looks[P_NIGHT_T] = (Look){{P_NIGHT, P_NIGHT, P_NAVY, P_DARK_SLATE},
                              P_DARK_SLATE, P_NAVY, P_NIGHT, P_SLATE,
                              P_GREY, P_SLATE, P_DARK_SLATE, P_LIGHT_GREY,
                              P_DARK_GREEN, P_DEEP_TEAL, P_DARK_SLATE, P_NAVY,
                              P_DARK_SLATE, P_NAVY, 150, 1};
    looks[P_DAWN] = (Look){{P_DARK_SLATE, P_PLUM, P_PINK, P_GOLD},
                           P_PINK, P_PLUM, P_DARK_SLATE, P_PEACH,
                           P_PEACH, P_TAN, P_CLAY, P_WHITE,
                           P_MID_GREEN, P_DARK_GREEN, P_GREY, P_SLATE,
                           P_PEACH, P_PINK, 80, 1};
}

static int period_of(int h, int mi)
{
    int m = h * 60 + mi;
    if (m < 300 || m >= 1230) return P_NIGHT_T; /* 20:30 - 5:00 */
    if (m < 420) return P_DAWN;                   /* 5:00 - 7:00 */
    if (m < 1050) return P_DAY;                   /* 7:00 - 17:30 */
    return P_DUSK;                                /* 17:30 - 20:30 */
}

/* ------------------------------------------------------------------ */
/* geography                                                           */

static int shore_y(int x) { return 108 + (int)(4.0f * sinf(x * 0.035f)); }

static int cliff_top(int x)
{
    if (x < 40) return 54 + (int)(1.5f * sinf(x * 0.5f));
    if (x > 60) return 200;
    return 54 + (x - 40) * 56 / 20; /* drops down to the beach */
}

/* lighthouse slots: base position and what it stands on */
enum { ON_CLIFF, ON_ISLET, ON_PIER, ON_BEACH };
static const struct { short x, y, on; } slots[SCENE_MAX_MODULES] = {
    {6, 54, ON_CLIFF},  {82, 90, ON_ISLET},  {124, 97, ON_PIER},  {76, 121, ON_BEACH}, {102, 75, ON_ISLET},
    {140, 84, ON_ISLET}, {58, 80, ON_ISLET}, {98, 120, ON_BEACH}, {124, 70, ON_ISLET}, {70, 70, ON_ISLET},
    {113, 128, ON_BEACH}, {143, 101, ON_ISLET}, {88, 67, ON_ISLET}, {136, 126, ON_BEACH}};

static int last_style[SCENE_MAX_MODULES];

static int lamp_height(int style)
{
    switch (style % NUM_MODULE_STYLES) {
    case 0: return 19;
    case 1: return 23;
    case 2: return 13;
    default: return 12;
    }
}

void scene_module_pos(int i, int *x, int *y)
{
    i %= SCENE_MAX_MODULES;
    *x = slots[i].x;
    *y = slots[i].y - lamp_height(last_style[i]);
}

/* ------------------------------------------------------------------ */
/* static background: sky, cliff, town, beach                         */

static uint32_t bg[SCENE_W * SCENE_H];
#define NSTARS 70
static struct { short x, y; unsigned char b; float ph; } stars[NSTARS];

static void sky_gradient(const Look *L)
{
    for (int y = 0; y < HORIZON; y++) {
        float t = (float)y / HORIZON * 3.0f;
        int i = (int)t;
        if (i > 2) i = 2;
        int lvl = (int)((t - i) * 16);
        for (int x = 0; x < SCENE_W; x++) bg[y * SCENE_W + x] = dith(x, y, lvl) ? L->sky[i + 1] : L->sky[i];
    }
}

static void bg_px(int x, int y, uint32_t c)
{
    if (x >= 0 && y >= 0 && x < SCENE_W && y < SCENE_H) bg[y * SCENE_W + x] = c;
}

static void build_background(int period)
{
    const Look *L = &looks[period];
    sky_gradient(L);
    /* far island silhouette on the horizon */
    for (int x = 176; x < 236; x++) {
        int h = (int)(6 * sinf((x - 176) * PI / 60)) + (x > 196 && x < 206 ? 2 : 0);
        for (int y = HORIZON - h; y < HORIZON; y++) bg_px(x, y, L->sea_far == P_SKY ? P_SLATE : P_NAVY);
    }
    /* sand: everything below the horizon starts as beach, the sea is drawn over it */
    for (int y = HORIZON; y < SCENE_H; y++)
        for (int x = 0; x < SCENE_W; x++) {
            uint32_t c = dith(x, y, ((x * 7 + y * 13) % 5 == 0) ? 10 : 3) ? L->sand2 : L->sand;
            bg[y * SCENE_W + x] = c;
        }
    /* little beach things: shells, pebbles, a starfish */
    seed = 99;
    for (int k = 0; k < 40; k++) {
        int x = 66 + rnd(174), y = shore_y(x) + 6 + rnd(SCENE_H - shore_y(x) - 6 > 0 ? SCENE_H - shore_y(x) - 6 : 1);
        bg_px(x, y, k % 3 ? L->sand2 : L->foam);
    }
    int sx = 168, sy = 126;
    uint32_t star_c = period == P_NIGHT_T ? P_CLAY : P_ORANGE;
    bg_px(sx, sy, star_c); bg_px(sx - 1, sy, star_c); bg_px(sx + 1, sy, star_c); bg_px(sx, sy - 1, star_c); bg_px(sx - 1, sy + 1, star_c); bg_px(sx + 1, sy + 1, star_c);

    /* the headland cliff with grass on top */
    for (int x = 0; x < 62; x++) {
        int top = cliff_top(x);
        int bottom = shore_y(x) + 2;
        for (int y = top; y < bottom && y < SCENE_H; y++) {
            uint32_t c;
            if (y < top + 2) c = L->grass;
            else if (y < top + 3) c = L->grass2;
            else {
                /* layered rock face: strata, cracks and a lit edge */
                int band = ((y - top + (x * 3) / 7) / 4) % 4;
                c = band == 0 ? L->rock2 : (band == 2 && dith(x, y, 6)) ? L->rock2 : L->rock;
                if ((x * 7 + y * 3) % 23 == 0) c = L->rock2;
                if (x > 2 && cliff_top(x - 3) > y) c = gfx_mix(L->rock, P_WHITE, 40);
                if (y > bottom - 4 && dith(x, y, 8)) c = L->rock2;
            }
            bg_px(x, y, c);
        }
    }
    /* grass tufts where the cliff meets the sand */
    for (int x = 54; x < 72; x += 3) {
        int y = shore_y(x) + 3;
        bg_px(x, y, L->grass); bg_px(x + 1, y - 1, L->grass); bg_px(x + 2, y, L->grass2);
    }
    /* stars (only drawn at night) */
    seed = 4242;
    for (int i = 0; i < NSTARS; i++) {
        stars[i].x = (short)rnd(SCENE_W);
        stars[i].y = (short)rnd(HORIZON - 6);
        stars[i].b = (unsigned char)rnd(10);
        stars[i].ph = rnd(628) / 100.0f;
    }
    cur_period = period;
}

static void beach_props(const Look *L)
{
    /* palm trees on the right of the beach */
    static const int palms[2][2] = {{186, 118}, {214, 124}};
    for (int p = 0; p < 2; p++) {
        int px = palms[p][0], py = palms[p][1];
        for (int k = 0; k < 22; k++) {
            int x = px + (int)(sinf(k * 0.08f) * 4), y = py - k;
            gfx_pixel(x, y, k % 3 == 0 ? P_BROWN : P_CLAY);
            gfx_pixel(x + 1, y, P_BROWN);
        }
        int tx = px + 5, ty = py - 22;
        static const int fronds[6][2] = {{-9, 3}, {-6, -3}, {0, -5}, {7, -3}, {10, 3}, {3, 4}};
        for (int f = 0; f < 6; f++)
            for (int k = 0; k <= 8; k++) {
                float t = k / 8.0f;
                int x = tx + (int)(fronds[f][0] * t), y = ty + (int)(fronds[f][1] * t + 3 * t * t);
                gfx_pixel(x, y, L->grass);
                gfx_pixel(x, y + 1, L->grass2);
            }
        gfx_pixel(tx, ty + 1, P_BROWN); gfx_pixel(tx + 1, ty + 2, P_BROWN);
    }
    /* beach umbrella */
    int ux = 160, uy = 112;
    for (int k = 0; k < 12; k++) gfx_pixel(ux, uy + k, P_LIGHT_GREY);
    for (int r = 0; r < 4; r++)
        for (int x = -9 + r * 2; x <= 9 - r * 2; x++) gfx_pixel(ux + x, uy - r, ((x + 20) / 3) % 2 ? P_RED : P_WHITE);

}

/* ------------------------------------------------------------------ */
/* moving parts                                                        */

static void draw_sky_objects(int period, int hour, int minute, float t)
{
    const Look *L = &looks[period];
    if (period == P_NIGHT_T) {
        for (int i = 0; i < NSTARS; i++) {
            float tw = sinf(t * (1 + (i % 4) * 0.5f) + stars[i].ph);
            uint32_t c = stars[i].b > 7 ? P_WHITE : stars[i].b > 3 ? P_LIGHT_GREY : P_SLATE;
            if (tw > -0.3f) gfx_pixel(stars[i].x, stars[i].y, c);
        }
        /* crescent moon */
        int mx = 54, my = 16;
        for (int y = -6; y <= 6; y++)
            for (int x = -6; x <= 6; x++)
                if (x * x + y * y <= 36 && (x - 3) * (x - 3) + (y + 1) * (y + 1) > 22) gfx_pixel(mx + x, my + y, (x + y < -3) ? P_WHITE : P_LIGHT_GREY);
    } else {
        /* the sun moves across the sky during the day */
        int m = hour * 60 + minute;
        float f = (m - 360) / 780.0f; /* 6:00 .. 19:00 */
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        int sxp = 30 + (int)(f * 180), syp = HORIZON - (int)(sinf(f * PI) * 48) + 4;
        if (period != P_DAY) syp = HORIZON - 3;
        if (period == P_DAWN) sxp = 40;
        if (period == P_DUSK) sxp = 196;
        int r = period == P_DAY ? 7 : 9;
        uint32_t c1 = period == P_DAY ? P_YELLOW : P_GOLD, c2 = period == P_DAY ? P_WHITE : P_ORANGE;
        gfx_circle_alpha(sxp, syp, r + 4, c1, 60);
        for (int y = -r; y <= r; y++)
            for (int x = -r; x <= r; x++)
                if (x * x + y * y <= r * r && syp + y < HORIZON) gfx_pixel(sxp + x, syp + y, (x + y < -r / 2) ? c2 : c1);
    }
    /* clouds drifting */
    static const int cl[4][3] = {{20, 14, 0}, {110, 26, 1}, {170, 10, 2}, {220, 32, 1}};
    for (int k = 0; k < 4; k++) {
        int w = 18 + cl[k][2] * 6;
        int x = (int)(cl[k][0] + t * (1.5f + k * 0.4f)) % (SCENE_W + 60) - 30, y = cl[k][1];
        gfx_rect(x + 4, y, w - 8, 3, L->cloud);
        gfx_rect(x, y + 3, w, 3, L->cloud);
        gfx_rect(x + 6, y - 2, w / 3, 2, L->cloud);
        gfx_rect(x + 2, y + 6, w - 4, 1, L->cloud2);
    }
}

static void draw_sea(int period, float t, int sunx)
{
    const Look *L = &looks[period];
    for (int x = 0; x < SCENE_W; x++) {
        float wave = 1.5f + 1.5f * sinf(t * 1.1f + x * 0.045f);
        int edge = shore_y(x) + (int)wave;
        for (int y = HORIZON; y < edge && y < SCENE_H; y++) {
            int d = y - HORIZON;
            uint32_t c;
            if (d < 3) c = L->sea_far;
            else if (y > edge - 6) c = dith(x, y, 8) ? L->sea_near : L->sea;
            else c = (d < 8 && dith(x, y, 8)) ? L->sea_far : L->sea;
            gfx_pixel(x, y, c);
        }
        /* foam line where the wave reaches, and darker wet sand behind it */
        if (edge < SCENE_H) gfx_pixel(x, edge, L->foam);
        if (edge + 1 < SCENE_H && sinf(t * 1.1f + x * 0.045f) > 0.6f) gfx_pixel(x, edge - 1, L->foam);
        for (int y = edge + 1; y < shore_y(x) + 5 && y < SCENE_H; y++) gfx_pixel(x, y, L->wet);
    }
    /* glittering ripples */
    for (int k = 0; k < 46; k++) {
        int row = HORIZON + 4 + (k * 37) % 40;
        int x = (int)(k * 53 + t * (4 + k % 5)) % (SCENE_W + 20) - 10;
        if (row >= shore_y(x) - 1) continue;
        int len = 2 + k % 4;
        gfx_rect(x, row, len, 1, L->sea_hi);
    }
    /* sun / moon reflection */
    int rx = period == P_NIGHT_T ? 54 : sunx;
    for (int y = HORIZON + 1; y < HORIZON + 34; y += 2) {
        int w = 2 + (y - HORIZON) / 6;
        int jitter = (int)(sinf(t * 3 + y) * 2);
        if (((y + (int)(t * 4)) % 4) < 3)
            gfx_rect(rx - w / 2 + jitter, y, w, 1, period == P_NIGHT_T ? P_LIGHT_GREY : (period == P_DAY ? P_WHITE : P_GOLD));
    }
}

/* ------------------------------------------------------------------ */
/* lighthouses                                                         */

static uint32_t lamp_col(const SceneModule *m, float t)
{
    if (m->alert) return ((int)(t * 5)) % 2 ? P_RED : P_DARK_RED;
    if (m->complete) return P_GOLD;
    if (m->power <= 0.01f) return P_SLATE;
    return m->power < 0.5f ? P_GOLD : P_YELLOW;
}

/* origin = base centre */
static void lantern(int y, uint32_t lc, uint32_t capc)
{
    mr(-4, y + 3, 9, 1, P_NIGHT);   /* gallery */
    mp(-4, y + 2, P_NIGHT); mp(4, y + 2, P_NIGHT);
    mr(-2, y, 5, 3, P_NIGHT);        /* lantern room frame */
    mr(-1, y, 3, 2, lc);             /* the lamp */
    mr(-2, y - 1, 5, 1, capc);       /* roof */
    mr(-1, y - 2, 3, 1, capc);
    mp(0, y - 3, P_NIGHT);           /* finial */
}

static void draw_classic(const SceneModule *m, float t, uint32_t col, uint32_t capc)
{
    /* tapered striped tower, 16 rows */
    for (int r = 0; r < 16; r++) {
        int y = -1 - r;
        int hw = 3 - (r >= 8 ? 1 : 0);
        uint32_t c = ((r / 3) % 2 == 0) ? col : P_WHITE;
        mr(-hw - 1, y, 1, 1, P_NIGHT);
        mr(hw + 1, y, 1, 1, P_NIGHT);
        mr(-hw, y, hw * 2 + 1, 1, c);
        mp(hw, y, gfx_shade(c, 72));
        mp(-hw, y, gfx_mix(c, P_WHITE, 80));
    }
    mr(0, -3, 1, 2, P_BROWN); /* door */
    mp(0, -10, P_NIGHT);      /* window */
    lantern(-20, lamp_col(m, t), capc);
}

static void draw_tall(const SceneModule *m, float t, uint32_t col, uint32_t capc)
{
    for (int r = 0; r < 20; r++) {
        int y = -1 - r;
        int hw = 2;
        uint32_t c = (r >= 8 && r < 12) ? P_WHITE : col;
        mp(-hw - 1, y, P_NIGHT);
        mp(hw + 1, y, P_NIGHT);
        mr(-hw, y, hw * 2 + 1, 1, c);
        mp(hw, y, gfx_shade(c, 72));
    }
    mr(-4, -1, 9, 1, P_NIGHT); /* plinth */
    mp(0, -15, P_NIGHT);
    mp(0, -5, P_NIGHT);
    lantern(-24, lamp_col(m, t), capc);
}

static void draw_cottage(const SceneModule *m, float t, uint32_t col, uint32_t capc)
{
    /* short tower */
    for (int r = 0; r < 10; r++) {
        int y = -1 - r;
        mp(-3, y, P_NIGHT);
        mp(3, y, P_NIGHT);
        mr(-2, y, 5, 1, P_WHITE);
        mp(2, y, P_LIGHT_GREY);
    }
    mr(-2, -6, 5, 2, col);
    lantern(-14, lamp_col(m, t), capc);
    /* keeper's house on the left */
    mr(-11, -6, 8, 6, P_NIGHT);
    mr(-10, -5, 6, 5, P_WHITE);
    mr(-11, -8, 8, 2, col);
    mr(-10, -9, 6, 1, col);
    mr(-12, -7, 1, 1, gfx_shade(col, 70));
    mp(-8, -3, (looks[cur_period < 0 ? 0 : cur_period].lights_on) ? P_YELLOW : P_BLUE);
    mr(-6, -2, 1, 2, P_BROWN);
}

static void draw_pierlight(const SceneModule *m, float t, uint32_t col, uint32_t capc)
{
    /* skeleton tower on legs */
    for (int r = 0; r < 9; r++) {
        int w = 4 - r / 3;
        mp(-w, -1 - r, P_GREY);
        mp(w, -1 - r, P_GREY);
        if (r % 3 == 1) mr(-w, -1 - r, w * 2 + 1, 1, P_SLATE);
    }
    mr(-2, -12, 5, 3, P_NIGHT);
    mr(-1, -11, 3, 2, col);
    mr(-1, -13, 3, 1, lamp_col(m, t));
    mp(0, -14, capc);
}

static void draw_lighthouse(int bx, int by, const SceneModule *m, float t, int scale)
{
    ox = bx;
    oy = by;
    sc = scale;
    uint32_t col = module_colors[m->color % NUM_MODULE_COLORS];
    uint32_t capc = m->complete ? P_GOLD : P_DARK_RED;
    if (m->color == 0 && !m->complete) capc = P_NIGHT; /* red stripes get a dark roof */
    switch (m->style % NUM_MODULE_STYLES) {
    case 0: draw_classic(m, t, col, capc); break;
    case 1: draw_tall(m, t, col, capc); break;
    case 2: draw_cottage(m, t, col, capc); break;
    default: draw_pierlight(m, t, col, capc); break;
    }
    if (m->complete) {
        /* little flag on the roof */
        int top = -lamp_height(m->style) - 4;
        mp(2, top, P_GREY); mp(2, top - 1, P_GREY); mp(2, top - 2, P_GREY);
        mr(3, top - 2, 3, 2, P_GOLD);
    }
    sc = 1;
}

void scene_draw_module_at(int cx, int cy, const SceneModule *m, float t, int scale)
{
    draw_lighthouse(cx, cy + 12 * scale, m, t, scale);
}

/* sweeping light beam: the lamp turns, so the beam swings left and right */
static void beam(int lx, int ly, float power, int complete, float t, float phase, int alpha)
{
    float a = t * 0.9f + phase;
    float c = cosf(a);
    int dir = c >= 0 ? 1 : -1;
    float len = (complete ? 70 : 20 + 55 * power) * fabsf(c);
    if (len < 3) return;
    for (int k = 2; k < (int)len; k++) {
        float f = (float)k / len;
        int half = 1 + (int)(k * 0.12f);
        int al = (int)(alpha * (1 - f));
        int x = lx + dir * k;
        int yc = ly + (int)(k * 0.06f);
        for (int y = -half; y <= half; y++) gfx_blend_pixel(x, yc + y, complete ? P_GOLD : P_YELLOW, al * (half - abs(y) + 1) / (half + 1));
    }
}

static void islet(int x, int y, const Look *L, float t)
{
    for (int r = 0; r < 5; r++) {
        int hw = 8 - r * 2 + (r == 0 ? 1 : 0);
        for (int dx = -hw; dx <= hw; dx++) gfx_pixel(x + dx, y + 2 - r, (dx < -hw / 2) ? L->rock : L->rock2);
    }
    gfx_rect(x - 6, y - 2, 3, 1, L->rock);
    /* foam ring */
    for (int dx = -10; dx <= 10; dx++) {
        if ((dx + (int)(t * 6)) % 4 == 0) gfx_pixel(x + dx, y + 3, L->foam);
    }
}

static void pier(int x, int y_end, const Look *L)
{
    int y0 = shore_y(x) + 3;
    for (int y = y_end; y <= y0; y++) {
        gfx_rect(x - 4, y, 9, 1, (y % 2) ? P_CLAY : P_BROWN);
        if (y % 4 == 0) {
            gfx_pixel(x - 4, y + 1, P_DARK_BROWN);
            gfx_pixel(x + 4, y + 1, P_DARK_BROWN);
        }
    }
    gfx_rect(x - 5, y_end, 1, y0 - y_end, P_DARK_BROWN);
    gfx_rect(x + 5, y_end, 1, y0 - y_end, P_DARK_BROWN);
    (void)L;
}

static void town(int level, const Look *L, float t)
{
    /* houses on the cliff top; more appear as you level up */
    int n = 2 + level / 2;
    if (n > 5) n = 5;
    static const int hx[5] = {16, 23, 30, 12, 37};
    static const uint32_t roofs[5] = {P_RED, P_SKY, P_ORANGE, P_MID_GREEN, P_MAGENTA};
    for (int i = 0; i < n; i++) {
        int x = hx[i], y = cliff_top(x) - 1 - (i >= 3 ? 0 : 0);
        if (i == 4) y = cliff_top(37) - 1;
        gfx_rect(x - 3, y - 4, 7, 5, P_NIGHT);
        gfx_rect(x - 2, y - 3, 5, 4, i % 2 ? P_SAND : P_WHITE);
        gfx_rect(x - 3, y - 6, 7, 2, roofs[i]);
        gfx_rect(x - 2, y - 7, 5, 1, roofs[i]);
        int lit = L->lights_on && ((int)(t * 0.3f + i) % 7 != 0);
        gfx_pixel(x - 1, y - 2, lit ? P_YELLOW : P_BLUE);
        gfx_pixel(x + 1, y - 2, lit ? P_GOLD : P_BLUE);
        gfx_pixel(x, y, P_BROWN);
    }
}

static void gull(float x, float y, float t, uint32_t c)
{
    int ix = (int)x, iy = (int)y;
    int up = ((int)(t * 6)) % 2;
    gfx_pixel(ix, iy, c);
    gfx_pixel(ix - 1, iy - (up ? 1 : 0), c);
    gfx_pixel(ix + 1, iy - (up ? 1 : 0), c);
    gfx_pixel(ix - 2, iy - (up ? 2 : 0), c);
    gfx_pixel(ix + 2, iy - (up ? 2 : 0), c);
}

static void sailboat(int x, int y, uint32_t sail, uint32_t hull)
{
    gfx_rect(x - 5, y, 11, 2, hull);
    gfx_rect(x - 4, y + 2, 9, 1, gfx_shade(hull, 70));
    gfx_rect(x, y - 9, 1, 9, P_BROWN);
    for (int r = 0; r < 8; r++) gfx_rect(x + 1, y - 8 + r, 1 + r / 2, 1, sail);
    for (int r = 0; r < 5; r++) gfx_rect(x - 1 - r / 2, y - 5 + r, 1 + r / 2, 1, gfx_shade(sail, 85));
}

/* ------------------------------------------------------------------ */
/* effects                                                             */

#define NPART 96
static struct { float x, y, vx, vy, life; uint32_t c; } parts[NPART];
#define NBOATS 4
static struct { float x, y, tx, life; uint32_t c; } boats[NBOATS];

void scene_burst(int i, uint32_t col)
{
    int x, y;
    scene_module_pos(i, &x, &y);
    for (int k = 0, made = 0; k < NPART && made < 18; k++) {
        if (parts[k].life > 0) continue;
        float a = rnd(628) / 100.0f, sp = 12 + rnd(26);
        parts[k].x = (float)x;
        parts[k].y = (float)y;
        parts[k].vx = cosf(a) * sp;
        parts[k].vy = sinf(a) * sp - 10;
        parts[k].life = 0.5f + rnd(50) / 100.0f;
        parts[k].c = (made % 3 == 0) ? P_YELLOW : (made % 3 == 1 ? col : P_WHITE);
        made++;
    }
}

void scene_launch(int i, uint32_t col)
{
    /* a boat sails in from the open sea to the lighthouse */
    i %= SCENE_MAX_MODULES;
    for (int k = 0; k < NBOATS; k++)
        if (boats[k].life <= 0) {
            boats[k].x = SCENE_W + 10;
            boats[k].y = (float)(slots[i].y < 100 ? slots[i].y + 4 : 100);
            if (boats[k].y < HORIZON + 8) boats[k].y = HORIZON + 8;
            boats[k].tx = slots[i].x + 12;
            boats[k].life = 9;
            boats[k].c = col;
            return;
        }
}

void scene_update(float dt)
{
    for (int k = 0; k < NPART; k++)
        if (parts[k].life > 0) {
            parts[k].life -= dt;
            parts[k].x += parts[k].vx * dt;
            parts[k].y += parts[k].vy * dt;
            parts[k].vy += 30 * dt;
        }
    for (int k = 0; k < NBOATS; k++)
        if (boats[k].life > 0) {
            boats[k].life -= dt;
            if (boats[k].x > boats[k].tx) boats[k].x -= 28 * dt; /* sail in, then stay a while */
        }
}

/* ------------------------------------------------------------------ */

void scene_init(void)
{
    init_looks();
    build_background(P_DAY);
}

typedef struct { int i, y; } Order;

void scene_draw(uint32_t *lowres, const SceneState *s)
{
    int period = period_of(s->hour, s->minute);
    if (period != cur_period) build_background(period);
    const Look *L = &looks[period];
    float t = s->t;

    gfx_set_target(lowres);
    gfx_clip_push(0, 0, SCENE_W, SCENE_H);
    for (int y = 0; y < SCENE_H; y++) memcpy(lowres + y * BUF_W, bg + y * SCENE_W, SCENE_W * 4);

    int m = s->hour * 60 + s->minute;
    float f = (m - 360) / 780.0f;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    int sunx = period == P_DAY ? 30 + (int)(f * 180) : (period == P_DAWN ? 40 : 196);
    draw_sky_objects(period, s->hour, s->minute, t);
    draw_sea(period, t, sunx);

    /* the cliff covers the sea on the left (redraw from the background) */
    for (int x = 0; x < 62; x++)
        for (int y = cliff_top(x); y < shore_y(x) + 2 && y < SCENE_H; y++) gfx_pixel(x, y, bg[y * SCENE_W + x]);
    town(s->level, L, t);
    beach_props(L);

    /* sailboats out at sea at higher levels */
    if (s->level >= 4) sailboat((int)(SCENE_W + 20 - fmodf(t * 4, SCENE_W + 40)), HORIZON + 6, P_WHITE, P_RED);
    if (s->level >= 9) sailboat((int)(fmodf(t * 3 + 80, SCENE_W + 40) - 20), HORIZON + 14, P_GOLD, P_BLUE);

    int n = s->n > SCENE_MAX_MODULES ? SCENE_MAX_MODULES : s->n;
    for (int i = 0; i < n; i++) last_style[i] = s->m[i].style;

    /* draw back to front */
    Order ord[SCENE_MAX_MODULES];
    for (int i = 0; i < n; i++) { ord[i].i = i; ord[i].y = slots[i].y; }
    for (int a = 1; a < n; a++)
        for (int b = a; b > 0 && ord[b - 1].y > ord[b].y; b--) { Order tmp = ord[b]; ord[b] = ord[b - 1]; ord[b - 1] = tmp; }
    for (int k = 0; k < n; k++) {
        int i = ord[k].i;
        int bx = slots[i].x, by = slots[i].y;
        if (slots[i].on == ON_ISLET) islet(bx, by, L, t + i);
        else if (slots[i].on == ON_PIER) pier(bx, by, L);
        else if (slots[i].on == ON_BEACH) gfx_rect(bx - 4, by - 1, 9, 2, L->rock);
        draw_lighthouse(bx, by, &s->m[i], t + i * 0.4f, 1);
    }
    /* boats sailing in */
    for (int k = 0; k < NBOATS; k++)
        if (boats[k].life > 0 && boats[k].life > 0.0f) sailboat((int)boats[k].x, (int)boats[k].y, P_WHITE, boats[k].c);

    /* light: halos and beams on top of everything */
    for (int i = 0; i < n; i++) {
        const SceneModule *mm = &s->m[i];
        int lx, ly;
        scene_module_pos(i, &lx, &ly);
        if (mm->alert) continue;
        if (mm->power <= 0.01f && !mm->complete) continue;
        if (L->beam_alpha >= 90) gfx_circle_alpha(lx, ly, 4, mm->complete ? P_GOLD : P_YELLOW, L->beam_alpha / 3);
        beam(lx, ly, mm->power, mm->complete, t, i * 1.7f, L->beam_alpha);
    }
    /* gulls */
    if (s->level >= 2) {
        int ng = s->level >= 6 ? 3 : 2;
        for (int g = 0; g < ng; g++) {
            float gx = fmodf(t * (8 + g * 3) + g * 70, SCENE_W + 30) - 15;
            float gy = 22 + g * 9 + sinf(t * 0.7f + g) * 5;
            gull(gx, gy, t + g, period == P_NIGHT_T ? P_LIGHT_GREY : P_WHITE);
        }
    }
    /* selection marker: a bouncing arrow above the lighthouse */
    if (s->sel >= 0 && s->sel < n) {
        int lx, ly;
        scene_module_pos(s->sel, &lx, &ly);
        int ay = ly - 9 - (int)(fabsf(sinf(t * 4)) * 3);
        gfx_rect(lx - 3, ay, 7, 1, P_NIGHT);
        gfx_rect(lx - 2, ay + 1, 5, 1, P_YELLOW);
        gfx_rect(lx - 1, ay + 2, 3, 1, P_YELLOW);
        gfx_pixel(lx, ay + 3, P_YELLOW);
        gfx_rect(lx - 3, ay - 1, 7, 1, P_YELLOW);
    }
    for (int k = 0; k < NPART; k++)
        if (parts[k].life > 0) gfx_pixel((int)parts[k].x, (int)parts[k].y, parts[k].c);
    gfx_clip_pop();
}

void scene_blit2x(uint32_t *dst, const uint32_t *lowres)
{
    for (int y = 0; y < SCENE_H; y++) {
        const uint32_t *src = lowres + y * BUF_W;
        uint32_t *d0 = dst + (y * 2) * BUF_W, *d1 = d0 + BUF_W;
        for (int x = 0; x < SCENE_W; x++) {
            uint32_t c = src[x];
            d0[x * 2] = d0[x * 2 + 1] = c;
            d1[x * 2] = d1[x * 2 + 1] = c;
        }
    }
}
