/* sim.c - plays the real app on a PC with scripted button presses and saves screenshots.
 * Build: gcc -O1 -Isrc tools/sim.c src/scene.c src/gfx.c src/font.c src/fonts.c src/model.c -lm -o sim
 */
#include "../src/app.c"

static uint32_t fb[BUF_W * SCR_H];
static DateTime clk;
static int shots = 0;
void snd_play(int id) { (void)id; }

static void frame(unsigned b)
{
    app_frame(fb, b, 1.0f / 30, &clk, 76);
}
static void idle(int n) { for (int i = 0; i < n; i++) frame(0); }
static void press(unsigned b) { frame(b); frame(0); }
static void pressn(unsigned b, int n) { for (int i = 0; i < n; i++) press(b); }
static void shot(const char *name)
{
    char p[128];
    snprintf(p, sizeof p, "shots/%02d_%s.ppm", ++shots, name);
    fprintf(stderr, "shot %s (screen %d overlay %d)\n", name, screen, overlay);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n480 272\n255\n");
    for (int y = 0; y < 272; y++)
        for (int x = 0; x < 480; x++) {
            uint32_t c = fb[y * BUF_W + x];
            unsigned char q[3] = {C_R(c), C_G(c), C_B(c)};
            fwrite(q, 1, 3, f);
        }
    fclose(f);
}

/* type with the on-screen keyboard, like a person would */
static void type(const char *s)
{
    for (; *s; s++) {
        if (*s == ' ') { press(K_TRI); continue; }
        int want_shift = -1, r = -1, c = -1;
        for (int sh = 0; sh < 2 && r < 0; sh++)
            for (int rr = 0; rr < 4 && r < 0; rr++) {
                const char *p = strchr(kb_rows[sh][rr], *s);
                if (p) { r = rr; c = (int)(p - kb_rows[sh][rr]); want_shift = sh; }
            }
        if (r < 0) continue;
        if ((kb_shift ? 1 : 0) != want_shift) press(K_L);
        while (kb_row != r) press(kb_row < r ? K_DOWN : K_UP);
        while (kb_col != c) press(kb_col < c ? K_RIGHT : K_LEFT);
        press(K_X);
    }
}

static void goto_field(int f)
{
    for (int i = 0; i < 40 && fields[ed_sel] != f; i++) press(K_DOWN);
}

static void new_goal(void)
{
    press(K_START);           /* menu */
    while (menu_sel != 0) press(K_UP);
    press(K_X);               /* New goal */
}

static void set_type(int t)
{
    goto_field(F_TYPE);
    while (eg.type != t) press(K_RIGHT);
}

static void name_goal(const char *n)
{
    goto_field(F_NAME);
    press(K_X);
    type(n);
    press(K_START);
}

static void next_day(int n)
{
    int d = day_from_ymd(clk.y, clk.mo, clk.d) + n;
    ymd_from_day(d, &clk.y, &clk.mo, &clk.d);
    frame(0);
}

