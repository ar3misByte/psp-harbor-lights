/* model.h - goals, dates, streaks, XP and saving (portable C, no PSP code) */
#ifndef MODEL_H
#define MODEL_H

#define MAX_GOALS 32
#define MAX_STEPS 12
#define MAX_LOG 1000
#define NAME_LEN 32
#define UNIT_LEN 16

enum { G_HABIT = 0, G_COUNTER, G_WEEKLY, G_MILESTONE, G_COUNTDOWN, G_NTYPES };

typedef struct {
    int y, mo, d, h, mi;
} DateTime;

typedef struct {
    int day; /* day number (days since 1970-01-01) */
    int val; /* habit/weekly: 1 = done; counter: amount added that day */
} LogE;

typedef struct {
    char name[NAME_LEN];
    int type;
    int color;           /* module colour 0..NUM_MODULE_COLORS-1 */
    int style;           /* module shape 0..3 */
    int days;            /* habit: weekdays, bit 0 = Monday */
    int target;          /* counter: target amount; weekly: times per week */
    char unit[UNIT_LEN]; /* counter: "km", "books"... */
    int xp_each;         /* XP per check-in / unit / step */
    int xp_bonus;        /* XP for hitting the target / finishing */
    int start_day;
    int has_deadline;    /* countdowns always use the deadline as the event time */
    DateTime deadline;
    int done;            /* marked complete (countdown) */
    int done_day;
    int nsteps;
    char steps[MAX_STEPS][NAME_LEN];
    int steps_done;      /* bit mask */
    int nlog;
    LogE log[MAX_LOG];   /* sorted by day */
} Goal;

typedef struct {
    int xp_base;      /* XP needed for level 1 -> 2 */
    int xp_growth;    /* % more XP needed for each following level */
    int streak_every; /* bonus every N days (weeks for weekly goals) in a row */
    int streak_bonus; /* XP for each such streak milestone */
    int miss_penalty; /* XP lost per missed scheduled day / missed week */
    int clock24;
} Settings;

typedef struct {
    Goal goals[MAX_GOALS];
    int n;
    Settings s;
} Station;

/* ---- dates ---- */
int day_from_ymd(int y, int m, int d);
void ymd_from_day(int day, int *y, int *m, int *d);
int weekday(int day); /* 0 = Monday .. 6 = Sunday */
int days_in_month(int y, int m);
const char *month_short(int m);
const char *weekday_short(int wd);
long dt_minutes(const DateTime *t); /* minutes since 1970 */
void dt_from_day(DateTime *t, int day, int h, int mi);
void fmt_date(char *out, int n, int day);               /* "Mon 09 Nov 2026" */
void fmt_datetime(char *out, int n, const DateTime *t, int clock24);

/* ---- goals ---- */
void goal_defaults(Goal *g, int type, int today);
int log_get(const Goal *g, int day);
void log_set(Goal *g, int day, int val); /* val 0 removes */
int counter_total(const Goal *g);
int week_count(const Goal *g, int day); /* check-ins in the Mon-Sun week of day */
int habit_scheduled(const Goal *g, int day);
int streak_current(const Goal *g, int today);
int streak_best(const Goal *g, int today);
int goal_complete(const Goal *g, const DateTime *now);
float goal_power(const Goal *g, int today, const DateTime *now); /* 0..1, how "lit" the module is */
int goal_alert(const Goal *g, int today, const DateTime *now);   /* missed something */
int goal_due_today(const Goal *g, int today);  /* still something to do today */
int goal_xp(const Goal *g, const Settings *s, int today);
int popcount(unsigned v);

/* ---- station ---- */
void station_defaults(Station *st);
int station_xp(const Station *st, int today);
void level_from_xp(const Settings *s, int xp, int *level, int *into, int *need);
int xp_for_level(const Settings *s, int level); /* XP needed to go from level to level+1 */
int station_add(Station *st, const Goal *g);
void station_delete(Station *st, int i);
void station_move(Station *st, int i, int dir);

int station_save(const Station *st, const char *path);
int station_load(Station *st, const char *path);

#endif
