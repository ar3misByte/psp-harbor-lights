/* app.c - Harbor Lights: screens, controls and effects */
#include "app.h"
#include "font.h"
#include "gfx.h"
#include "pal.h"
#include "scene.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* state                                                               */

static Station st;
static char save_path[256];
static DateTime now;
static int today;
static float T = 0;  /* seconds since start */
static int battery = -1;

static uint32_t lowres[BUF_W * SCENE_H];

enum { SCR_HOME, SCR_GOALS, SCR_DETAIL, SCR_EDIT, SCR_STEPS, SCR_SETTINGS, SCR_HELP };
static int screen = SCR_HOME;

enum { OV_NONE, OV_MENU, OV_KEYBOARD, OV_DATE, OV_CONFIRM };
static int overlay = OV_NONE;

/* input */
static unsigned keys = 0, keys_old = 0;
static float rep_t[16];
static int pr(unsigned k) { return (keys & k) && !(keys_old & k); }
static int rp(unsigned k)
{
    int b = 0;
    while (!((1u << b) & k)) b++;
    if (pr(k)) { rep_t[b] = 0.35f; return 1; }
    return (keys & k) && rep_t[b] <= 0 && (rep_t[b] = 0.06f, 1);
}

/* derived values */
static int cur_xp = 0, cur_level = 1, xp_into = 0, xp_need = 100;
static int was_complete[MAX_GOALS];
static int dirty = 0;

/* effects */
#define NFLOAT 8
static struct { float x, y, life; char txt[24]; uint32_t c; } floaters[NFLOAT];
static float levelup_t = 0;
static int levelup_to = 0;
static char toast_msg[96];
static float toast_t = 0;

/* home */
static int home_sel = 0, home_scroll = 0, hide_ui = 0;
static int today_list[MAX_GOALS], today_n = 0;

/* goals list */
static int gl_sel = 0, gl_scroll = 0, gl_moving = 0;

/* detail */
static int det_i = 0;
static int cal_edit = 0, cal_day = 0;
static int amount_i = 0;
static const int amounts[7] = {1, 2, 5, 10, 25, 50, 100};
static int step_sel = 0;

/* editor */
static Goal eg;          /* working copy */
static int eg_index = -1; /* -1 = new */
static int ed_sel = 0, ed_scroll = 0, ed_day = 0;
static int ed_changed = 0;
enum { F_NAME, F_TYPE, F_DAYS, F_TIMES, F_TARGET, F_UNIT, F_STEPS, F_START, F_DEADLINE, F_EVENT,
       F_XP_EACH, F_XP_BONUS, F_COLOR, F_STYLE, F_STATUS, F_DELETE, F_SAVE };
static int fields[20], nfields = 0;

/* steps editor */
static int st_sel = 0, st_moving = 0;

/* settings */
static int set_sel = 0;

/* menu */
static int menu_sel = 0;

/* keyboard */
static char kb_buf[64];
static int kb_max = 31, kb_row = 1, kb_col = 0, kb_shift = 0;
static char kb_title[32];
static char *kb_target = NULL;
static int kb_then = 0; /* what to do after: 0 = nothing, 1 = add step */

/* date picker */
static int dp_v[5]; /* d, m, y, h, mi */
static int dp_field = 0, dp_time = 0;
static int dp_target = 0; /* 1 start day, 2 deadline, 3 event */

/* confirm */
static char cf_msg[96], cf_msg2[96];
static int cf_action = 0, cf_param = 0;
enum { ACT_DELETE_GOAL = 1, ACT_DELETE_STEP, ACT_DISCARD, ACT_COUNTDOWN_DONE };

static const char *type_names[G_NTYPES] = {"Daily habit", "Counter", "Weekly target", "Milestones", "Countdown"};
static const char *type_tags[G_NTYPES] = {"HABIT", "COUNTER", "WEEKLY", "MILESTONES", "COUNTDOWN"};
static const char *type_desc[G_NTYPES] = {
    "Check in on the days you choose.", "Count up to a number: books, km...",
    "Do it a number of times each week.", "A checklist of steps to finish.", "Days left until a date and time."};

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static void toast(const char *m)
{
    strncpy(toast_msg, m, sizeof toast_msg - 1);
    toast_msg[sizeof toast_msg - 1] = 0;
    toast_t = 2.2f;
}

static uint32_t gcol(const Goal *g) { return module_colors[g->color % NUM_MODULE_COLORS]; }

static void module_screen_pos(int i, int *x, int *y)
{
    int lx, ly;
    scene_module_pos(i, &lx, &ly);
    *x = lx * 2;
    *y = ly * 2;
}

static void add_floater(int gi, int dxp)
{
    if (dxp == 0) return;
    for (int k = 0; k < NFLOAT; k++)
        if (floaters[k].life <= 0) {
            int x = 160, y = 140;
            if (screen != SCR_HOME) { /* under the XP bar while a panel covers the coast */
                int active = 0;
                for (int j = 0; j < NFLOAT; j++) active += floaters[j].life > 0;
                x = 300 + active * 70;
                y = 46;
            }
            else if (gi >= 0 && gi < SCENE_MAX_MODULES) module_screen_pos(gi, &x, &y);
            floaters[k].x = (float)x;
            floaters[k].y = (float)y - 20;
            floaters[k].life = 1.6f;
            snprintf(floaters[k].txt, sizeof floaters[k].txt, "%+d XP", dxp);
            floaters[k].c = dxp > 0 ? P_YELLOW : P_RED;
            return;
        }
}

static void recompute(int quiet, int focus)
{
    int old_xp = cur_xp, old_level = cur_level;
    cur_xp = station_xp(&st, today);
    level_from_xp(&st.s, cur_xp, &cur_level, &xp_into, &xp_need);
    for (int i = 0; i < st.n; i++) {
        int c = goal_complete(&st.goals[i], &now);
        if (!quiet && c && !was_complete[i]) {
            if (i < SCENE_MAX_MODULES) scene_launch(i, gcol(&st.goals[i]));
            snd_play(SND_LAUNCH);
            char m[80];
            snprintf(m, sizeof m, "Goal complete: %s!", st.goals[i].name);
            toast(m);
        }
        was_complete[i] = c;
    }
    if (!quiet) {
        add_floater(focus, cur_xp - old_xp);
        if (cur_level > old_level) {
            levelup_t = 3.0f;
            levelup_to = cur_level;
            snd_play(SND_LEVEL);
        }
    }
}

static void changed(int focus)
{
    dirty = 1;
    recompute(0, focus);
}

static void fmt_dur(char *b, int n, long minutes)
{
    if (minutes < 0) minutes = 0;
    long d = minutes / 1440, h = (minutes % 1440) / 60, m = minutes % 60;
    if (d > 0) snprintf(b, n, "%ldd %02ldh %02ldm", d, h, m);
    else snprintf(b, n, "%ldh %02ldm", h, m);
}

static void deadline_text(const Goal *g, char *b, int n)
{
    char dt[48];
    fmt_datetime(dt, sizeof dt, &g->deadline, st.s.clock24);
    long left = dt_minutes(&g->deadline) - dt_minutes(&now);
    long days = left >= 0 ? (left + 1439) / 1440 : (-left) / 1440;
    if (left >= 0) snprintf(b, n, "%s  -  %ld day%s left", dt, days, days == 1 ? "" : "s");
    else snprintf(b, n, "%s  -  overdue by %ld day%s", dt, days, days == 1 ? "" : "s");
}

/* ------------------------------------------------------------------ */
/* pixel sprites                                                       */

static uint32_t spr_col(char ch, uint32_t tint)
{
    switch (ch) {
    case 'o': return P_ORANGE;
    case 'O': return P_GOLD;
    case 'Y': return P_YELLOW;
    case 'W': return P_WHITE;
    case 'R': return P_RED;
    case 'r': return P_DARK_RED;
    case 'G': return P_GREEN;
    case 'g': return P_MID_GREEN;
    case 'S': return P_SLATE;
    case 's': return P_DARK_SLATE;
    case 'K': return P_NIGHT;
    case 'B': return P_BLUE;
    case 'C': return P_CYAN;
    case 'L': return P_LIGHT_GREY;
    case 'P': return P_PINK;
    case 'T': return tint;
    }
    return 0;
}

static void sprite(int x, int y, const char *const *rows, int h, int scale, uint32_t tint)
{
    for (int r = 0; r < h; r++)
        for (int c = 0; rows[r][c]; c++) {
            uint32_t col = spr_col(rows[r][c], tint);
            if (col) gfx_rect(x + c * scale, y + r * scale, scale, scale, col);
        }
}

static const char *const SPR_FLAME[7] = {"..o..", ".oOo.", ".oOo.", "oOYOo", "oOYOo", ".oYo.", "..o.."};
static const char *const SPR_STAR[7] = {"...Y...", "...Y...", ".YYOYY.", "YYOOOYY", ".YOOOY.", ".Y...Y.", "Y.....Y"};
static const char *const SPR_CHECK_ON[9] = {"KKKKKKKKK", "KGGGGGGGK", "KGGGGGWGK", "KGGGGWWGK", "KGWGWWGGK", "KGWWWGGGK", "KGGWGGGGK", "KGGGGGGGK", "KKKKKKKKK"};
static const char *const SPR_CHECK_OFF[9] = {"SSSSSSSSS", "SKKKKKKKS", "SKKKKKKKS", "SKKKKKKKS", "SKKKKKKKS", "SKKKKKKKS", "SKKKKKKKS", "SKKKKKKKS", "SSSSSSSSS"};
static const char *const SPR_CLOCK[7] = {".LLLLL.", "LKKWKKL", "LKKWKKL", "LKKWWKL", "LKKKKKL", "LKKKKKL", ".LLLLL."};

/* PSP-style button icons: 11x11 */
static void btn(int x, int y, unsigned k)
{
    const char *lbl = NULL;
    if (k == K_L) lbl = "L";
    else if (k == K_R) lbl = "R";
    else if (k == K_START) lbl = "START";
    else if (k == K_SELECT) lbl = "SELECT";
    if (lbl) {
        int w = text_w(&font_small, lbl, 1) + 6;
        gfx_rect(x + 1, y + 1, w - 2, 9, P_DARK_SLATE);
        gfx_rect(x, y + 2, w, 7, P_DARK_SLATE);
        text(&font_small, x + 3, y + 1, lbl, P_LIGHT_GREY, 1);
        return;
    }
    static const char *const disc[11] = {"...sssss...", ".sKKKKKKKs.", ".sKKKKKKKs.", "sKKKKKKKKKs", "sKKKKKKKKKs", "sKKKKKKKKKs",
                                         "sKKKKKKKKKs", "sKKKKKKKKKs", ".sKKKKKKKs.", ".sKKKKKKKs.", "...sssss..."};
    sprite(x, y, disc, 11, 1, 0);
    int cx = x + 5, cy = y + 5;
    if (k == K_X) {
        for (int i = -2; i <= 2; i++) {
            gfx_pixel(cx + i, cy + i, P_SKY);
            gfx_pixel(cx + i, cy - i, P_SKY);
        }
    } else if (k == K_O) {
        static const int ring[8][2] = {{-1, -2}, {0, -2}, {1, -2}, {-2, -1}, {2, -1}, {-2, 0}, {2, 0}, {-2, 1}};
        for (int i = 0; i < 8; i++) gfx_pixel(cx + ring[i][0], cy + ring[i][1], P_RED);
        gfx_pixel(cx + 2, cy + 1, P_RED);
        gfx_rect(cx - 1, cy + 2, 3, 1, P_RED);
    } else if (k == K_TRI) {
        gfx_pixel(cx, cy - 2, P_GREEN);
        gfx_pixel(cx - 1, cy - 1, P_GREEN); gfx_pixel(cx + 1, cy - 1, P_GREEN);
        gfx_pixel(cx - 1, cy, P_GREEN); gfx_pixel(cx + 1, cy, P_GREEN);
        gfx_pixel(cx - 2, cy + 1, P_GREEN); gfx_pixel(cx + 2, cy + 1, P_GREEN);
        gfx_rect(cx - 2, cy + 2, 5, 1, P_GREEN);
    } else if (k == K_SQ) {
        gfx_rect_outline(cx - 2, cy - 2, 5, 5, P_PINK);
    } else if (k == (K_UP | K_DOWN) || k == (K_LEFT | K_RIGHT) || k == (K_UP | K_DOWN | K_LEFT | K_RIGHT)) {
        gfx_rect(cx - 1, cy - 3, 3, 7, P_SLATE);
        gfx_rect(cx - 3, cy - 1, 7, 3, P_SLATE);
        if (k & K_UP) { gfx_pixel(cx, cy - 3, P_WHITE); gfx_pixel(cx, cy + 3, P_WHITE); }
        if (k & K_LEFT) { gfx_pixel(cx - 3, cy, P_WHITE); gfx_pixel(cx + 3, cy, P_WHITE); }
    }
}