int main(void)
{
    system("mkdir -p shots && rm -f shots/*.ppm harbor_save.txt harbor_save.txt.bak");
    clk = (DateTime){2026, 10, 26, 19, 30}; /* start two weeks ago, a Monday */
    app_init("harbor_save.txt", &clk);
    idle(5);
    shot("home_empty");

    /* --- habit: Gym, weekdays, 20 XP per check-in --- */
    press(K_X); /* empty station: X = new goal */
    idle(2);
    shot("editor_new");
    goto_field(F_NAME);
    press(K_X);
    type("Gym session");
    idle(3);
    shot("keyboard");
    press(K_START);
    goto_field(F_DAYS);
    pressn(K_RIGHT, 5); press(K_X); press(K_RIGHT); press(K_X); /* Sat, Sun off */
    goto_field(F_XP_EACH);
    press(K_R);                    /* +10 -> 20 */
    goto_field(F_COLOR); pressn(K_RIGHT, 4);  /* cyan */
    goto_field(F_STYLE); pressn(K_RIGHT, 1);  /* dome */
    idle(2);
    shot("editor_filled");
    press(K_START);
    idle(10);
    shot("home_first_module");

    /* --- counter: Read books --- */
    new_goal();
    set_type(G_COUNTER);
    name_goal("Read 12 books");
    goto_field(F_TARGET); press(K_RIGHT); press(K_RIGHT); /* 10 -> 12 */
    goto_field(F_UNIT); press(K_X); type("books"); press(K_START);
    goto_field(F_XP_EACH); pressn(K_RIGHT, 5); /* 5 -> 10 */
    goto_field(F_DEADLINE); press(K_X); idle(2);
    /* deadline: 31 Dec 2026 23:59 */
    dp_field = 1; while (dp_v[1] != 12) press(K_UP);
    dp_field = 0; while (dp_v[0] != 31) press(K_UP);
    idle(2);
    shot("datepicker");
    press(K_X);
    goto_field(F_COLOR); pressn(K_RIGHT, 1);
    press(K_START);
    idle(3);

    /* --- weekly: Run 3x a week --- */
    new_goal();
    set_type(G_WEEKLY);
    name_goal("Run 5 km");
    goto_field(F_XP_BONUS); pressn(K_RIGHT, 4); /* 50 -> 70 */
    goto_field(F_STYLE); pressn(K_RIGHT, 2);
    press(K_START);
    idle(3);

    /* --- milestones: portfolio --- */
    new_goal();
    set_type(G_MILESTONE);
    name_goal("Launch portfolio site");
    goto_field(F_STEPS); press(K_X);
    press(K_TRI); type("Pick a design"); press(K_START);
    press(K_TRI); type("Write project pages"); press(K_START);
    press(K_TRI); type("Deploy it"); press(K_START);
    press(K_TRI); type("Share on LinkedIn"); press(K_START);
    /* move "Deploy it" above "Write project pages" */
    st_sel = 2; press(K_SQ); press(K_UP); press(K_X);
    idle(2);
    shot("steps_editor");
    press(K_O);
    goto_field(F_STYLE); pressn(K_RIGHT, 3);
    goto_field(F_COLOR); pressn(K_LEFT, 4);
    press(K_START);
    idle(3);

    /* --- countdown: exams --- */
    new_goal();
    set_type(G_COUNTDOWN);
    name_goal("CAT exams");
    goto_field(F_EVENT); press(K_X);
    dp_field = 1; while (dp_v[1] != 11) press(dp_v[1] < 11 ? K_UP : K_DOWN);
    dp_field = 0; while (dp_v[0] != 24) press(dp_v[0] < 24 ? K_UP : K_DOWN);
    dp_field = 3; while (dp_v[3] != 9) press(K_UP);
    dp_field = 4; while (dp_v[4] != 30) press(K_UP);
    idle(1);
    shot("datepicker_time");
    press(K_X);
    goto_field(F_XP_BONUS); press(K_R); /* +50 */
    press(K_START);
    idle(3);

    /* --- two weeks of life --- */
    for (int day = 0; day < 14; day++) {
        int wd = weekday(day_from_ymd(clk.y, clk.mo, clk.d));
        /* gym on weekdays except one missed Thursday */
        home_sel = 0;
        if (wd < 5 && day != 3) press(K_X);
        /* run on Mon, Wed, Fri */
        home_sel = 2;
        if (wd == 0 || wd == 2 || wd == 4) press(K_X);
        /* read: add pages every other day */
        home_sel = 1;
        if (day % 3 == 0) press(K_X);
        idle(2);
        if (day == 9) { idle(1); shot("home_levelup_or_progress"); }
        next_day(1);
    }
    idle(100); /* let effects finish */
    shot("home_two_weeks");

    /* detail: habit */
    home_sel = 0;
    press(K_TRI);
    idle(2);
    shot("detail_habit");
    press(K_SQ);
    press(K_UP); press(K_UP); press(K_LEFT);
    idle(2);
    shot("detail_calendar_edit");
    press(K_X); /* fix a past day */
    press(K_SQ);
    press(K_O);

    /* detail: counter, add 5 */
    home_sel = 1;
    press(K_TRI);
    press(K_RIGHT); press(K_RIGHT); press(K_X);
    idle(2);
    shot("detail_counter");
    press(K_O);

    /* detail: weekly */
    home_sel = 2;
    press(K_TRI);
    idle(2);
    shot("detail_weekly");
    press(K_O);

    /* milestones: tick two steps */
    home_sel = 3;
    press(K_TRI);
    press(K_X); press(K_DOWN); press(K_X);
    idle(2);
    shot("detail_milestones");
    press(K_O);

    /* countdown */
    home_sel = 4;
    press(K_TRI);
    idle(2);
    shot("detail_countdown");
    press(K_O);

    /* all goals, rearrange, delete prompt */
    press(K_SQ);
    idle(2);
    shot("goals_list");
    gl_sel = 4; press(K_SQ); press(K_UP); press(K_UP);
    idle(2);
    shot("goals_moving");
    press(K_X);
    press(K_R);
    idle(2);
    shot("confirm_delete");
    press(K_O);
    press(K_O);

    /* settings */
    press(K_START);
    while (menu_sel != 2) press(K_DOWN);
    idle(1);
    shot("menu");
    press(K_X);
    press(K_DOWN); press(K_DOWN); press(K_DOWN); press(K_DOWN);
    pressn(K_RIGHT, 5); /* 5 XP penalty */
    idle(2);
    shot("settings");
    press(K_O);

    /* finish the book goal -> rocket launch */
    home_sel = 1;
    press(K_TRI);
    while (amount_i < 6) press(K_RIGHT);
    press(K_X);
    idle(6);
    shot("goal_complete_boat");
    press(K_O);
    idle(4);

    /* edit + delete via editor */
    press(K_SQ);
    gl_sel = 0;
    press(K_TRI);
    idle(2);
    shot("editor_existing");
    press(K_O);
    press(K_O);

    /* help + hidden UI */
    press(K_START);
    while (menu_sel != 3) press(K_DOWN);
    press(K_X);
    idle(1);
    shot("help");
    press(K_O);
    press(K_SELECT);
    idle(30);
    shot("coast_dusk");
    clk.h = 11; clk.mi = 15; idle(40); shot("coast_day");
    clk.h = 22; clk.mi = 40; idle(40); shot("coast_night");
    press(K_SELECT);
    idle(10);
    shot("home_night");
    clk.h = 11; clk.mi = 15; idle(10);
    shot("home_day");

    /* persistence check */
    int xp_before = station_xp(&st, today), n_before = st.n;
    char name0[NAME_LEN];
    strcpy(name0, st.goals[0].name);
    Station *copy = malloc(sizeof(Station));
    if (station_load(copy, "harbor_save.txt") != 0) { printf("LOAD FAILED\n"); return 1; }
    printf("goals %d/%d, xp %d/%d, first '%s'/'%s', level %d\n", copy->n, n_before, station_xp(copy, today), xp_before,
           copy->goals[0].name, name0, cur_level);
    for (int i = 0; i < st.n; i++) {
        char s[64];
        goal_status(&st.goals[i], s, sizeof s);
        printf("  %-24s %-10s status=%-6s streak=%d best=%d xp=%d\n", st.goals[i].name, type_tags[st.goals[i].type], s,
               streak_current(&st.goals[i], today), streak_best(&st.goals[i], today), goal_xp(&st.goals[i], &st.s, today));
    }
    printf("%d screenshots\n", shots);
    return (copy->n == n_before && station_xp(copy, today) == xp_before) ? 0 : 1;
}
