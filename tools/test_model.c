#include "model.h"
#include <stdio.h>
#include <string.h>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static Station st, st2;
int main(void) {
    /* dates */
    CHECK(day_from_ymd(1970,1,1)==0, "epoch");
    int t = day_from_ymd(2026,11,9);
    CHECK(weekday(t)==0, "9 Nov 2026 is a Monday, got %d", weekday(t));
    int y,m,d; ymd_from_day(t,&y,&m,&d); CHECK(y==2026&&m==11&&d==9, "roundtrip");
    for (int k=-800000;k<800000;k+=997){ ymd_from_day(k,&y,&m,&d); CHECK(day_from_ymd(y,m,d)==k,"roundtrip %d",k); }
    CHECK(days_in_month(2028,2)==29 && days_in_month(2100,2)==28 && days_in_month(2000,2)==29, "leap");
    char buf[64]; fmt_date(buf,64,t); CHECK(!strcmp(buf,"Mon 09 Nov 2026"),"fmt %s",buf);
    DateTime dt={2026,12,1,18,5}; fmt_datetime(buf,64,&dt,0); CHECK(!strcmp(buf,"Tue 01 Dec 2026, 6:05 PM"),"fmt12 %s",buf);

    station_defaults(&st);
    Settings *s=&st.s; s->streak_every=7; s->streak_bonus=25; s->miss_penalty=5;
    /* habit every day, started 20 days ago */
    Goal g; goal_defaults(&g,G_HABIT,t-20); strcpy(g.name,"Gym");
    for (int k=t-20;k<=t-11;k++) log_set(&g,k,1);   /* 10 days */
    /* missed t-10 */
    for (int k=t-9;k<=t-1;k++) log_set(&g,k,1);     /* 9 days, today open */
    CHECK(streak_current(&g,t)==9,"current %d",streak_current(&g,t));
    CHECK(streak_best(&g,t)==10,"best %d",streak_best(&g,t));
    CHECK(goal_alert(&g,t,&dt)==0,"no alert");
    CHECK(goal_due_today(&g,t)==1,"due");
    /* xp: 19 checkins*10 + bonus at run 7 (first run) + bonus at 7 (second run) - 1 miss*5 */
    CHECK(goal_xp(&g,s,t)==190+50-5,"xp %d",goal_xp(&g,s,t));
    log_set(&g,t,1);
    CHECK(streak_current(&g,t)==10,"after checkin %d",streak_current(&g,t));
    log_set(&g,t,0);
    /* mon/wed/fri habit: skip days don't break */
    Goal h; goal_defaults(&h,G_HABIT,t-14); h.days=1|4|16;
    for (int k=t-14;k<t;k++) if (habit_scheduled(&h,k)) log_set(&h,k,1);
    CHECK(streak_current(&h,t)==6,"mwf streak %d",streak_current(&h,t));
    CHECK(goal_xp(&h,s,t)==60,"mwf xp %d",goal_xp(&h,s,t));
    /* back-filling before start moves the start */
    log_set(&h,t-30,1); CHECK(h.start_day==t-30,"start moved");
    /* weekly 3x */
    Goal w; goal_defaults(&w,G_WEEKLY,t-21); w.target=3; w.xp_each=10; w.xp_bonus=30;
    int ws=t-21; /* t is Monday -> t-21 Monday */
    for (int k=0;k<3;k++) log_set(&w,ws+k,1);        /* week 1 met */
    for (int k=0;k<1;k++) log_set(&w,ws+7+k,1);      /* week 2 missed */
    for (int k=0;k<4;k++) log_set(&w,ws+14+k,1);     /* week 3 met (4) */
    CHECK(week_count(&w,t-1)==4,"wk3 count %d",week_count(&w,t-1));
    CHECK(streak_current(&w,t)==1,"weekly streak %d",streak_current(&w,t));
    CHECK(goal_xp(&w,s,t)==8*10+2*30-5,"weekly xp %d",goal_xp(&w,s,t));
    /* counter */
    Goal c; goal_defaults(&c,G_COUNTER,t); c.target=12; c.xp_each=5; c.xp_bonus=100;
    log_set(&c,t,7); log_set(&c,t-1,5);
    CHECK(counter_total(&c)==12 && goal_complete(&c,&dt),"counter complete");
    CHECK(goal_xp(&c,s,t)==60+100,"counter xp %d",goal_xp(&c,s,t));
    /* milestone */
    Goal ms; goal_defaults(&ms,G_MILESTONE,t); ms.nsteps=3; strcpy(ms.steps[0],"Plan"); strcpy(ms.steps[1],"Build"); strcpy(ms.steps[2],"Ship");
    ms.steps_done=1|4; CHECK(goal_xp(&ms,s,t)==20,"ms xp"); ms.steps_done=7; CHECK(goal_xp(&ms,s,t)==30+50,"ms xp2");
    /* countdown */
    Goal cd; goal_defaults(&cd,G_COUNTDOWN,t-10); dt_from_day(&cd.deadline,t+10,0,0);
    DateTime now; dt_from_day(&now,t,0,0);
    float p=goal_power(&cd,t,&now); CHECK(p>0.49f&&p<0.51f,"countdown power %f",p);
    /* levels: base 100, +20% */
    int lv,into,need; level_from_xp(s,0,&lv,&into,&need); CHECK(lv==1&&need==100,"lv1");
    level_from_xp(s,100,&lv,&into,&need); CHECK(lv==2&&into==0&&need==120,"lv2 %d %d %d",lv,into,need);
    level_from_xp(s,100+120+144+10,&lv,&into,&need); CHECK(lv==4&&into==10,"lv4 %d %d",lv,into);
    /* station ops + save/load */
    station_add(&st,&g); station_add(&st,&h); station_add(&st,&w); station_add(&st,&c); station_add(&st,&ms); station_add(&st,&cd);
    strcpy(st.goals[1].name,"Read = 20 pages | daily"); strcpy(st.goals[3].unit,"books");
    station_move(&st,0,1); CHECK(!strcmp(st.goals[1].name,"Gym"),"move");
    station_move(&st,1,-1); CHECK(!strcmp(st.goals[0].name,"Gym"),"move back");
    int xp1=station_xp(&st,t);
    CHECK(station_save(&st,"/tmp/claude-0/st.dat")==0,"save");
    CHECK(station_save(&st,"/tmp/claude-0/st.dat")==0,"save again (with backup)");
    CHECK(station_load(&st2,"/tmp/claude-0/st.dat")==0,"load");
    CHECK(st2.n==6,"n %d",st2.n);
    CHECK(!strcmp(st2.goals[1].name,"Read = 20 pages | daily"),"name '%s'",st2.goals[1].name);
    CHECK(station_xp(&st2,t)==xp1,"xp after load %d vs %d",station_xp(&st2,t),xp1);
    CHECK(st2.s.miss_penalty==5,"settings");
    CHECK(!strcmp(st2.goals[4].steps[2],"Ship") && st2.goals[4].steps_done==7,"steps");
    CHECK(st2.goals[5].deadline.d==cd.deadline.d && st2.goals[5].has_deadline,"deadline");
    station_delete(&st2,0); CHECK(st2.n==5 && !strcmp(st2.goals[0].name,"Read = 20 pages | daily"),"delete");
    /* big log: 1000+ entries */
    Goal big; goal_defaults(&big,G_HABIT,t-1200); for(int k=t-1200;k<=t;k++) log_set(&big,k,1);
    CHECK(big.nlog==MAX_LOG,"log capped %d",big.nlog);
    printf("total XP %d\n", xp1);
    printf(fails? "%d FAILURES\n":"ALL TESTS PASSED\n", fails);
    return fails!=0;
}