static int hint_x;
static void hint(unsigned k, const char *label)
{
    btn(hint_x, 258, k);
    int bw = 11;
    if (k == K_L || k == K_R || k == K_START || k == K_SELECT) {
        const char *l = k == K_L ? "L" : k == K_R ? "R" : k == K_START ? "START" : "SELECT";
        bw = text_w(&font_small, l, 1) + 6;
    }
    text(&font_small, hint_x + bw + 3, 259, label, P_LIGHT_GREY, 1);
    hint_x += bw + 3 + text_w(&font_small, label, 1) + 9;
}
static void hints_begin(void)
{
    gfx_rect_alpha(0, 254, SCR_W, 18, P_NIGHT, 225);
    gfx_rect(0, 254, SCR_W, 1, P_DARK_SLATE);
    hint_x = 8;
}

/* window with a pixel border and a title tab */
static void panel(int x, int y, int w, int h, const char *title, int alpha)
{
    gfx_rect_alpha(x + 1, y + 1, w - 2, h - 2, P_NIGHT, alpha);
    gfx_rect(x + 2, y, w - 4, 1, P_SLATE);
    gfx_rect(x + 2, y + h - 1, w - 4, 1, P_DARK_SLATE);
    gfx_rect(x, y + 2, 1, h - 4, P_DARK_SLATE);
    gfx_rect(x + w - 1, y + 2, 1, h - 4, P_DARK_SLATE);
    gfx_pixel(x + 1, y + 1, P_SLATE);
    gfx_pixel(x + w - 2, y + 1, P_SLATE);
    gfx_pixel(x + 1, y + h - 2, P_DARK_SLATE);
    gfx_pixel(x + w - 2, y + h - 2, P_DARK_SLATE);
    if (title) {
        int tw = text_w(&font_title, title, 1) + 10;
        gfx_rect(x + 6, y - 5, tw, 11, P_DARK_SLATE);
        gfx_rect(x + 7, y - 6, tw - 2, 1, P_SLATE);
        text(&font_title, x + 11, y - 3, title, P_GOLD, 1);
    }
}

static void darken(void)
{
    uint32_t *fb = gfx_target();
    for (int y = 0; y < SCR_H; y++) {
        uint32_t *p = fb + y * BUF_W;
        for (int x = 0; x < SCR_W; x++) p[x] = ((p[x] >> 1) & 0x7F7F7F) | 0xFF000000;
    }
}

static void bar(int x, int y, int w, int h, float p, uint32_t col)
{
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    gfx_rect(x - 1, y - 1, w + 2, h + 2, P_NIGHT);
    gfx_rect(x, y, w, h, P_NAVY);
    int f = (int)(w * p + 0.5f);
    gfx_rect(x, y, f, h, col);
    if (f > 0) gfx_rect(x, y, f, 1, gfx_mix(col, P_WHITE, 90));
    for (int sx = x + 6; sx < x + w; sx += 6) gfx_rect(sx, y, 1, h, P_NIGHT); /* segments */
}

/* ------------------------------------------------------------------ */
/* scene                                                               */

static void draw_scene(int sel)
{
    SceneState s;
    memset(&s, 0, sizeof s);
    s.n = st.n;
    for (int i = 0; i < st.n && i < SCENE_MAX_MODULES; i++) {
        const Goal *g = &st.goals[i];
        s.m[i].color = g->color;
        s.m[i].style = g->style;
        s.m[i].power = goal_power(g, today, &now);
        s.m[i].alert = goal_alert(g, today, &now);
        s.m[i].complete = goal_complete(g, &now);
    }
    s.sel = sel;
    s.level = cur_level;
    s.t = T;
    s.hour = now.h;
    s.minute = now.mi;
    uint32_t *screen = gfx_target();
    scene_draw(lowres, &s);
    gfx_set_target(screen);
    scene_blit2x(screen, lowres);
}

/* ------------------------------------------------------------------ */
/* shared bits of goal info                                            */

static void goal_status(const Goal *g, char *b, int n)
{
    switch (g->type) {
    case G_HABIT: snprintf(b, n, "%d", streak_current(g, today)); break;
    case G_WEEKLY: snprintf(b, n, "%d/%d", week_count(g, today), g->target); break;
    case G_COUNTER: snprintf(b, n, "%d/%d", counter_total(g), g->target); break;
    case G_MILESTONE: snprintf(b, n, "%d/%d", popcount((unsigned)g->steps_done), g->nsteps); break;
    case G_COUNTDOWN: {
        long left = dt_minutes(&g->deadline) - dt_minutes(&now);
        if (g->done) snprintf(b, n, "done");
        else if (left <= 0) snprintf(b, n, "now!");
        else if (left >= 1440) snprintf(b, n, "%ldd", left / 1440);
        else snprintf(b, n, "%ldh", left / 60 > 0 ? left / 60 : 1);
        break;
    }
    }
}

