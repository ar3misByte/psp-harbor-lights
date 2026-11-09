/* model.c - goals, dates, streaks, XP and saving */
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* dates (proleptic Gregorian, algorithm by Howard Hinnant)            */

int day_from_ymd(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

void ymd_from_day(int z, int *y, int *m, int *d)
{
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yy + (*m <= 2);
}

int weekday(int day) { return ((day % 7) + 7 + 3) % 7; } /* 1970-01-01 was a Thursday */

int days_in_month(int y, int m)
{
    static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    return (m >= 1 && m <= 12) ? dm[m - 1] : 30;
}

const char *month_short(int m)
{
    static const char *n[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return (m >= 1 && m <= 12) ? n[m - 1] : "???";
}

const char *weekday_short(int wd)
{
    static const char *n[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return n[((wd % 7) + 7) % 7];
}

long dt_minutes(const DateTime *t) { return (long)day_from_ymd(t->y, t->mo, t->d) * 1440L + t->h * 60 + t->mi; }

void dt_from_day(DateTime *t, int day, int h, int mi)
{
    ymd_from_day(day, &t->y, &t->mo, &t->d);
    t->h = h;
    t->mi = mi;
}

void fmt_date(char *out, int n, int day)
{
    int y, m, d;
    ymd_from_day(day, &y, &m, &d);
    snprintf(out, n, "%s %02d %s %d", weekday_short(weekday(day)), d, month_short(m), y);
}

void fmt_datetime(char *out, int n, const DateTime *t, int clock24)
{
    int day = day_from_ymd(t->y, t->mo, t->d);
    if (clock24)
        snprintf(out, n, "%s %02d %s %d, %02d:%02d", weekday_short(weekday(day)), t->d, month_short(t->mo), t->y, t->h, t->mi);
    else {
        int h12 = t->h % 12 == 0 ? 12 : t->h % 12;
        snprintf(out, n, "%s %02d %s %d, %d:%02d %s", weekday_short(weekday(day)), t->d, month_short(t->mo), t->y, h12, t->mi,
                 t->h < 12 ? "AM" : "PM");
    }
}

/* ------------------------------------------------------------------ */
/* logs                                                                */

static int log_find(const Goal *g, int day, int *ins)
{
    int lo = 0, hi = g->nlog - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g->log[mid].day == day) return mid;
        if (g->log[mid].day < day) lo = mid + 1;
        else hi = mid - 1;
    }
    if (ins) *ins = lo;
    return -1;
}

int log_get(const Goal *g, int day)
{
    int i = log_find(g, day, NULL);
    return i >= 0 ? g->log[i].val : 0;
}

void log_set(Goal *g, int day, int val)
{
    int ins = 0, i = log_find(g, day, &ins);
    if (val == 0) {
        if (i >= 0) {
            memmove(&g->log[i], &g->log[i + 1], (g->nlog - i - 1) * sizeof(LogE));
            g->nlog--;
        }
        return;
    }
    if (i >= 0) {
        g->log[i].val = val;
    } else {
        if (g->nlog >= MAX_LOG) {
            /* full: forget the oldest entry */
            if (ins == 0) return;
            memmove(&g->log[0], &g->log[1], (g->nlog - 1) * sizeof(LogE));
            g->nlog--;
            ins--;
        }
        memmove(&g->log[ins + 1], &g->log[ins], (g->nlog - ins) * sizeof(LogE));
        g->log[ins].day = day;
        g->log[ins].val = val;
        g->nlog++;
    }
    if (day < g->start_day) g->start_day = day; /* filling in older days moves the start back */
}

int counter_total(const Goal *g)
{
    int t = 0;
    for (int i = 0; i < g->nlog; i++) t += g->log[i].val;
    return t < 0 ? 0 : t;
}

static int week_start(int day) { return day - weekday(day); }

int week_count(const Goal *g, int day)
{
    int ws = week_start(day), c = 0;
    for (int i = 0; i < g->nlog; i++)
        if (g->log[i].day >= ws && g->log[i].day < ws + 7 && g->log[i].val > 0) c++;
    return c;
}

int popcount(unsigned v)
{
    int c = 0;
    for (; v; v &= v - 1) c++;
    return c;
}

/* ------------------------------------------------------------------ */
/* goal state                                                          */

static int deadline_day(const Goal *g) { return day_from_ymd(g->deadline.y, g->deadline.mo, g->deadline.d); }

/* last day the goal "runs" (no misses counted after it) */
static int active_until(const Goal *g, int today)
{
    int end = today;
    if (g->done && g->done_day < end) end = g->done_day;
    if (g->has_deadline && g->type != G_COUNTDOWN && deadline_day(g) < end) end = deadline_day(g);
    return end;
}

int habit_scheduled(const Goal *g, int day)
{
    int mask = g->days ? g->days : 0x7F;
    return (mask >> weekday(day)) & 1;
}

void goal_defaults(Goal *g, int type, int today)
{
    memset(g, 0, sizeof *g);
    g->type = type;
    g->days = 0x7F;
    g->target = type == G_WEEKLY ? 3 : 10;
    g->xp_each = type == G_COUNTER ? 5 : 10;
    g->xp_bonus = type == G_HABIT ? 0 : 50;
    g->start_day = today;
    g->color = 0;
    g->style = 0;
    if (type == G_COUNTDOWN) {
        g->has_deadline = 1;
        dt_from_day(&g->deadline, today + 7, 9, 0);
    }
}

int streak_current(const Goal *g, int today)
{
    if (g->type == G_HABIT) {
        int end = active_until(g, today);
        int d = end;
        if (d == today && habit_scheduled(g, today) && log_get(g, today) <= 0) d--; /* today is still open */
        int s = 0;
        for (; d >= g->start_day; d--) {
            if (!habit_scheduled(g, d)) continue;
            if (log_get(g, d) > 0) s++;
            else break;
        }
        return s;
    }
    if (g->type == G_WEEKLY) {
        int t = g->target > 0 ? g->target : 1;
        int ws = week_start(active_until(g, today)), first = week_start(g->start_day), s = 0;
        if (week_count(g, ws) >= t) s++;
        for (ws -= 7; ws >= first; ws -= 7) {
            if (week_count(g, ws) >= t) s++;
            else break;
        }
        return s;
    }
    return 0;
}

/* walk the whole history; calls back for every streak step. Returns best run. */
static int streak_walk(const Goal *g, int today, const Settings *s, int *bonus, int *misses)
{
    int run = 0, best = 0;
    if (bonus) *bonus = 0;
    if (misses) *misses = 0;
    int end = active_until(g, today);
    if (g->type == G_HABIT) {
        for (int d = g->start_day; d <= end; d++) {
            if (!habit_scheduled(g, d)) continue;
            if (log_get(g, d) > 0) {
                run++;
                if (run > best) best = run;
                if (s && bonus && s->streak_every > 0 && run % s->streak_every == 0) *bonus += s->streak_bonus;
            } else if (d < today) {
                run = 0;
                if (misses) (*misses)++;
            }
        }
    } else if (g->type == G_WEEKLY) {
        int t = g->target > 0 ? g->target : 1;
        int cur = week_start(today);
        for (int ws = week_start(g->start_day); ws <= week_start(end); ws += 7) {
            if (week_count(g, ws) >= t) {
                run++;
                if (run > best) best = run;
                if (s && bonus && s->streak_every > 0 && run % s->streak_every == 0) *bonus += s->streak_bonus;
            } else if (ws < cur) {
                run = 0;
                if (misses) (*misses)++;
            }
        }
    }
    return best;
}

int streak_best(const Goal *g, int today) { return streak_walk(g, today, NULL, NULL, NULL); }

int goal_complete(const Goal *g, const DateTime *now)
{
    (void)now;
    if (g->done) return 1;
    if (g->type == G_COUNTER) return g->target > 0 && counter_total(g) >= g->target;
    if (g->type == G_MILESTONE) return g->nsteps > 0 && popcount((unsigned)g->steps_done) >= g->nsteps;
    return 0;
}

float goal_power(const Goal *g, int today, const DateTime *now)
{
    if (goal_complete(g, now)) return 1;
    switch (g->type) {
    case G_HABIT: {
        int num = 0, den = 0;
        for (int d = today - 6; d <= today; d++) {
            if (d < g->start_day || !habit_scheduled(g, d)) continue;
            int done = log_get(g, d) > 0;
            if (d == today && !done) continue;
            den++;
            num += done;
        }
        return den ? (float)num / den : 0.0f;
    }
    case G_WEEKLY:
        return g->target > 0 ? (float)(week_count(g, today) > g->target ? g->target : week_count(g, today)) / g->target : 0;
    case G_COUNTER: {
        float p = g->target > 0 ? (float)counter_total(g) / g->target : 0;
        return p > 1 ? 1 : p;
    }
    case G_MILESTONE:
        return g->nsteps ? (float)popcount((unsigned)g->steps_done) / g->nsteps : 0;
    case G_COUNTDOWN: {
        long start = (long)g->start_day * 1440L, end = dt_minutes(&g->deadline), t = dt_minutes(now);
        if (end <= start) return 1;
        float p = (float)(t - start) / (float)(end - start);
        return p < 0 ? 0 : p > 1 ? 1 : p;
    }
    }
    return 0;
}

int goal_alert(const Goal *g, int today, const DateTime *now)
{
    if (goal_complete(g, now)) return 0;
    switch (g->type) {
    case G_HABIT: {
        int y = today - 1;
        if (y < g->start_day || !habit_scheduled(g, y)) return 0;
        if (g->has_deadline && y > deadline_day(g)) return 0;
        return log_get(g, y) <= 0;
    }
    case G_WEEKLY: {
        int prev = week_start(today) - 7;
        if (prev < week_start(g->start_day)) return 0;
        return week_count(g, prev) < (g->target > 0 ? g->target : 1);
    }
    case G_COUNTER:
    case G_MILESTONE:
        return g->has_deadline && dt_minutes(now) > dt_minutes(&g->deadline);
    }
    return 0;
}

int goal_due_today(const Goal *g, int today)
{
    if (g->done) return 0;
    switch (g->type) {
    case G_HABIT: return habit_scheduled(g, today) && log_get(g, today) <= 0;
    case G_WEEKLY: return week_count(g, today) < g->target;
    case G_COUNTER: return counter_total(g) < g->target;
    case G_MILESTONE: return popcount((unsigned)g->steps_done) < g->nsteps;
    case G_COUNTDOWN: return 0;
    }
    return 0;
}

int goal_xp(const Goal *g, const Settings *s, int today)
{
    int xp = 0, bonus = 0, misses = 0;
    switch (g->type) {
    case G_HABIT:
        for (int i = 0; i < g->nlog; i++)
            if (g->log[i].val > 0 && g->log[i].day <= today) xp += g->xp_each;
        streak_walk(g, today, s, &bonus, &misses);
        xp += bonus - misses * s->miss_penalty;
        break;
    case G_WEEKLY: {
        for (int i = 0; i < g->nlog; i++)
            if (g->log[i].val > 0 && g->log[i].day <= today) xp += g->xp_each;
        int t = g->target > 0 ? g->target : 1, end = active_until(g, today);
        for (int ws = week_start(g->start_day); ws <= week_start(end); ws += 7)
            if (week_count(g, ws) >= t) xp += g->xp_bonus;
        streak_walk(g, today, s, &bonus, &misses);
        xp += bonus - misses * s->miss_penalty;
        break;
    }
    case G_COUNTER:
        xp += counter_total(g) * g->xp_each;
        if (g->target > 0 && counter_total(g) >= g->target) xp += g->xp_bonus;
        break;
    case G_MILESTONE:
        xp += popcount((unsigned)g->steps_done) * g->xp_each;
        if (g->nsteps > 0 && popcount((unsigned)g->steps_done) >= g->nsteps) xp += g->xp_bonus;
        break;
    case G_COUNTDOWN:
        if (g->done) xp += g->xp_bonus;
        break;
    }
    return xp;
}

/* ------------------------------------------------------------------ */
/* station                                                             */

void station_defaults(Station *st)
{
    memset(st, 0, sizeof *st);
    st->s.xp_base = 100;
    st->s.xp_growth = 20;
    st->s.streak_every = 7;
    st->s.streak_bonus = 25;
    st->s.miss_penalty = 0;
    st->s.clock24 = 1;
}

int station_xp(const Station *st, int today)
{
    long xp = 0;
    for (int i = 0; i < st->n; i++) xp += goal_xp(&st->goals[i], &st->s, today);
    if (xp < 0) xp = 0;
    if (xp > 2000000000L) xp = 2000000000L;
    return (int)xp;
}

int xp_for_level(const Settings *s, int level)
{
    double need = s->xp_base > 0 ? s->xp_base : 1;
    for (int i = 1; i < level; i++) {
        need *= 1.0 + s->xp_growth / 100.0;
        if (need > 1e9) break;
    }
    return (int)(need + 0.5);
}

void level_from_xp(const Settings *s, int xp, int *level, int *into, int *need)
{
    int lv = 1;
    long left = xp;
    for (;;) {
        int n = xp_for_level(s, lv);
        if (left < n || lv >= 999) {
            *level = lv;
            *into = (int)left;
            *need = n;
            return;
        }
        left -= n;
        lv++;
    }
}

int station_add(Station *st, const Goal *g)
{
    if (st->n >= MAX_GOALS) return -1;
    st->goals[st->n] = *g;
    return st->n++;
}

void station_delete(Station *st, int i)
{
    if (i < 0 || i >= st->n) return;
    memmove(&st->goals[i], &st->goals[i + 1], (st->n - i - 1) * sizeof(Goal));
    st->n--;
}

void station_move(Station *st, int i, int dir)
{
    int j = i + dir;
    if (i < 0 || i >= st->n || j < 0 || j >= st->n) return;
    static Goal tmp;
    tmp = st->goals[i];
    st->goals[i] = st->goals[j];
    st->goals[j] = tmp;
}

/* ------------------------------------------------------------------ */
/* saving: a readable text file                                        */

static void put_str(FILE *f, const char *key, const char *v)
{
    fprintf(f, "%s=", key);
    for (; *v; v++) fputc((*v == '\n' || *v == '\r') ? ' ' : *v, f);
    fputc('\n', f);
}

int station_save(const Station *st, const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "HARBORLIGHTS 1\n");
    fprintf(f, "settings=%d %d %d %d %d %d\n", st->s.xp_base, st->s.xp_growth, st->s.streak_every, st->s.streak_bonus,
            st->s.miss_penalty, st->s.clock24);
    for (int i = 0; i < st->n; i++) {
        const Goal *g = &st->goals[i];
        fprintf(f, "GOAL\n");
        put_str(f, "name", g->name);
        fprintf(f, "type=%d\ncolor=%d\nstyle=%d\ndays=%d\ntarget=%d\n", g->type, g->color, g->style, g->days, g->target);
        put_str(f, "unit", g->unit);
        fprintf(f, "xp=%d %d\nstart=%d\n", g->xp_each, g->xp_bonus, g->start_day);
        fprintf(f, "deadline=%d %d %d %d %d %d\n", g->has_deadline, g->deadline.y, g->deadline.mo, g->deadline.d, g->deadline.h,
                g->deadline.mi);
        fprintf(f, "done=%d %d\nstepsdone=%d\n", g->done, g->done_day, g->steps_done);
        for (int k = 0; k < g->nsteps; k++) put_str(f, "step", g->steps[k]);
        for (int k = 0; k < g->nlog; k += 40) {
            fprintf(f, "log=");
            for (int j = k; j < g->nlog && j < k + 40; j++) fprintf(f, "%d:%d ", g->log[j].day, g->log[j].val);
            fputc('\n', f);
        }
        fprintf(f, "END\n");
    }
    int ok = !ferror(f);
    if (fclose(f) != 0) ok = 0;
    if (!ok) return -2;
    char bak[512];
    snprintf(bak, sizeof bak, "%s.bak", path);
    remove(bak);
    rename(path, bak);   /* keep the previous save as a backup */
    if (rename(tmp, path) != 0) {
        /* no rename on this file system: copy the temp file over instead */
        FILE *in = fopen(tmp, "rb"), *out = fopen(path, "wb");
        if (!in || !out) {
            if (in) fclose(in);
            if (out) fclose(out);
            return -3;
        }
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        fclose(in);
        if (fclose(out) != 0) return -4;
        remove(tmp);
    }
    return 0;
}

