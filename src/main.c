/*
 * Harbor Lights - goals, habits and streaks as a pixel-art lighthouse coast (PSP)
 * Platform glue: display, buttons, clock, sound. Everything else is in app.c.
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>
#include <psprtc.h>
#include <string.h>
#include "app.h"
#include "gfx.h"

PSP_MODULE_INFO("Harbor Lights", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

void sfx_init(void);
void sfx_shutdown(void);

static volatile int exit_req = 0;

static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; exit_req = 1; return 0; }
static int cb_thread(SceSize a, void *b)
{
    (void)a; (void)b;
    int id = sceKernelCreateCallback("Exit Callback", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

static uint32_t drawbuf[BUF_W * SCR_H] __attribute__((aligned(64)));
static int backbuf = 1;
static void present(int vblanks)
{
    uint32_t *vram = (uint32_t *)(0x44000000 + backbuf * BUF_W * SCR_H * 4);
    memcpy(vram, drawbuf, sizeof drawbuf);
    for (int i = 0; i < vblanks; i++) sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf(vram, BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
    backbuf ^= 1;
}

static void get_now(DateTime *t)
{
    ScePspDateTime d;
    memset(&d, 0, sizeof d);
    sceRtcGetCurrentClockLocalTime(&d);
    t->y = d.year; t->mo = d.month; t->d = d.day; t->h = d.hour; t->mi = d.minute;
    if (t->y < 2000 || t->mo < 1 || t->d < 1) { t->y = 2026; t->mo = 1; t->d = 1; }
}

static unsigned map_buttons(unsigned b)
{
    unsigned k = 0;
    if (b & PSP_CTRL_UP) k |= K_UP;
    if (b & PSP_CTRL_DOWN) k |= K_DOWN;
    if (b & PSP_CTRL_LEFT) k |= K_LEFT;
    if (b & PSP_CTRL_RIGHT) k |= K_RIGHT;
    if (b & PSP_CTRL_CROSS) k |= K_X;
    if (b & PSP_CTRL_CIRCLE) k |= K_O;
    if (b & PSP_CTRL_TRIANGLE) k |= K_TRI;
    if (b & PSP_CTRL_SQUARE) k |= K_SQ;
    if (b & PSP_CTRL_LTRIGGER) k |= K_L;
    if (b & PSP_CTRL_RTRIGGER) k |= K_R;
    if (b & PSP_CTRL_START) k |= K_START;
    if (b & PSP_CTRL_SELECT) k |= K_SELECT;
    return k;
}

int main(int argc, char *argv[])
{
    int th = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
    scePowerSetClockFrequency(333, 333, 166);
    sceDisplaySetMode(0, SCR_W, SCR_H);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    /* save next to the EBOOT */
    char path[256] = "ms0:/PSP/GAME/HARBOR/harbor.txt";
    if (argc > 0 && argv[0]) {
        strncpy(path, argv[0], sizeof path - 20);
        char *sl = strrchr(path, '/');
        if (sl) strcpy(sl + 1, "harbor.txt");
    }
    sfx_init();
    DateTime now;
    get_now(&now);
    app_init(path, &now);

    SceInt64 last = sceKernelGetSystemTimeWide();
    int bat = -1;
    float bat_t = 0;
    while (!exit_req) {
        SceInt64 t = sceKernelGetSystemTimeWide();
        float dt = (float)(t - last) / 1e6f;
        last = t;
        if (dt > 0.1f) dt = 0.1f;
        bat_t -= dt;
        if (bat_t <= 0) {
            bat_t = 5;
            bat = scePowerIsBatteryExist() ? scePowerGetBatteryLifePercent() : -1;
        }
        get_now(&now);
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned k = map_buttons(pad.Buttons);
        /* the analog stick works like the d-pad */
        if (pad.Ly < 40) k |= K_UP;
        if (pad.Ly > 215) k |= K_DOWN;
        if (pad.Lx < 40) k |= K_LEFT;
        if (pad.Lx > 215) k |= K_RIGHT;
        app_frame(drawbuf, k, dt, &now, bat);
        present(2);
    }
    app_save_now();
    sfx_shutdown();
    sceKernelExitGame();
    return 0;
}