static void schedule_text(const Goal *g, char *b, int n)
{
    static const char *d1[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    switch (g->type) {
    case G_HABIT: {
        int m = g->days ? g->days : 0x7F;
        if (m == 0x7F) snprintf(b, n, "Every day");
        else if (m == 0x1F) snprintf(b, n, "Weekdays");
        else if (m == 0x60) snprintf(b, n, "Weekends");
        else {
            b[0] = 0;
            for (int i = 0; i < 7; i++)
                if (m & (1 << i)) {
                    if (b[0]) strncat(b, " ", n - strlen(b) - 1);
                    strncat(b, d1[i], n - strlen(b) - 1);
                }
        }
        break;
    }
    case G_WEEKLY: snprintf(b, n, "%d times a week", g->target); break;
    case G_COUNTER: snprintf(b, n, "Target %d %s", g->target, g->unit); break;
    case G_MILESTONE: snprintf(b, n, "%d steps", g->nsteps); break;
    case G_COUNTDOWN: fmt_datetime(b, n, &g->deadline, st.s.clock24); break;
    }
}

/* the main action for a goal from the home screen / detail */
static void quick_action(int gi)
{
    Goal *g = &st.goals[gi];
    if (g->done) { toast("This goal is complete"); snd_play(SND_ERR); return; }
    switch (g->type) {
    case G_HABIT:
    case G_WEEKLY:
        if (log_get(g, today) > 0) {
            log_set(g, today, 0);
            snd_play(SND_UNCHECK);
        } else {
            log_set(g, today, 1);
            snd_play(SND_CHECK);
            if (gi < SCENE_MAX_MODULES) scene_burst(gi, gcol(g));
        }
        changed(gi);
        break;
    case G_COUNTER: {
        int a = amounts[amount_i];
        log_set(g, today, log_get(g, today) + a);
        snd_play(SND_CHECK);
        if (gi < SCENE_MAX_MODULES) scene_burst(gi, gcol(g));
        changed(gi);
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* overlays: keyboard                                                  */

static const char *kb_rows[2][4] = {
    {"1234567890-", "qwertyuiop'", "asdfghjkl:/", "zxcvbnm,.?!"},
    {"#@&%()+=*_\"", "QWERTYUIOP;", "ASDFGHJKL<>", "ZXCVBNM[]$^"}};

static void open_keyboard(const char *title, char *target, int maxlen, int then)
{
    strncpy(kb_title, title, sizeof kb_title - 1);
    kb_target = target;
    kb_max = maxlen;
    strncpy(kb_buf, target ? target : "", sizeof kb_buf - 1);
    kb_buf[kb_max] = 0;
    kb_row = 1;
    kb_col = 0;
    kb_shift = kb_buf[0] == 0; /* start with a capital letter */
    kb_then = then;
    overlay = OV_KEYBOARD;
    snd_play(SND_OK);
}

static void kb_type(char c)
{
    int n = (int)strlen(kb_buf);
    if (n >= kb_max) { snd_play(SND_ERR); return; }
    kb_buf[n] = c;
    kb_buf[n + 1] = 0;
    snd_play(SND_TYPE);
    if (kb_shift == 1 && c >= 'A' && c <= 'Z') kb_shift = 0; /* one capital, then lower case */
}

static void kb_done(void)
{
    if (kb_target) {
        strncpy(kb_target, kb_buf, kb_max);
        kb_target[kb_max] = 0;
    }
    overlay = OV_NONE;
    ed_changed = 1;
    snd_play(SND_OK);
    if (kb_then == 1 && kb_buf[0] && eg.nsteps < MAX_STEPS) eg.nsteps++; /* new step was typed */
}

static void kb_input(void)
{
    /* bottom row (row 4): SHIFT, SPACE, DEL, DONE */
    if (rp(K_UP)) { kb_row = (kb_row + 4) % 5; snd_play(SND_MOVE); }
    if (rp(K_DOWN)) { kb_row = (kb_row + 1) % 5; snd_play(SND_MOVE); }
    int cols = kb_row == 4 ? 4 : 11;
    if (kb_row == 4 && kb_col > 3) kb_col = 3;
    if (rp(K_LEFT)) { kb_col = (kb_col + cols - 1) % cols; snd_play(SND_MOVE); }
    if (rp(K_RIGHT)) { kb_col = (kb_col + 1) % cols; snd_play(SND_MOVE); }
    if (pr(K_L) || pr(K_R)) kb_shift = kb_shift ? 0 : 2;
    if (rp(K_SQ)) {
        int n = (int)strlen(kb_buf);
        if (n > 0) { kb_buf[n - 1] = 0; snd_play(SND_TYPE); }
    }
    if (rp(K_TRI)) kb_type(' ');
    if (pr(K_START)) { kb_done(); return; }
    if (pr(K_O)) {
        overlay = OV_NONE;
        if (kb_then == 1) eg.steps[eg.nsteps][0] = 0;
        snd_play(SND_BACK);
        return;
    }
    if (rp(K_X)) {
        if (kb_row < 4) kb_type(kb_rows[kb_shift ? 1 : 0][kb_row][kb_col]);
        else if (kb_col == 0) kb_shift = kb_shift ? 0 : 2;
        else if (kb_col == 1) kb_type(' ');
        else if (kb_col == 2) {
            int n = (int)strlen(kb_buf);
            if (n > 0) kb_buf[n - 1] = 0;
            snd_play(SND_TYPE);
        } else kb_done();
    }
}

static void kb_draw(void)
{
    int w = 352, h = 176, x = (SCR_W - w) / 2, y = 52;
    panel(x, y, w, h, kb_title, 245);
    /* text box */
    gfx_rect(x + 12, y + 14, w - 24, 22, P_NAVY);
    gfx_rect(x + 12, y + 35, w - 24, 1, P_SLATE);
    text_fit(&font_body, x + 18, y + 17, kb_buf, P_WHITE, 1, w - 50);
    int cw = text_w(&font_body, kb_buf, 1);
    if (cw > w - 50) cw = w - 50;
    if (((int)(T * 2.5f)) % 2 == 0) gfx_rect(x + 19 + cw, y + 18, 2, 14, P_YELLOW);
    char cnt[16];
    snprintf(cnt, sizeof cnt, "%d/%d", (int)strlen(kb_buf), kb_max);
    text_right(&font_small, x + w - 16, y + 39, cnt, P_SLATE, 1);
    /* keys */
    int kx0 = x + 16, ky0 = y + 50, kw = 28, kh = 21;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 11; c++) {
            int kx = kx0 + c * (kw + 1), ky = ky0 + r * (kh + 1);
            int sel = kb_row == r && kb_col == c;
            gfx_rect(kx, ky, kw, kh, sel ? P_GOLD : P_DARK_SLATE);
            gfx_rect(kx, ky + kh - 2, kw, 2, sel ? P_ORANGE : P_NAVY);
            char s[2] = {kb_rows[kb_shift ? 1 : 0][r][c], 0};
            text_center(&font_body, kx + kw / 2, ky + 2, s, sel ? P_NIGHT : P_SAND, 1);
        }
    static const char *special[4] = {"SHIFT", "SPACE", "DEL", "DONE"};
    static const int sw[4] = {70, 120, 60, 66};
    int sx = kx0;
    for (int c = 0; c < 4; c++) {
        int ky = ky0 + 4 * (kh + 1);
        int sel = kb_row == 4 && kb_col == c;
        uint32_t base = c == 3 ? P_MID_GREEN : (c == 0 && kb_shift ? P_SKY : P_DARK_SLATE);
        gfx_rect(sx, ky, sw[c], kh, sel ? P_GOLD : base);
        gfx_rect(sx, ky + kh - 2, sw[c], 2, sel ? P_ORANGE : P_NAVY);
        text_center(&font_small, sx + sw[c] / 2, ky + 6, special[c], sel ? P_NIGHT : P_WHITE, 1);
        sx += sw[c] + 4;
    }
    hints_begin();
    hint(K_X, "Type");
    hint(K_SQ, "Delete");
    hint(K_TRI, "Space");
    hint(K_L, "Shift");
    hint(K_START, "Done");
    hint(K_O, "Cancel");
}

/* ------------------------------------------------------------------ */
/* overlays: date picker                                               */

static void open_date(int target, const DateTime *t, int with_time)
{
    dp_v[0] = t->d;
    dp_v[1] = t->mo;
    dp_v[2] = t->y;
    dp_v[3] = t->h;
    dp_v[4] = t->mi;
    dp_time = with_time;
    dp_field = 0;
    dp_target = target;
    overlay = OV_DATE;
    snd_play(SND_OK);
}

static void dp_fix(void)
{
    if (dp_v[1] < 1) dp_v[1] = 12;
    if (dp_v[1] > 12) dp_v[1] = 1;
    if (dp_v[2] < 2000) dp_v[2] = 2000;
    if (dp_v[2] > 2099) dp_v[2] = 2099;
    int dim = days_in_month(dp_v[2], dp_v[1]);
    if (dp_v[0] < 1) dp_v[0] = dim;
    if (dp_v[0] > dim) dp_v[0] = dp_v[0] == dim + 1 ? 1 : dim;
    dp_v[3] = (dp_v[3] + 24) % 24;
    dp_v[4] = (dp_v[4] + 60) % 60;
}

static void dp_input(void)
{
    int nf = dp_time ? 5 : 3;
    if (rp(K_LEFT)) { dp_field = (dp_field + nf - 1) % nf; snd_play(SND_MOVE); }
    if (rp(K_RIGHT)) { dp_field = (dp_field + 1) % nf; snd_play(SND_MOVE); }
    int step = (dp_field == 4) ? 5 : 1;
    if (keys & (K_L | K_R)) step = dp_field == 4 ? 15 : (dp_field == 2 ? 10 : (dp_field == 0 ? 7 : 3));
    if (rp(K_UP)) {
        if (dp_field == 4) dp_v[4] = (dp_v[4] / step) * step + step;
        else dp_v[dp_field] += step;
        dp_fix();
        snd_play(SND_MOVE);
    }
    if (rp(K_DOWN)) {
        if (dp_field == 4) dp_v[4] = ((dp_v[4] + step - 1) / step) * step - step;
        else dp_v[dp_field] -= step;
        dp_fix();
        snd_play(SND_MOVE);
    }
    if (pr(K_TRI)) { /* jump to today */
        dp_v[0] = now.d; dp_v[1] = now.mo; dp_v[2] = now.y;
        snd_play(SND_MOVE);
    }
    if (pr(K_X) || pr(K_START)) {
        DateTime t = {dp_v[2], dp_v[1], dp_v[0], dp_v[3], dp_v[4]};
        if (dp_target == 1) eg.start_day = day_from_ymd(t.y, t.mo, t.d);
        else {
            eg.deadline = t;
            eg.has_deadline = 1;
        }
        ed_changed = 1;
        overlay = OV_NONE;
        snd_play(SND_OK);
    }
    if (pr(K_O)) { overlay = OV_NONE; snd_play(SND_BACK); }
}

static void dp_draw(void)
{
    int w = 340, h = 150, x = (SCR_W - w) / 2, y = 62;
    panel(x, y, w, h, dp_target == 1 ? "START DATE" : dp_target == 3 ? "EVENT DATE & TIME" : "DEADLINE", 245);
    char f[5][8];
    snprintf(f[0], 8, "%02d", dp_v[0]);
    snprintf(f[1], 8, "%s", month_short(dp_v[1]));
    snprintf(f[2], 8, "%d", dp_v[2]);
    snprintf(f[3], 8, "%02d", dp_v[3]);
    snprintf(f[4], 8, "%02d", dp_v[4]);
    int nf = dp_time ? 5 : 3;
    int xs[5], ws[5], tot = 0;
    for (int i = 0; i < nf; i++) {
        ws[i] = text_w(&font_big, f[i], 1) + 16;
        tot += ws[i] + (i == 3 ? 24 : 6);
    }
    int cx = x + (w - tot) / 2;
    for (int i = 0; i < nf; i++) {
        if (i == 3) {
            cx += 18;
            text(&font_big, cx - 12, y + 30, ":", P_SLATE, 1);
        }
        xs[i] = cx;
        int sel = dp_field == i;
        gfx_rect(cx, y + 26, ws[i], 30, sel ? P_DARK_SLATE : P_NAVY);
        if (sel) {
            gfx_rect(cx, y + 54, ws[i], 2, P_GOLD);
            /* up/down arrows */
            int ax = cx + ws[i] / 2;
            for (int k = 0; k < 3; k++) {
                gfx_rect(ax - k, y + 18 + k, k * 2 + 1, 1, P_GOLD);
                gfx_rect(ax - k, y + 64 - k, k * 2 + 1, 1, P_GOLD);
            }
        }
        text_center(&font_big, cx + ws[i] / 2, y + 29, f[i], sel ? P_YELLOW : P_SAND, 1);
        cx += ws[i] + 6;
    }
    (void)xs;
    /* weekday + relative */
    int day = day_from_ymd(dp_v[2], dp_v[1], dp_v[0]);
    int diff = day - today;
    char info[64];
    if (diff == 0) snprintf(info, sizeof info, "%s  -  today", weekday_short(weekday(day)));
    else if (diff > 0) snprintf(info, sizeof info, "%s  -  in %d day%s", weekday_short(weekday(day)), diff, diff == 1 ? "" : "s");
    else snprintf(info, sizeof info, "%s  -  %d day%s ago", weekday_short(weekday(day)), -diff, diff == -1 ? "" : "s");
    text_center(&font_body, x + w / 2, y + 80, info, P_CYAN, 1);
    text_center(&font_small, x + w / 2, y + 106, "HOLD L OR R TO CHANGE FASTER", P_SLATE, 1);
    hints_begin();
    hint(K_LEFT | K_RIGHT, "Field");
    hint(K_UP | K_DOWN, "Change");
    hint(K_TRI, "Today");
    hint(K_X, "OK");
    hint(K_O, "Cancel");
}

/* ------------------------------------------------------------------ */
/* overlays: confirm + menu                                            */

static void confirm(const char *m1, const char *m2, int action, int param)
{
    strncpy(cf_msg, m1, sizeof cf_msg - 1);
    strncpy(cf_msg2, m2 ? m2 : "", sizeof cf_msg2 - 1);
    cf_action = action;
    cf_param = param;
    overlay = OV_CONFIRM;
    snd_play(SND_ERR);
}

static void open_editor(int index, int type);

static void cf_input(void)
{
    if (pr(K_O)) { overlay = OV_NONE; snd_play(SND_BACK); return; }
    if (!pr(K_X)) return;
    overlay = OV_NONE;
    switch (cf_action) {
    case ACT_DELETE_GOAL: {
        char m[80];
        snprintf(m, sizeof m, "Removed \"%s\"", st.goals[cf_param].name);
        station_delete(&st, cf_param);
        memmove(&was_complete[cf_param], &was_complete[cf_param + 1], (MAX_GOALS - cf_param - 1) * sizeof(int));
        dirty = 1;
        recompute(1, -1);
        toast(m);
        snd_play(SND_BACK);
        if (gl_sel >= st.n) gl_sel = st.n > 0 ? st.n - 1 : 0;
        screen = SCR_GOALS;
        break;
    }
    case ACT_DELETE_STEP:
        if (cf_param < eg.nsteps) {
            memmove(eg.steps[cf_param], eg.steps[cf_param + 1], (eg.nsteps - cf_param - 1) * NAME_LEN);
            /* shift done bits down */
            unsigned lo = (unsigned)eg.steps_done & ((1u << cf_param) - 1), hi = ((unsigned)eg.steps_done >> (cf_param + 1)) << cf_param;
            eg.steps_done = (int)(lo | hi);
            eg.nsteps--;
            if (st_sel >= eg.nsteps && st_sel > 0) st_sel--;
            ed_changed = 1;
        }
        snd_play(SND_BACK);
        break;
    case ACT_DISCARD:
        screen = eg_index >= 0 ? SCR_DETAIL : SCR_HOME;
        snd_play(SND_BACK);
        break;
    case ACT_COUNTDOWN_DONE: {
        Goal *g = &st.goals[cf_param];
        g->done = !g->done;
        g->done_day = today;
        changed(cf_param);
        snd_play(g->done ? SND_CHECK : SND_UNCHECK);
        break;
    }
    }
}

static void cf_draw(void)
{
    int w = 320, h = 96, x = (SCR_W - w) / 2, y = 84;
    panel(x, y, w, h, "CONFIRM", 245);
    gfx_rect(x + 1, y + 1, w - 2, 2, P_RED);
    text_center(&font_body, x + w / 2, y + 18, cf_msg, P_WHITE, 1);
    text_center(&font_body, x + w / 2, y + 38, cf_msg2, P_GOLD, 1);
    int bx = x + w / 2 - 70;
    btn(bx, y + 68, K_X);
    text(&font_small, bx + 15, y + 69, "YES", P_WHITE, 1);
    btn(bx + 80, y + 68, K_O);
    text(&font_small, bx + 95, y + 69, "NO", P_WHITE, 1);
}

static const char *menu_items[5] = {"New goal", "All goals", "XP & level settings", "How it works", "Close"};

static void menu_input(void)
{
    if (rp(K_UP)) { menu_sel = (menu_sel + 4) % 5; snd_play(SND_MOVE); }
    if (rp(K_DOWN)) { menu_sel = (menu_sel + 1) % 5; snd_play(SND_MOVE); }
    if (pr(K_O) || pr(K_START)) { overlay = OV_NONE; snd_play(SND_BACK); return; }
    if (pr(K_X)) {
        overlay = OV_NONE;
        snd_play(SND_OK);
        switch (menu_sel) {
        case 0:
            if (st.n >= MAX_GOALS) toast("The coast is full (32 lighthouses)");
            else open_editor(-1, G_HABIT);
            break;
        case 1: screen = SCR_GOALS; gl_moving = 0; break;
        case 2: screen = SCR_SETTINGS; set_sel = 0; break;
        case 3: screen = SCR_HELP; break;
        }
    }
}

static void menu_draw(void)
{
    int w = 210, h = 118, x = (SCR_W - w) / 2, y = 70;
    panel(x, y, w, h, "MENU", 245);
    for (int i = 0; i < 5; i++) {
        int ry = y + 12 + i * 20;
        if (i == menu_sel) {
            gfx_rect(x + 8, ry - 1, w - 16, 18, P_DARK_SLATE);
            sprite(x + 12, ry + 2, SPR_STAR, 7, 1, 0);
        }
        text(&font_body, x + 26, ry, menu_items[i], i == menu_sel ? P_YELLOW : P_SAND, 1);
    }
    hints_begin();
    hint(K_UP | K_DOWN, "Choose");
    hint(K_X, "Open");
    hint(K_O, "Close");
}

/* ------------------------------------------------------------------ */
/* HUD                                                                 */

static void hud(void)
{
    gfx_rect_alpha(0, 0, SCR_W, 24, P_NIGHT, 215);
    gfx_rect(0, 24, SCR_W, 1, P_DARK_SLATE);
    /* level badge */
    gfx_rect(4, 3, 40, 18, P_DARK_SLATE);
    gfx_rect(5, 2, 38, 1, P_SLATE);
    text(&font_small, 8, 6, "LV", P_GOLD, 1);
    char b[48];
    snprintf(b, sizeof b, "%d", cur_level);
    text(&font_big, 22, 0, b, P_YELLOW, 1);
    /* xp bar */
    float p = xp_need > 0 ? (float)xp_into / xp_need : 0;
    bar(52, 9, 132, 7, p, P_GOLD);
    snprintf(b, sizeof b, "%d / %d XP", xp_into, xp_need);
    text(&font_small, 192, 8, b, P_LIGHT_GREY, 1);
    /* clock */
    if (st.s.clock24) snprintf(b, sizeof b, "%02d:%02d", now.h, now.mi);
    else snprintf(b, sizeof b, "%d:%02d %s", now.h % 12 ? now.h % 12 : 12, now.mi, now.h < 12 ? "AM" : "PM");
    int tx = SCR_W - 8 - text_w(&font_body, b, 1);
    text(&font_body, tx, 4, b, P_WHITE, 1);
    char d[32];
    snprintf(d, sizeof d, "%s %02d %s", weekday_short(weekday(today)), now.d, month_short(now.mo));
    int dx = tx - 10 - text_w(&font_small, d, 1);
    text(&font_small, dx, 8, d, P_GREY, 1);
    if (battery >= 0) {
        int bx = dx - 30;
        gfx_rect_outline(bx, 7, 18, 9, P_SLATE);
        gfx_rect(bx + 18, 9, 2, 5, P_SLATE);
        gfx_rect(bx + 2, 9, 14 * battery / 100, 5, battery <= 15 ? P_RED : battery <= 35 ? P_GOLD : P_GREEN);
    }
}

static void draw_effects_ui(void)
{
    for (int k = 0; k < NFLOAT; k++)
        if (floaters[k].life > 0) {
            float a = floaters[k].life > 0.4f ? 1 : floaters[k].life / 0.4f;
            int y = (int)floaters[k].y;
            uint32_t c = gfx_mix(P_NIGHT, floaters[k].c, (int)(255 * a));
            text_center(&font_title, (int)floaters[k].x + 1, y + 1, floaters[k].txt, P_NIGHT, 1);
            text_center(&font_title, (int)floaters[k].x, y, floaters[k].txt, c, 1);
        }
    if (toast_t > 0) {
        float a = toast_t > 0.4f ? 1 : toast_t / 0.4f;
        int w = text_w(&font_body, toast_msg, 1) + 28;
        int x = (SCR_W - w) / 2, y = 226;
        gfx_rect_alpha(x, y, w, 22, P_NIGHT, (int)(235 * a));
        gfx_rect_alpha(x, y, w, 1, P_GOLD, (int)(255 * a));
        gfx_rect_alpha(x, y + 21, w, 1, P_GOLD, (int)(255 * a));
        text(&font_body, x + 14, y + 3, toast_msg, gfx_mix(P_NIGHT, P_WHITE, (int)(255 * a)), 1);
    }
    if (levelup_t > 0) {
        float p = 3.0f - levelup_t;
        int y = 86 + (p < 0.3f ? (int)((0.3f - p) * -200) : 0);
        int w = 280, x = (SCR_W - w) / 2;
        gfx_rect_alpha(x, y, w, 70, P_NIGHT, 235);
        gfx_rect(x, y, w, 2, P_GOLD);
        gfx_rect(x, y + 68, w, 2, P_GOLD);
        text_center(&font_title, SCR_W / 2 + 2, y + 12, "LEVEL UP!", P_DARK_BROWN, 2);
        text_center(&font_title, SCR_W / 2, y + 10, "LEVEL UP!", ((int)(T * 6) % 2) ? P_YELLOW : P_GOLD, 2);
        char b[48];
        snprintf(b, sizeof b, "Your harbor reached level %d", levelup_to);
        text_center(&font_body, SCR_W / 2, y + 40, b, P_WHITE, 1);
        for (int k = 0; k < 6; k++) {
            float a = T * 2 + k;
            sprite(SCR_W / 2 + (int)(cosf(a) * 150) - 3, y + 30 + (int)(sinf(a * 1.3f) * 30), SPR_STAR, 7, 1, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* HOME                                                                */

static void build_today(void)
{
    today_n = 0;
    for (int i = 0; i < st.n; i++)
        if (!goal_complete(&st.goals[i], &now)) today_list[today_n++] = i;
    if (home_sel >= today_n) home_sel = today_n > 0 ? today_n - 1 : 0;
}

static void home_input(void)
{
    if (pr(K_SELECT)) { hide_ui = !hide_ui; snd_play(SND_MOVE); }
    if (pr(K_START)) { overlay = OV_MENU; menu_sel = 0; snd_play(SND_OK); return; }
    if (pr(K_SQ)) { screen = SCR_GOALS; gl_moving = 0; snd_play(SND_OK); return; }
    if (today_n == 0) {
        if (pr(K_X)) {
            if (st.n == 0) open_editor(-1, G_HABIT);
            else { screen = SCR_GOALS; snd_play(SND_OK); }
        }
        return;
    }
    if (rp(K_DOWN)) { home_sel = (home_sel + 1) % today_n; snd_play(SND_MOVE); }
    if (rp(K_UP)) { home_sel = (home_sel + today_n - 1) % today_n; snd_play(SND_MOVE); }
    int gi = today_list[home_sel];
    if (pr(K_X)) {
        int t = st.goals[gi].type;
        if (t == G_HABIT || t == G_WEEKLY || t == G_COUNTER) { amount_i = 0; quick_action(gi); }
        else { det_i = gi; screen = SCR_DETAIL; cal_edit = 0; step_sel = 0; snd_play(SND_OK); }
    }
    if (pr(K_TRI)) { det_i = gi; screen = SCR_DETAIL; cal_edit = 0; step_sel = 0; amount_i = 0; snd_play(SND_OK); }
}

static void home_draw(void)
{
    int sel_mod = (today_n > 0 && !hide_ui) ? today_list[home_sel] : -1;
    draw_scene(sel_mod < SCENE_MAX_MODULES ? sel_mod : -1);
    if (hide_ui) {
        hints_begin();
        hint(K_SELECT, "Show panels");
        return;
    }
    hud();
    int px = 296, py = 34, pw = 178, ph = 214;
    panel(px, py, pw, ph, "TODAY", 205);
    if (st.n == 0) {
        SceneModule lh = {0, 0, 1.0f, 0, 0};
        scene_draw_module_at(px + pw / 2, py + 34, &lh, T, 2);
        text_center(&font_body, px + pw / 2, py + 66, "The coast is dark.", P_SAND, 1);
        text_center(&font_body, px + pw / 2, py + 90, "Press X to build", P_GREY, 1);
        text_center(&font_body, px + pw / 2, py + 106, "your first lighthouse.", P_GREY, 1);
    } else if (today_n == 0) {
        sprite(px + pw / 2 - 7, py + 30, SPR_STAR, 7, 2, 0);
        text_center(&font_body, px + pw / 2, py + 54, "Every goal", P_SAND, 1);
        text_center(&font_body, px + pw / 2, py + 70, "is complete!", P_SAND, 1);
        text_center(&font_body, px + pw / 2, py + 94, "START: new goal", P_GREY, 1);
    } else {
        int rows = 10, rh = 18;
        if (home_sel < home_scroll) home_scroll = home_sel;
        if (home_sel >= home_scroll + rows) home_scroll = home_sel - rows + 1;
        int done_today = 0, due = 0;
        for (int k = 0; k < today_n; k++) {
            const Goal *g = &st.goals[today_list[k]];
            if (g->type == G_HABIT && habit_scheduled(g, today)) { due++; done_today += log_get(g, today) > 0; }
            if (g->type == G_WEEKLY) { due++; done_today += log_get(g, today) > 0; }
        }
        for (int r = 0; r < rows && home_scroll + r < today_n; r++) {
            int k = home_scroll + r, gi = today_list[k];
            const Goal *g = &st.goals[gi];
            int ry = py + 10 + r * rh;
            int sel = k == home_sel;
            if (sel) {
                gfx_rect(px + 4, ry - 1, pw - 8, rh, P_DARK_SLATE);
                gfx_rect(px + 4, ry - 1, 2, rh, P_YELLOW);
            }
            gfx_rect(px + 10, ry + 5, 6, 6, gcol(g));
            gfx_rect(px + 10, ry + 10, 6, 1, gfx_shade(gcol(g), 60));
            int alert = goal_alert(g, today, &now);
            uint32_t nc = sel ? P_WHITE : (alert ? P_PINK : P_SAND);
            int rest = g->type == G_HABIT && !habit_scheduled(g, today);
            if (rest) nc = P_SLATE;
            /* right side status */
            int rx = px + pw - 10;
            char s[24];
            goal_status(g, s, sizeof s);
            if (g->type == G_HABIT || g->type == G_WEEKLY) {
                int on = log_get(g, today) > 0;
                if (!rest) sprite(rx - 9, ry + 3, on ? SPR_CHECK_ON : SPR_CHECK_OFF, 9, 1, 0);
                rx -= 14;
            }
            if (g->type == G_HABIT) {
                text_right(&font_body, rx, ry, s, P_GOLD, 1);
                rx -= text_w(&font_body, s, 1) + 2;
                sprite(rx - 5, ry + 4, SPR_FLAME, 7, 1, 0);
                rx -= 8;
            } else if (g->type == G_COUNTDOWN) {
                sprite(rx - 7, ry + 4, SPR_CLOCK, 7, 1, 0);
                rx -= 10;
                text_right(&font_body, rx, ry, s, P_CYAN, 1);
                rx -= text_w(&font_body, s, 1);
            } else {
                text_right(&font_body, rx, ry, s, P_CYAN, 1);
                rx -= text_w(&font_body, s, 1);
            }
            text_fit(&font_body, px + 21, ry, g->name, nc, 1, rx - (px + 21) - 4);
        }
        if (today_n > rows) {
            /* scroll bar */
            int bh = ph - 40, by = py + 10;
            gfx_rect(px + pw - 4, by, 2, bh, P_NAVY);
            gfx_rect(px + pw - 4, by + bh * home_scroll / today_n, 2, bh * rows / today_n, P_SLATE);
        }
        char f[48];
        snprintf(f, sizeof f, "TODAY  %d / %d CHECKED IN", done_today, due);
        gfx_rect(px + 6, py + ph - 22, pw - 12, 1, P_DARK_SLATE);
        text_center(&font_small, px + pw / 2, py + ph - 16, f, P_GREY, 1);
    }
    hints_begin();
    if (today_n > 0) {
        hint(K_UP | K_DOWN, "Choose");
        hint(K_X, "Check in");
        hint(K_TRI, "Details");
    } else if (st.n == 0) {
        hint(K_X, "New goal");
    }
    hint(K_SQ, "All goals");
    hint(K_START, "Menu");
    hint(K_SELECT, "Hide");
}

/* ------------------------------------------------------------------ */
/* GOALS list                                                          */

static void goals_input(void)
{
    if (gl_moving) {
        if (rp(K_UP) && gl_sel > 0) { station_move(&st, gl_sel, -1); int t = was_complete[gl_sel]; was_complete[gl_sel] = was_complete[gl_sel - 1]; was_complete[gl_sel - 1] = t; gl_sel--; dirty = 1; snd_play(SND_MOVE); }
        if (rp(K_DOWN) && gl_sel < st.n - 1) { station_move(&st, gl_sel, 1); int t = was_complete[gl_sel]; was_complete[gl_sel] = was_complete[gl_sel + 1]; was_complete[gl_sel + 1] = t; gl_sel++; dirty = 1; snd_play(SND_MOVE); }
        if (pr(K_X) || pr(K_SQ) || pr(K_O)) { gl_moving = 0; snd_play(SND_OK); }
        return;
    }
    if (pr(K_O)) { screen = SCR_HOME; snd_play(SND_BACK); return; }
    if (pr(K_START)) {
        if (st.n >= MAX_GOALS) toast("The coast is full (32 lighthouses)");
        else open_editor(-1, G_HABIT);
        return;
    }
    if (st.n == 0) return;
    if (rp(K_DOWN)) { gl_sel = (gl_sel + 1) % st.n; snd_play(SND_MOVE); }
    if (rp(K_UP)) { gl_sel = (gl_sel + st.n - 1) % st.n; snd_play(SND_MOVE); }
    if (pr(K_X)) { det_i = gl_sel; screen = SCR_DETAIL; cal_edit = 0; step_sel = 0; amount_i = 0; snd_play(SND_OK); }
    if (pr(K_TRI)) open_editor(gl_sel, st.goals[gl_sel].type);
    if (pr(K_SQ)) { gl_moving = 1; snd_play(SND_OK); }
    if (pr(K_R)) {
        char m[64];
        snprintf(m, sizeof m, "\"%s\"", st.goals[gl_sel].name);
        confirm("Delete this goal and its history?", m, ACT_DELETE_GOAL, gl_sel);
    }
}

static void goals_draw(void)
{
    draw_scene(-1);
    darken();
    hud();
    int px = 8, py = 36, pw = 464, ph = 212;
    char title[32];
    snprintf(title, sizeof title, "ALL GOALS  %d", st.n);
    panel(px, py, pw, ph, title, 230);
    if (st.n == 0) {
        text_center(&font_body, SCR_W / 2, py + 80, "No goals yet. Press START to add one.", P_SAND, 1);
    }
    int rows = 9, rh = 22;
    if (gl_sel < gl_scroll) gl_scroll = gl_sel;
    if (gl_sel >= gl_scroll + rows) gl_scroll = gl_sel - rows + 1;
    for (int r = 0; r < rows && gl_scroll + r < st.n; r++) {
        int i = gl_scroll + r;
        const Goal *g = &st.goals[i];
        int ry = py + 10 + r * rh;
        int sel = i == gl_sel;
        int off = (sel && gl_moving) ? 8 : 0;
        if (sel) {
            gfx_rect(px + 6 + off, ry - 2, pw - 12 - off, rh - 1, gl_moving ? P_PLUM : P_DARK_SLATE);
            gfx_rect(px + 6 + off, ry - 2, 2, rh - 1, gl_moving ? P_MAGENTA : P_YELLOW);
        }
        SceneModule m = {g->color, g->style, goal_power(g, today, &now), goal_alert(g, today, &now), goal_complete(g, &now)};
        gfx_clip_push(px + 6, ry - 2, px + 44 + off, ry + rh - 3);
        scene_draw_module_at(px + 24 + off, ry + 8, &m, T, 1);
        gfx_clip_pop();
        text_fit(&font_body, px + 44 + off, ry, g->name, sel ? P_WHITE : P_SAND, 1, 190);
        char sch[64];
        schedule_text(g, sch, sizeof sch);
        text(&font_small, px + 244, ry + 1, type_tags[g->type], P_GOLD, 1);
        text_fit(&font_small, px + 244, ry + 10, sch, P_GREY, 1, 130);
        char s[24];
        if (m.complete) snprintf(s, sizeof s, "DONE");
        else goal_status(g, s, sizeof s);
        if (g->type == G_HABIT && !m.complete) {
            sprite(px + pw - 62, ry + 4, SPR_FLAME, 7, 1, 0);
            text(&font_body, px + pw - 54, ry, s, P_GOLD, 1);
        } else text_right(&font_body, px + pw - 30, ry, s, m.complete ? P_GREEN : P_CYAN, 1);
        if (m.alert) text(&font_small, px + 382, ry + 5, "!", P_RED, 1);
    }
    if (gl_moving) text_right(&font_small, px + pw - 12, py + ph - 14, "MOVING - UP/DOWN, X TO DROP", P_MAGENTA, 1);
    hints_begin();
    if (gl_moving) {
        hint(K_UP | K_DOWN, "Move");
        hint(K_X, "Drop");
    } else {
        if (st.n > 0) {
            hint(K_X, "Open");
            hint(K_TRI, "Edit");
            hint(K_SQ, "Rearrange");
            hint(K_R, "Delete");
        }
        hint(K_START, "New");
        hint(K_O, "Back");
    }
}

/* ------------------------------------------------------------------ */
/* DETAIL                                                              */

#define HM_WEEKS 26

static int hm_first_day(void) { return today - weekday(today) - (HM_WEEKS - 1) * 7; }

static void detail_input(void)
{
    Goal *g = &st.goals[det_i];
    if (cal_edit) {
        int first = hm_first_day();
        if (rp(K_LEFT) && cal_day - 7 >= first) { cal_day -= 7; snd_play(SND_MOVE); }
        if (rp(K_RIGHT)) { cal_day = cal_day + 7 <= today ? cal_day + 7 : today; snd_play(SND_MOVE); }
        if (rp(K_UP) && cal_day - 1 >= first) { cal_day--; snd_play(SND_MOVE); }
        if (rp(K_DOWN) && cal_day + 1 <= today) { cal_day++; snd_play(SND_MOVE); }
        if (g->type == G_COUNTER) {
            if (rp(K_X)) { log_set(g, cal_day, log_get(g, cal_day) + amounts[amount_i]); changed(det_i); snd_play(SND_CHECK); }
            if (rp(K_TRI)) { log_set(g, cal_day, log_get(g, cal_day) - amounts[amount_i]); changed(det_i); snd_play(SND_UNCHECK); }
            if (pr(K_L)) amount_i = amount_i > 0 ? amount_i - 1 : 0;
            if (pr(K_R)) amount_i = amount_i < 6 ? amount_i + 1 : 6;
        } else if (pr(K_X)) {
            int on = log_get(g, cal_day) > 0;
            log_set(g, cal_day, on ? 0 : 1);
            changed(det_i);
            snd_play(on ? SND_UNCHECK : SND_CHECK);
        }
        if (pr(K_SQ) || pr(K_O)) { cal_edit = 0; snd_play(SND_BACK); }
        return;
    }
    if (pr(K_O)) { screen = SCR_HOME; snd_play(SND_BACK); return; }
    if (pr(K_TRI)) { open_editor(det_i, g->type); return; }
    switch (g->type) {
    case G_HABIT:
    case G_WEEKLY:
        if (pr(K_X)) quick_action(det_i);
        if (pr(K_SQ)) { cal_edit = 1; cal_day = today; snd_play(SND_OK); }
        break;
    case G_COUNTER:
        if (rp(K_RIGHT)) { amount_i = amount_i < 6 ? amount_i + 1 : 6; snd_play(SND_MOVE); }
        if (rp(K_LEFT)) { amount_i = amount_i > 0 ? amount_i - 1 : 0; snd_play(SND_MOVE); }
        if (rp(K_X)) quick_action(det_i);
        if (pr(K_SQ)) { cal_edit = 1; cal_day = today; snd_play(SND_OK); }
        break;
    case G_MILESTONE:
        if (g->nsteps > 0) {
            if (rp(K_DOWN)) { step_sel = (step_sel + 1) % g->nsteps; snd_play(SND_MOVE); }
            if (rp(K_UP)) { step_sel = (step_sel + g->nsteps - 1) % g->nsteps; snd_play(SND_MOVE); }
            if (pr(K_X)) {
                g->steps_done ^= 1 << step_sel;
                int on = (g->steps_done >> step_sel) & 1;
                if (on && det_i < SCENE_MAX_MODULES) scene_burst(det_i, gcol(g));
                changed(det_i);
                snd_play(on ? SND_CHECK : SND_UNCHECK);
            }
        }
        break;
    case G_COUNTDOWN:
        if (pr(K_X)) confirm(g->done ? "Mark this as not done?" : "Mark this as done?", g->name, ACT_COUNTDOWN_DONE, det_i);
        break;
    }
}

static void stat_card(int x, int y, int w, const char *label, const char *val, uint32_t col, const char *const *icon, int ih)
{
    gfx_rect(x, y, w, 36, P_NAVY);
    gfx_rect(x, y, w, 1, P_DARK_SLATE);
    text(&font_small, x + 6, y + 4, label, P_GREY, 1);
    int vx = x + 6;
    if (icon) {
        sprite(vx, y + 18, icon, ih, 1, 0);
        vx += 10;
    }
    text(&font_big, vx, y + 12, val, col, 1);
}

static void heatmap(const Goal *g, int x, int y)
{
    int first = hm_first_day();
    int maxv = 1;
    if (g->type == G_COUNTER)
        for (int i = 0; i < g->nlog; i++)
            if (g->log[i].val > maxv) maxv = g->log[i].val;
    static const char *wl[7] = {"M", "", "W", "", "F", "", "S"};
    for (int r = 0; r < 7; r++) text(&font_small, x - 10, y + r * 9, wl[r], P_SLATE, 1);
    int last_m = -1;
    for (int w = 0; w < HM_WEEKS; w++) {
        int wd0 = first + w * 7;
        int yy, mm, dd;
        ymd_from_day(wd0, &yy, &mm, &dd);
        if (mm != last_m && dd <= 7) {
            text(&font_small, x + w * 9, y - 10, month_short(mm), P_GREY, 1);
            last_m = mm;
        }
        for (int r = 0; r < 7; r++) {
            int d = wd0 + r;
            int cx = x + w * 9, cy = y + r * 9;
            uint32_t c;
            int v = log_get(g, d);
            if (d > today) c = 0;
            else if (v > 0) {
                c = gcol(g);
                if (g->type == G_COUNTER) {
                    int lvl = v * 3 / maxv;
                    c = lvl >= 2 ? c : lvl == 1 ? gfx_shade(c, 70) : gfx_shade(c, 45);
                }
            } else if (d < g->start_day) c = gfx_shade(P_NAVY, 55);
            else if (g->type == G_HABIT && habit_scheduled(g, d) && d < today) c = P_DARK_RED;
            else c = P_NAVY;
            if (c) gfx_rect(cx, cy, 8, 8, c);
            if (c && v > 0) gfx_rect(cx, cy, 8, 1, gfx_mix(c, P_WHITE, 70));
            if (d == today) gfx_rect_outline(cx - 1, cy - 1, 10, 10, P_WHITE);
            if (cal_edit && d == cal_day && ((int)(T * 4) % 2 == 0)) gfx_rect_outline(cx - 1, cy - 1, 10, 10, P_YELLOW);
        }
    }
}

static void detail_draw(void)
{
    Goal *g = &st.goals[det_i];
    draw_scene(det_i < SCENE_MAX_MODULES ? det_i : -1);
    darken();
    hud();
    int px = 8, py = 36, pw = 464, ph = 212;
    panel(px, py, pw, ph, type_tags[g->type], 232);
    /* header */
    SceneModule m = {g->color, g->style, goal_power(g, today, &now), goal_alert(g, today, &now), goal_complete(g, &now)};
    gfx_rect(px + 10, py + 12, 52, 48, P_NAVY);
    gfx_clip_push(px + 10, py + 12, px + 62, py + 60);
    scene_draw_module_at(px + 36, py + 34, &m, T, 2);
    gfx_clip_pop();
    text_fit(&font_title, px + 72, py + 14, g->name, P_SAND, 1, 260);
    char b[96];
    schedule_text(g, b, sizeof b);
    text_fit(&font_body, px + 72, py + 26, b, P_GREY, 1, 260);
    if (g->type != G_COUNTDOWN && g->has_deadline) {
        deadline_text(g, b, sizeof b);
        text(&font_small, px + 72, py + 46, "DEADLINE", P_GOLD, 1);
        text_fit(&font_small, px + 124, py + 46, b, dt_minutes(&g->deadline) < dt_minutes(&now) ? P_RED : P_LIGHT_GREY, 1, 220);
    } else {
        fmt_date(b, sizeof b, g->start_day);
        text(&font_small, px + 72, py + 46, "STARTED", P_GOLD, 1);
        text(&font_small, px + 124, py + 46, b, P_LIGHT_GREY, 1);
    }
    /* XP from this goal */
    snprintf(b, sizeof b, "%d", goal_xp(g, &st.s, today));
    text(&font_small, px + pw - 90, py + 14, "XP EARNED", P_GREY, 1);
    sprite(px + pw - 90, py + 30, SPR_STAR, 7, 1, 0);
    text(&font_big, px + pw - 80, py + 22, b, P_GOLD, 1);
    if (m.complete) {
        gfx_rect(px + pw - 92, py + 48, 80, 12, P_MID_GREEN);
        text_center(&font_small, px + pw - 52, py + 50, "COMPLETE", P_WHITE, 1);
    }

    int cy = py + 66;
    char v[32];
    switch (g->type) {
    case G_HABIT:
    case G_WEEKLY: {
        int wk = g->type == G_WEEKLY;
        snprintf(v, sizeof v, "%d", streak_current(g, today));
        stat_card(px + 10, cy, 106, wk ? "WEEKS IN A ROW" : "STREAK (DAYS)", v, P_GOLD, SPR_FLAME, 7);
        snprintf(v, sizeof v, "%d", streak_best(g, today));
        stat_card(px + 122, cy, 106, "BEST STREAK", v, P_YELLOW, NULL, 0);
        if (wk) snprintf(v, sizeof v, "%d / %d", week_count(g, today), g->target);
        else {
            /* % of scheduled days done in the last 30 */
            int den = 0, num = 0;
            for (int d = today - 29; d <= today; d++) {
                if (d < g->start_day || !habit_scheduled(g, d)) continue;
                if (d == today && log_get(g, d) <= 0) continue;
                den++;
                num += log_get(g, d) > 0;
            }
            snprintf(v, sizeof v, "%d%%", den ? num * 100 / den : 0);
        }
        stat_card(px + 234, cy, 106, wk ? "THIS WEEK" : "LAST 30 DAYS", v, P_CYAN, NULL, 0);
        int on = log_get(g, today) > 0;
        gfx_rect(px + 346, cy, 108, 36, on ? P_DARK_GREEN : P_NAVY);
        text(&font_small, px + 352, cy + 4, "TODAY", P_GREY, 1);
        sprite(px + 352, cy + 18, on ? SPR_CHECK_ON : SPR_CHECK_OFF, 9, 1, 0);
        text(&font_body, px + 366, cy + 15, on ? "Checked in" : (g->type == G_HABIT && !habit_scheduled(g, today) ? "Rest day" : "Not yet"), on ? P_GREEN : P_SAND, 1);
        heatmap(g, px + 60, cy + 58);
        if (cal_edit) {
            fmt_date(b, sizeof b, cal_day);
            int on2 = log_get(g, cal_day) > 0;
            char line[96];
            snprintf(line, sizeof line, "%s  -  %s", b, on2 ? "checked in" : "not checked in");
            text(&font_body, px + 60, cy + 124, line, P_YELLOW, 1);
        } else {
            text(&font_small, px + 60, cy + 128, "SQUARE: FIX PAST DAYS ON THE CALENDAR", P_SLATE, 1);
        }
        break;
    }
    case G_COUNTER: {
        int tot = counter_total(g);
        snprintf(v, sizeof v, "%d / %d", tot, g->target);
        gfx_rect(px + 10, cy, 444, 44, P_NAVY);
        text(&font_small, px + 16, cy + 4, "PROGRESS", P_GREY, 1);
        text(&font_big, px + 16, cy + 14, v, P_CYAN, 1);
        text(&font_body, px + 22 + text_w(&font_big, v, 1), cy + 18, g->unit, P_GREY, 1);
        bar(px + 200, cy + 20, 240, 10, g->target > 0 ? (float)tot / g->target : 0, gcol(g));
        snprintf(b, sizeof b, "%d%%", g->target > 0 ? tot * 100 / g->target : 0);
        text_right(&font_small, px + 440, cy + 4, b, P_GREY, 1);
        snprintf(b, sizeof b, "ADD: +%d", amounts[amount_i]);
        text(&font_small, px + 360, cy + 52, b, P_GOLD, 1);
        heatmap(g, px + 60, cy + 64);
        if (cal_edit) {
            fmt_date(b, sizeof b, cal_day);
            char line[96];
            snprintf(line, sizeof line, "%s  -  %d %s that day   (L/R: +/-%d)", b, log_get(g, cal_day), g->unit, amounts[amount_i]);
            text_fit(&font_body, px + 20, cy + 128, line, P_YELLOW, 1, 430);
        } else {
            snprintf(b, sizeof b, "Today: %d %s", log_get(g, today), g->unit);
            text(&font_body, px + 60, cy + 126, b, P_SAND, 1);
        }
        break;
    }
    case G_MILESTONE: {
        int dn = popcount((unsigned)g->steps_done);
        snprintf(v, sizeof v, "%d / %d", dn, g->nsteps);
        text(&font_small, px + 16, cy, "STEPS DONE", P_GREY, 1);
        text(&font_big, px + 90, cy - 6, v, P_CYAN, 1);
        bar(px + 180, cy + 2, 270, 8, g->nsteps ? (float)dn / g->nsteps : 0, gcol(g));
        if (g->nsteps == 0) text(&font_body, px + 16, cy + 30, "No steps yet. Press TRIANGLE to add them.", P_SAND, 1);
        int rows = 7, scroll = step_sel >= rows ? step_sel - rows + 1 : 0;
        for (int r = 0; r < rows && scroll + r < g->nsteps; r++) {
            int k = scroll + r, ry = cy + 20 + r * 18;
            int on = (g->steps_done >> k) & 1;
            if (k == step_sel) gfx_rect(px + 10, ry - 1, 444, 18, P_DARK_SLATE);
            sprite(px + 16, ry + 3, on ? SPR_CHECK_ON : SPR_CHECK_OFF, 9, 1, 0);
            text_fit(&font_body, px + 32, ry, g->steps[k], on ? P_GREY : P_SAND, 1, 400);
            if (on) gfx_rect(px + 32, ry + 8, text_w(&font_body, g->steps[k], 1) < 400 ? text_w(&font_body, g->steps[k], 1) : 400, 1, P_GREY);
        }
        break;
    }
    case G_COUNTDOWN: {
        long left = dt_minutes(&g->deadline) - dt_minutes(&now);
        gfx_rect(px + 10, cy, 444, 92, P_NAVY);
        if (g->done) {
            text_center(&font_title, SCR_W / 2, cy + 22, "DONE!", P_GREEN, 3);
        } else if (left <= 0) {
            text_center(&font_title, SCR_W / 2, cy + 22, "IT'S TIME!", P_YELLOW, 3);
        } else {
            fmt_dur(b, sizeof b, left);
            text_center(&font_big, SCR_W / 2, cy + 14, b, P_CYAN, 2);
        }
        fmt_datetime(b, sizeof b, &g->deadline, st.s.clock24);
        text_center(&font_body, SCR_W / 2, cy + 64, b, P_SAND, 1);
        bar(px + 20, cy + 104, 424, 8, goal_power(g, today, &now), gcol(g));
        fmt_date(b, sizeof b, g->start_day);
        char line[64];
        snprintf(line, sizeof line, "FROM %s", b);
        text(&font_small, px + 20, cy + 118, line, P_SLATE, 1);
        break;
    }
    }
    hints_begin();
    if (cal_edit) {
        hint(K_UP | K_DOWN | K_LEFT | K_RIGHT, "Pick day");
        if (g->type == G_COUNTER) { hint(K_X, "Add"); hint(K_TRI, "Subtract"); }
        else hint(K_X, "Toggle");
        hint(K_SQ, "Done");
    } else {
        switch (g->type) {
        case G_HABIT:
        case G_WEEKLY: hint(K_X, log_get(g, today) > 0 ? "Undo today" : "Check in"); hint(K_SQ, "Calendar"); break;
        case G_COUNTER: hint(K_X, "Add"); hint(K_LEFT | K_RIGHT, "Amount"); hint(K_SQ, "Calendar"); break;
        case G_MILESTONE: hint(K_UP | K_DOWN, "Step"); hint(K_X, "Tick"); break;
        case G_COUNTDOWN: hint(K_X, g->done ? "Not done" : "Mark done"); break;
        }
        hint(K_TRI, "Edit");
        hint(K_O, "Back");
    }
}

/* ------------------------------------------------------------------ */
/* EDITOR                                                              */

static void build_fields(void)
{
    nfields = 0;
    fields[nfields++] = F_NAME;
    if (eg_index < 0) fields[nfields++] = F_TYPE;
    if (eg.type == G_HABIT) fields[nfields++] = F_DAYS;
    if (eg.type == G_WEEKLY) fields[nfields++] = F_TIMES;
    if (eg.type == G_COUNTER) { fields[nfields++] = F_TARGET; fields[nfields++] = F_UNIT; }
    if (eg.type == G_MILESTONE) fields[nfields++] = F_STEPS;
    fields[nfields++] = F_START;
    fields[nfields++] = eg.type == G_COUNTDOWN ? F_EVENT : F_DEADLINE;
    if (eg.type != G_COUNTDOWN) fields[nfields++] = F_XP_EACH;
    if (eg.type != G_HABIT) fields[nfields++] = F_XP_BONUS;
    fields[nfields++] = F_COLOR;
    fields[nfields++] = F_STYLE;
    if (eg_index >= 0) { fields[nfields++] = F_STATUS; fields[nfields++] = F_DELETE; }
    fields[nfields++] = F_SAVE;
    if (ed_sel >= nfields) ed_sel = nfields - 1;
}

static void open_editor(int index, int type)
{
    eg_index = index;
    if (index >= 0) eg = st.goals[index];
    else {
        goal_defaults(&eg, type, today);
        eg.color = st.n % NUM_MODULE_COLORS;
        eg.style = st.n % NUM_MODULE_STYLES;
    }
    ed_sel = 0;
    ed_scroll = 0;
    ed_day = 0;
    ed_changed = 0;
    build_fields();
    screen = SCR_EDIT;
    snd_play(SND_OK);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void editor_save(void)
{
    if (!eg.name[0]) { toast("Give your goal a name first"); snd_play(SND_ERR); return; }
    if (eg.type == G_MILESTONE && eg.nsteps == 0) { toast("Add at least one step"); snd_play(SND_ERR); return; }
    if (eg.type == G_HABIT && (eg.days & 0x7F) == 0) { toast("Pick at least one day"); snd_play(SND_ERR); return; }
    if (eg_index < 0) {
        int i = station_add(&st, &eg);
        was_complete[i] = goal_complete(&eg, &now);
        if (i < SCENE_MAX_MODULES) { scene_burst(i, gcol(&eg)); scene_burst(i, P_WHITE); }
        char m[80];
        snprintf(m, sizeof m, "Lighthouse built: %s", eg.name);
        toast(m);
        snd_play(SND_LAUNCH);
        dirty = 1;
        recompute(0, i);
        screen = SCR_HOME;
        build_today();
        for (int k = 0; k < today_n; k++)
            if (today_list[k] == i) home_sel = k;
    } else {
        st.goals[eg_index] = eg;
        changed(eg_index);
        toast("Saved");
        snd_play(SND_OK);
        det_i = eg_index;
        screen = SCR_DETAIL;
    }
}

static void editor_input(void)
{
    if (rp(K_DOWN)) { ed_sel = (ed_sel + 1) % nfields; snd_play(SND_MOVE); }
    if (rp(K_UP)) { ed_sel = (ed_sel + nfields - 1) % nfields; snd_play(SND_MOVE); }
    if (pr(K_START)) { editor_save(); return; }
    if (pr(K_O)) {
        if (ed_changed) confirm("Discard your changes?", NULL, ACT_DISCARD, 0);
        else { screen = eg_index >= 0 ? SCR_DETAIL : SCR_HOME; snd_play(SND_BACK); }
        return;
    }
    int f = fields[ed_sel];
    int dir = rp(K_RIGHT) ? 1 : rp(K_LEFT) ? -1 : 0;
    int big = rp(K_R) ? 10 : rp(K_L) ? -10 : 0;
    int d = dir ? dir : big;
    switch (f) {
    case F_NAME:
        if (pr(K_X)) open_keyboard("GOAL NAME", eg.name, NAME_LEN - 1, 0);
        break;
    case F_TYPE:
        if (dir) {
            char name[NAME_LEN];
            int c = eg.color, s = eg.style;
            strcpy(name, eg.name);
            goal_defaults(&eg, (eg.type + dir + G_NTYPES) % G_NTYPES, today);
            strcpy(eg.name, name);
            eg.color = c;
            eg.style = s;
            build_fields();
            ed_changed = 1;
            snd_play(SND_MOVE);
        }
        break;
    case F_DAYS:
        if (dir) { ed_day = (ed_day + dir + 7) % 7; snd_play(SND_MOVE); }
        if (pr(K_X)) { eg.days ^= 1 << ed_day; ed_changed = 1; snd_play(SND_TYPE); }
        break;
    case F_TIMES:
        if (dir) { eg.target = clampi(eg.target + dir, 1, 7); ed_changed = 1; snd_play(SND_MOVE); }
        break;
    case F_TARGET:
        if (d) { eg.target = clampi(eg.target + d, 1, 100000); ed_changed = 1; snd_play(SND_MOVE); }
        break;
    case F_UNIT:
        if (pr(K_X)) open_keyboard("UNIT (E.G. KM, BOOKS)", eg.unit, UNIT_LEN - 1, 0);
        break;
    case F_STEPS:
        if (pr(K_X)) { screen = SCR_STEPS; st_sel = 0; st_moving = 0; snd_play(SND_OK); }
        break;
    case F_START:
        if (pr(K_X)) {
            DateTime t;
            dt_from_day(&t, eg.start_day, 0, 0);
            open_date(1, &t, 0);
        }
        break;
    case F_DEADLINE:
    case F_EVENT:
        if (pr(K_X)) {
            DateTime t = eg.deadline;
            if (!eg.has_deadline) dt_from_day(&t, today + 7, 23, 59);
            open_date(f == F_EVENT ? 3 : 2, &t, 1);
        }
        if (pr(K_TRI) && f == F_DEADLINE) { eg.has_deadline = 0; ed_changed = 1; snd_play(SND_BACK); }
        break;
    case F_XP_EACH:
        if (d) { eg.xp_each = clampi(eg.xp_each + d, 0, 100000); ed_changed = 1; snd_play(SND_MOVE); }
        break;
    case F_XP_BONUS:
        if (d) { eg.xp_bonus = clampi(eg.xp_bonus + d * 5, 0, 1000000); ed_changed = 1; snd_play(SND_MOVE); }
        break;
    case F_COLOR:
        if (dir) { eg.color = (eg.color + dir + NUM_MODULE_COLORS) % NUM_MODULE_COLORS; ed_changed = 1; snd_play(SND_MOVE); }
        break;
    case F_STYLE:
        if (dir) { eg.style = (eg.style + dir + NUM_MODULE_STYLES) % NUM_MODULE_STYLES; ed_changed = 1; snd_play(SND_MOVE); }
        break;
    case F_STATUS:
        if (dir || pr(K_X)) { eg.done = !eg.done; eg.done_day = today; ed_changed = 1; snd_play(SND_MOVE); }
        break;
    case F_DELETE:
        if (pr(K_X)) {
            char m[64];
            snprintf(m, sizeof m, "\"%s\"", st.goals[eg_index].name);
            confirm("Delete this goal and its history?", m, ACT_DELETE_GOAL, eg_index);
        }
        break;
    case F_SAVE:
        if (pr(K_X)) editor_save();
        break;
    }
}

static void lr_value(int x, int y, const char *v, int sel, uint32_t col)
{
    if (sel) {
        text(&font_body, x, y, "<", P_GOLD, 1);
        text(&font_body, x + 14, y, v, col, 1);
        text(&font_body, x + 18 + text_w(&font_body, v, 1), y, ">", P_GOLD, 1);
    } else text(&font_body, x + 14, y, v, col, 1);
}

static void editor_draw(void)
{
    draw_scene(-1);
    darken();
    hud();
    int px = 8, py = 36, pw = 464, ph = 212;
    panel(px, py, pw, ph, eg_index < 0 ? "NEW GOAL" : "EDIT GOAL", 232);
    /* preview on the right */
    int vx = px + 330;
    gfx_rect(vx, py + 12, 124, 96, P_NAVY);
    SceneModule m = {eg.color, eg.style, 0.75f, 0, eg.done};
    scene_draw_module_at(vx + 62, py + 54, &m, T, 3);
    text_center(&font_small, vx + 62, py + 98, module_style_names[eg.style], P_GREY, 1);
    text(&font_small, vx, py + 116, type_tags[eg.type], P_GOLD, 1);
    /* wrap description */
    const char *desc = type_desc[eg.type];
    char l1[40], l2[40];
    int cut = (int)strlen(desc);
    if (text_w(&font_body, desc, 1) > 124) {
        for (cut = (int)strlen(desc) - 1; cut > 0; cut--) {
            if (desc[cut] != ' ') continue;
            char tmp[64];
            memcpy(tmp, desc, cut);
            tmp[cut] = 0;
            if (text_w(&font_body, tmp, 1) <= 124) break;
        }
    }
    snprintf(l1, sizeof l1, "%.*s", cut, desc);
    snprintf(l2, sizeof l2, "%s", desc[cut] ? desc + cut + 1 : "");
    text(&font_body, vx, py + 126, l1, P_GREY, 1);
    text(&font_body, vx, py + 142, l2, P_GREY, 1);

    int rows = 10, rh = 19;
    if (ed_sel < ed_scroll) ed_scroll = ed_sel;
    if (ed_sel >= ed_scroll + rows) ed_scroll = ed_sel - rows + 1;
    char v[96];
    for (int r = 0; r < rows && ed_scroll + r < nfields; r++) {
        int k = ed_scroll + r, f = fields[k];
        int ry = py + 10 + r * rh;
        int sel = k == ed_sel;
        if (sel) {
            gfx_rect(px + 6, ry - 1, 314, rh - 1, P_DARK_SLATE);
            gfx_rect(px + 6, ry - 1, 2, rh - 1, P_YELLOW);
        }
        const char *label = "";
        uint32_t lc = sel ? P_WHITE : P_GREY;
        int lx = px + 14, vx2 = px + 124;
        switch (f) {
        case F_NAME:
            label = "Name";
            if (eg.name[0]) text_fit(&font_body, vx2 + 14, ry, eg.name, P_SAND, 1, 180);
            else text(&font_body, vx2 + 14, ry, sel ? "Press X to type" : "(no name yet)", P_SLATE, 1);
            break;
        case F_TYPE: label = "Type"; lr_value(vx2, ry, type_names[eg.type], sel, P_SAND); break;
        case F_DAYS: {
            label = "Days";
            static const char *dl = "MTWTFSS";
            for (int i = 0; i < 7; i++) {
                int on = (eg.days >> i) & 1;
                int bx = vx2 + 14 + i * 20;
                gfx_rect(bx, ry + 1, 16, 15, on ? gcol(&eg) : P_NAVY);
                char s[2] = {dl[i], 0};
                text_center(&font_body, bx + 8, ry, s, on ? P_NIGHT : P_SLATE, 1);
                if (sel && i == ed_day) gfx_rect(bx, ry + 16, 16, 2, P_YELLOW);
            }
            break;
        }
        case F_TIMES: label = "Times a week"; snprintf(v, sizeof v, "%d", eg.target); lr_value(vx2, ry, v, sel, P_SAND); break;
        case F_TARGET: label = "Target"; snprintf(v, sizeof v, "%d", eg.target); lr_value(vx2, ry, v, sel, P_SAND); break;
        case F_UNIT:
            label = "Unit";
            text(&font_body, vx2 + 14, ry, eg.unit[0] ? eg.unit : "(optional)", eg.unit[0] ? P_SAND : P_SLATE, 1);
            break;
        case F_STEPS:
            label = "Steps";
            snprintf(v, sizeof v, "%d step%s  (X to edit)", eg.nsteps, eg.nsteps == 1 ? "" : "s");
            text(&font_body, vx2 + 14, ry, v, P_SAND, 1);
            break;
        case F_START: label = "Start date"; fmt_date(v, sizeof v, eg.start_day); text(&font_body, vx2 + 14, ry, v, P_SAND, 1); break;
        case F_DEADLINE:
        case F_EVENT:
            label = f == F_EVENT ? "Event" : "Deadline";
            if (eg.has_deadline) {
                fmt_datetime(v, sizeof v, &eg.deadline, st.s.clock24);
                text_fit(&font_body, vx2 + 14, ry, v, P_SAND, 1, 180);
            } else text(&font_body, vx2 + 14, ry, "None", P_SLATE, 1);
            break;
        case F_XP_EACH:
            label = eg.type == G_COUNTER ? "XP per unit" : eg.type == G_MILESTONE ? "XP per step" : "XP per check-in";
            snprintf(v, sizeof v, "%d XP", eg.xp_each);
            lr_value(vx2, ry, v, sel, P_GOLD);
            break;
        case F_XP_BONUS:
            label = eg.type == G_WEEKLY ? "XP per week hit" : eg.type == G_COUNTDOWN ? "XP when done" : "XP on finishing";
            snprintf(v, sizeof v, "%d XP", eg.xp_bonus);
            lr_value(vx2, ry, v, sel, P_GOLD);
            break;
        case F_COLOR:
            label = "Stripes";
            gfx_rect(vx2 + 14, ry + 3, 12, 11, gcol(&eg));
            if (sel) {
                text(&font_body, vx2, ry, "<", P_GOLD, 1);
                text(&font_body, vx2 + 32, ry, module_color_names[eg.color], P_SAND, 1);
                text(&font_body, vx2 + 36 + text_w(&font_body, module_color_names[eg.color], 1), ry, ">", P_GOLD, 1);
            } else text(&font_body, vx2 + 32, ry, module_color_names[eg.color], P_SAND, 1);
            break;
        case F_STYLE: label = "Lighthouse"; lr_value(vx2, ry, module_style_names[eg.style], sel, P_SAND); break;
        case F_STATUS: label = "Status"; lr_value(vx2, ry, eg.done ? "Completed" : "Active", sel, eg.done ? P_GREEN : P_SAND); break;
        case F_DELETE: label = "Delete goal"; lc = sel ? P_RED : P_DARK_RED; break;
        case F_SAVE: label = eg_index < 0 ? "Build lighthouse" : "Save changes"; lc = sel ? P_YELLOW : P_GREEN; break;
        }
        text(&font_body, lx, ry, label, lc, 1);
    }
    if (nfields > rows) {
        int bh = rows * rh, by = py + 10;
        gfx_rect(px + 322, by, 2, bh, P_NAVY);
        gfx_rect(px + 322, by + bh * ed_scroll / nfields, 2, bh * rows / nfields, P_SLATE);
    }
    hints_begin();
    int f = fields[ed_sel];
    hint(K_UP | K_DOWN, "Field");
    if (f == F_NAME || f == F_UNIT) hint(K_X, "Type");
    else if (f == F_DAYS) { hint(K_LEFT | K_RIGHT, "Day"); hint(K_X, "On/off"); }
    else if (f == F_START || f == F_DEADLINE || f == F_EVENT) { hint(K_X, "Set"); if (f == F_DEADLINE) hint(K_TRI, "Clear"); }
    else if (f == F_STEPS || f == F_DELETE || f == F_SAVE) hint(K_X, "Select");
    else { hint(K_LEFT | K_RIGHT, "Change"); if (f == F_TARGET || f == F_XP_EACH || f == F_XP_BONUS) hint(K_L, "/R x10"); }
    hint(K_START, "Save");
    hint(K_O, "Cancel");
}

/* ------------------------------------------------------------------ */
/* STEPS editor                                                        */

static void steps_input(void)
{
    if (st_moving) {
        if (rp(K_UP) && st_sel > 0) {
            char t[NAME_LEN];
            memcpy(t, eg.steps[st_sel], NAME_LEN);
            memcpy(eg.steps[st_sel], eg.steps[st_sel - 1], NAME_LEN);
            memcpy(eg.steps[st_sel - 1], t, NAME_LEN);
            int a = (eg.steps_done >> st_sel) & 1, b = (eg.steps_done >> (st_sel - 1)) & 1;
            eg.steps_done &= ~((1 << st_sel) | (1 << (st_sel - 1)));
            eg.steps_done |= (a << (st_sel - 1)) | (b << st_sel);
            st_sel--;
            ed_changed = 1;
            snd_play(SND_MOVE);
        }
        if (rp(K_DOWN) && st_sel < eg.nsteps - 1) {
            char t[NAME_LEN];
            memcpy(t, eg.steps[st_sel], NAME_LEN);
            memcpy(eg.steps[st_sel], eg.steps[st_sel + 1], NAME_LEN);
            memcpy(eg.steps[st_sel + 1], t, NAME_LEN);
            int a = (eg.steps_done >> st_sel) & 1, b = (eg.steps_done >> (st_sel + 1)) & 1;
            eg.steps_done &= ~((1 << st_sel) | (1 << (st_sel + 1)));
            eg.steps_done |= (a << (st_sel + 1)) | (b << st_sel);
            st_sel++;
            ed_changed = 1;
            snd_play(SND_MOVE);
        }
        if (pr(K_X) || pr(K_SQ) || pr(K_O)) { st_moving = 0; snd_play(SND_OK); }
        return;
    }
    if (pr(K_O)) { screen = SCR_EDIT; snd_play(SND_BACK); return; }
    if (pr(K_TRI)) {
        if (eg.nsteps >= MAX_STEPS) { toast("12 steps is the maximum"); snd_play(SND_ERR); }
        else {
            eg.steps[eg.nsteps][0] = 0;
            st_sel = eg.nsteps;
            open_keyboard("NEW STEP", eg.steps[eg.nsteps], NAME_LEN - 1, 1);
        }
        return;
    }
    if (eg.nsteps == 0) return;
    if (rp(K_DOWN)) { st_sel = (st_sel + 1) % eg.nsteps; snd_play(SND_MOVE); }
    if (rp(K_UP)) { st_sel = (st_sel + eg.nsteps - 1) % eg.nsteps; snd_play(SND_MOVE); }
    if (pr(K_X)) open_keyboard("RENAME STEP", eg.steps[st_sel], NAME_LEN - 1, 0);
    if (pr(K_SQ)) { st_moving = 1; snd_play(SND_OK); }
    if (pr(K_R)) {
        char m[64];
        snprintf(m, sizeof m, "\"%s\"", eg.steps[st_sel]);
        confirm("Delete this step?", m, ACT_DELETE_STEP, st_sel);
    }
}

static void steps_draw(void)
{
    draw_scene(-1);
    darken();
    hud();
    int px = 8, py = 36, pw = 464, ph = 212;
    char title[48];
    snprintf(title, sizeof title, "STEPS  %d/%d", eg.nsteps, MAX_STEPS);
    panel(px, py, pw, ph, title, 232);
    text_fit(&font_body, px + 14, py + 10, eg.name[0] ? eg.name : "(unnamed goal)", P_GREY, 1, 430);
    if (eg.nsteps == 0) text(&font_body, px + 14, py + 40, "No steps yet. Press TRIANGLE to add the first one.", P_SAND, 1);
    for (int k = 0; k < eg.nsteps; k++) {
        int ry = py + 32 + k * 15;
        int sel = k == st_sel;
        int off = sel && st_moving ? 8 : 0;
        if (sel) gfx_rect(px + 6 + off, ry - 1, pw - 12 - off, 15, st_moving ? P_PLUM : P_DARK_SLATE);
        char n[8];
        snprintf(n, sizeof n, "%d.", k + 1);
        text(&font_body, px + 14 + off, ry - 2, n, P_GOLD, 1);
        text_fit(&font_body, px + 38 + off, ry - 2, eg.steps[k], sel ? P_WHITE : P_SAND, 1, 380);
        if ((eg.steps_done >> k) & 1) text_right(&font_small, px + pw - 14, ry + 2, "DONE", P_GREEN, 1);
    }
    hints_begin();
    if (st_moving) { hint(K_UP | K_DOWN, "Move"); hint(K_X, "Drop"); }
    else {
        hint(K_TRI, "Add step");
        if (eg.nsteps) { hint(K_X, "Rename"); hint(K_SQ, "Rearrange"); hint(K_R, "Delete"); }
        hint(K_O, "Back");
    }
}

/* ------------------------------------------------------------------ */
/* SETTINGS                                                            */

#define NSET 7
static void settings_input(void)
{
    if (rp(K_DOWN)) { set_sel = (set_sel + 1) % NSET; snd_play(SND_MOVE); }
    if (rp(K_UP)) { set_sel = (set_sel + NSET - 1) % NSET; snd_play(SND_MOVE); }
    if (pr(K_O) || (set_sel == NSET - 1 && pr(K_X))) {
        dirty = 1;
        recompute(1, -1);
        screen = SCR_HOME;
        snd_play(SND_BACK);
        return;
    }
    int dir = rp(K_RIGHT) ? 1 : rp(K_LEFT) ? -1 : 0;
    int big = rp(K_R) ? 1 : rp(K_L) ? -1 : 0;
    if (!dir && !big) return;
    Settings *s = &st.s;
    switch (set_sel) {
    case 0: s->xp_base = clampi(s->xp_base + dir * 10 + big * 100, 10, 1000000); break;
    case 1: s->xp_growth = clampi(s->xp_growth + dir * 5 + big * 25, 0, 300); break;
    case 2: s->streak_every = clampi(s->streak_every + dir + big * 7, 1, 365); break;
    case 3: s->streak_bonus = clampi(s->streak_bonus + dir * 5 + big * 50, 0, 1000000); break;
    case 4: s->miss_penalty = clampi(s->miss_penalty + dir + big * 10, 0, 1000000); break;
    case 5: s->clock24 = !s->clock24; break;
    }
    recompute(1, -1);
    snd_play(SND_MOVE);
}

static void settings_draw(void)
{
    draw_scene(-1);
    darken();
    hud();
    int px = 8, py = 36, pw = 464, ph = 212;
    panel(px, py, pw, ph, "XP & LEVEL SETTINGS", 232);
    static const char *labels[NSET] = {"XP for level 1 -> 2", "Extra XP each level", "Streak bonus every",
                                       "Streak bonus", "Missed day penalty", "Clock", "Done"};
    char v[64];
    for (int i = 0; i < NSET; i++) {
        int ry = py + 12 + i * 21;
        int sel = i == set_sel;
        if (sel) {
            gfx_rect(px + 6, ry - 1, 300, 19, P_DARK_SLATE);
            gfx_rect(px + 6, ry - 1, 2, 19, P_YELLOW);
        }
        text(&font_body, px + 14, ry, labels[i], i == NSET - 1 ? (sel ? P_YELLOW : P_GREEN) : (sel ? P_WHITE : P_GREY), 1);
        switch (i) {
        case 0: snprintf(v, sizeof v, "%d XP", st.s.xp_base); break;
        case 1: snprintf(v, sizeof v, "+%d%%", st.s.xp_growth); break;
        case 2: snprintf(v, sizeof v, "%d in a row", st.s.streak_every); break;
        case 3: snprintf(v, sizeof v, "%d XP", st.s.streak_bonus); break;
        case 4: snprintf(v, sizeof v, "-%d XP", st.s.miss_penalty); break;
        case 5: snprintf(v, sizeof v, "%s", st.s.clock24 ? "24 hour" : "12 hour"); break;
        default: v[0] = 0;
        }
        if (v[0]) lr_value(px + 170, ry, v, sel, i == 4 ? P_PINK : P_GOLD);
    }
    /* explanation + preview */
    int ex = px + 318;
    gfx_rect(ex, py + 12, 136, 186, P_NAVY);
    static const char *help[NSET][3] = {
        {"How much XP the", "first level-up", "costs."},
        {"Each level needs", "this much more XP", "than the one before."},
        {"Days in a row (weeks", "for weekly goals) to", "earn a streak bonus."},
        {"XP you get every time", "a streak reaches that", "many in a row."},
        {"XP taken away for each", "missed habit day or", "missed weekly target."},
        {"How times are shown.", "", ""},
        {"Go back to the", "coast.", ""}};
    for (int k = 0; k < 3; k++) text(&font_body, ex + 6, py + 16 + k * 15, help[set_sel][k], P_SAND, 1);
    text(&font_small, ex + 6, py + 70, "XP NEEDED", P_GOLD, 1);
    static const int lv[5] = {1, 2, 5, 10, 20};
    for (int k = 0; k < 5; k++) {
        snprintf(v, sizeof v, "Lv %d -> %d", lv[k], lv[k] + 1);
        text(&font_body, ex + 6, py + 82 + k * 15, v, P_GREY, 1);
        snprintf(v, sizeof v, "%d", xp_for_level(&st.s, lv[k]));
        text_right(&font_body, ex + 130, py + 82 + k * 15, v, P_GOLD, 1);
    }
    snprintf(v, sizeof v, "You: %d XP = Lv %d", cur_xp, cur_level);
    text(&font_body, ex + 6, py + 168, v, P_CYAN, 1);
    hints_begin();
    hint(K_UP | K_DOWN, "Setting");
    hint(K_LEFT | K_RIGHT, "Change");
    hint(K_L, "/R Big steps");
    hint(K_O, "Done");
}

/* ------------------------------------------------------------------ */
/* HELP                                                                */

static void help_draw(void)
{
    draw_scene(-1);
    darken();
    hud();
    int px = 8, py = 36, pw = 464, ph = 212;
    panel(px, py, pw, ph, "HOW IT WORKS", 232);
    static const char *lines[] = {
        "Every goal is a lighthouse on your stretch of coast.",
        "Keep it going and its light shines further. Miss a day",
        "and the lamp flickers red. Finish it and a boat sails in.",
        "The town on the cliff grows as you level up.",
        "Daily habit   -  check in on the days you pick.",
        "Counter         -  add up to a target (books, km...).",
        "Weekly target -  do it N times every Mon-Sun week.",
        "Milestones    -  tick off a list of steps.",
        "Countdown     -  days left until a date and time.",
        "",
        "You choose all the XP: per goal in its editor, and",
        "levels, streak bonus and penalties in Settings.",
        "Forgot to check in? Open the goal, press SQUARE.",
    };
    for (int i = 0; i < (int)(sizeof lines / sizeof lines[0]); i++)
        text(&font_body, px + 16, py + 10 + i * 15, lines[i], i == 0 ? P_YELLOW : P_SAND, 1);
    hints_begin();
    hint(K_O, "Back");
}

/* ------------------------------------------------------------------ */
/* main entry points                                                   */

void app_init(const char *path, const DateTime *t)
{
    strncpy(save_path, path, sizeof save_path - 1);
    now = *t;
    today = day_from_ymd(now.y, now.mo, now.d);
    scene_init();
    if (station_load(&st, save_path) != 0) {
        /* try the backup from the previous save */
        char bak[300];
        snprintf(bak, sizeof bak, "%s.bak", save_path);
        if (station_load(&st, bak) != 0) station_defaults(&st);
    }
    recompute(1, -1);
}

void app_save_now(void)
{
    if (station_save(&st, save_path) != 0) toast("Could not save to the Memory Stick!");
    dirty = 0;
}

Station *app_station(void) { return &st; }

void app_frame(uint32_t *screen_buf, unsigned buttons, float dt, const DateTime *t, int bat)
{
    keys_old = keys;
    keys = buttons;
    for (int i = 0; i < 16; i++) if (rep_t[i] > 0) rep_t[i] -= dt;
    T += dt;
    battery = bat;
    int old_today = today;
    now = *t;
    today = day_from_ymd(now.y, now.mo, now.d);
    if (today != old_today) recompute(1, -1); /* a new day: streaks and penalties move on */

    scene_update(dt);
    for (int k = 0; k < NFLOAT; k++)
        if (floaters[k].life > 0) { floaters[k].life -= dt; floaters[k].y -= (screen == SCR_HOME ? 22 : 5) * dt; }
    if (toast_t > 0) toast_t -= dt;
    if (levelup_t > 0) {
        levelup_t -= dt;
        if (pr(K_X) || pr(K_O)) levelup_t = 0; /* dismiss */
        else keys_old = keys;                  /* swallow input while showing */
    }
    if (screen == SCR_HOME) build_today();

    /* input */
    if (levelup_t <= 0) {
        switch (overlay) {
        case OV_MENU: menu_input(); break;
        case OV_KEYBOARD: kb_input(); break;
        case OV_DATE: dp_input(); break;
        case OV_CONFIRM: cf_input(); break;
        default:
            switch (screen) {
            case SCR_HOME: home_input(); break;
            case SCR_GOALS: goals_input(); break;
            case SCR_DETAIL: detail_input(); break;
            case SCR_EDIT: editor_input(); break;
            case SCR_STEPS: steps_input(); break;
            case SCR_SETTINGS: settings_input(); break;
            case SCR_HELP: if (pr(K_O) || pr(K_X)) { screen = SCR_HOME; snd_play(SND_BACK); } break;
            }
        }
    }
    if (screen == SCR_EDIT) build_fields();
    if (screen == SCR_HOME) build_today();
    if (det_i >= st.n) det_i = st.n ? st.n - 1 : 0;
    if (screen == SCR_DETAIL && st.n == 0) screen = SCR_HOME;

    /* draw */
    gfx_set_target(screen_buf);
    gfx_noclip();
    switch (screen) {
    case SCR_HOME: home_draw(); break;
    case SCR_GOALS: goals_draw(); break;
    case SCR_DETAIL: detail_draw(); break;
    case SCR_EDIT: editor_draw(); break;
    case SCR_STEPS: steps_draw(); break;
    case SCR_SETTINGS: settings_draw(); break;
    case SCR_HELP: help_draw(); break;
    }
    switch (overlay) {
    case OV_MENU: menu_draw(); break;
    case OV_KEYBOARD: kb_draw(); break;
    case OV_DATE: dp_draw(); break;
    case OV_CONFIRM: cf_draw(); break;
    }
    draw_effects_ui();

    if (dirty) app_save_now();
}