static int log_cmp(const void *a, const void *b) { return ((const LogE *)a)->day - ((const LogE *)b)->day; }

static void copy_str(char *dst, const char *src, int n)
{
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

int station_load(Station *st, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    static char line[2048];
    if (!fgets(line, sizeof line, f) || (strncmp(line, "HARBORLIGHTS", 12) != 0 && strncmp(line, "STARSTATION", 11) != 0)) {
        fclose(f);
        return -2;
    }
    station_defaults(st);
    Goal *g = NULL;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        const char *v = eq ? eq + 1 : "";
        if (eq) *eq = 0;
        if (!strcmp(line, "settings")) {
            sscanf(v, "%d %d %d %d %d %d", &st->s.xp_base, &st->s.xp_growth, &st->s.streak_every, &st->s.streak_bonus,
                   &st->s.miss_penalty, &st->s.clock24);
        } else if (!strcmp(line, "GOAL")) {
            g = st->n < MAX_GOALS ? &st->goals[st->n++] : NULL;
            if (g) memset(g, 0, sizeof *g);
        } else if (!g) {
            continue;
        } else if (!strcmp(line, "name")) copy_str(g->name, v, NAME_LEN);
        else if (!strcmp(line, "type")) g->type = atoi(v);
        else if (!strcmp(line, "color")) g->color = atoi(v);
        else if (!strcmp(line, "style")) g->style = atoi(v);
        else if (!strcmp(line, "days")) g->days = atoi(v);
        else if (!strcmp(line, "target")) g->target = atoi(v);
        else if (!strcmp(line, "unit")) copy_str(g->unit, v, UNIT_LEN);
        else if (!strcmp(line, "xp")) sscanf(v, "%d %d", &g->xp_each, &g->xp_bonus);
        else if (!strcmp(line, "start")) g->start_day = atoi(v);
        else if (!strcmp(line, "deadline"))
            sscanf(v, "%d %d %d %d %d %d", &g->has_deadline, &g->deadline.y, &g->deadline.mo, &g->deadline.d, &g->deadline.h,
                   &g->deadline.mi);
        else if (!strcmp(line, "done")) sscanf(v, "%d %d", &g->done, &g->done_day);
        else if (!strcmp(line, "stepsdone")) g->steps_done = atoi(v);
        else if (!strcmp(line, "step")) {
            if (g->nsteps < MAX_STEPS) copy_str(g->steps[g->nsteps++], v, NAME_LEN);
        } else if (!strcmp(line, "log")) {
            const char *p = v;
            int day, val, n;
            while (sscanf(p, "%d:%d%n", &day, &val, &n) == 2) {
                if (g->nlog < MAX_LOG) {
                    g->log[g->nlog].day = day;
                    g->log[g->nlog].val = val;
                    g->nlog++;
                }
                p += n;
            }
        } else if (!strcmp(line, "END")) {
            if (g->type < 0 || g->type >= G_NTYPES) g->type = G_HABIT;
            qsort(g->log, g->nlog, sizeof(LogE), log_cmp); /* in case the file was edited by hand */
            g = NULL;
        }
    }
    fclose(f);
    return 0;
}
