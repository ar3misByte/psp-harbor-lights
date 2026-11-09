/* sfx.c - little 8-bit sound effects for the PSP (square waves + noise) */
#include <pspkernel.h>
#include <psputils.h>
#include <pspaudio.h>
#include <math.h>
#include <string.h>
#include "app.h"

#define RATE 44100
#define BLOCK 512
#define MAXLEN (RATE * 3 / 4) /* longest sound: 0.75 s */
#define NSND 9

static short *snd[NSND];
static int snd_len[NSND];
static short pool[MAXLEN * 2 * 3]; /* mono samples for all sounds */
static int pool_used = 0;
static short out[3][BLOCK * 2] __attribute__((aligned(64)));
static int ch = -1;
static SceUID sema = -1, th = -1;
static volatile int req = -1, quit = 0;

static unsigned int nz = 1;
static float noise(void) { nz = nz * 1664525u + 1013904223u; return (float)((int)(nz >> 9) & 0xFFFF) / 32768.0f - 1.0f; }

/* append a tone: square wave from f0 to f1 Hz, ms long, with a quick decay */
static void tone(int id, float f0, float f1, int ms, float vol, int is_noise)
{
    int n = RATE * ms / 1000;
    short *dst = snd[id] + snd_len[id];
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float f = f0 + (f1 - f0) * t;
        ph += f / RATE;
        if (ph >= 1) ph -= 1;
        float v = is_noise ? noise() : (ph < 0.5f ? 1.0f : -1.0f);
        float env = (1 - t) * (i < 80 ? i / 80.0f : 1);
        dst[i] = (short)(v * env * vol * 9000);
    }
    snd_len[id] += n;
}

static void make(void)
{
    static const int lens[NSND] = {30, 120, 120, 200, 140, 700, 500, 150, 20};
    for (int i = 0; i < NSND; i++) {
        snd[i] = pool + pool_used;
        pool_used += RATE * lens[i] / 1000 + 64;
        snd_len[i] = 0;
    }
    tone(SND_MOVE, 1400, 1400, 18, 0.25f, 0);
    tone(SND_OK, 660, 660, 50, 0.4f, 0); tone(SND_OK, 990, 990, 60, 0.4f, 0);
    tone(SND_BACK, 660, 660, 50, 0.35f, 0); tone(SND_BACK, 440, 440, 60, 0.35f, 0);
    tone(SND_CHECK, 988, 988, 50, 0.45f, 0); tone(SND_CHECK, 1319, 1319, 140, 0.45f, 0);
    tone(SND_UNCHECK, 523, 523, 60, 0.35f, 0); tone(SND_UNCHECK, 392, 392, 70, 0.35f, 0);
    tone(SND_LEVEL, 523, 523, 90, 0.45f, 0); tone(SND_LEVEL, 659, 659, 90, 0.45f, 0);
    tone(SND_LEVEL, 784, 784, 90, 0.45f, 0); tone(SND_LEVEL, 1047, 1047, 120, 0.45f, 0);
    tone(SND_LEVEL, 784, 784, 70, 0.4f, 0); tone(SND_LEVEL, 1047, 1047, 220, 0.5f, 0);
    tone(SND_LAUNCH, 200, 1200, 480, 0.35f, 1);
    tone(SND_ERR, 150, 120, 140, 0.4f, 0);
    tone(SND_TYPE, 1800, 1800, 12, 0.2f, 0);
}

static int sfx_thread(SceSize a, void *b)
{
    (void)a; (void)b;
    int cur = -1, pos = 0, ob = 0;
    while (!quit) {
        if (cur < 0) {
            SceUInt to = 100000;
            if (sceKernelWaitSema(sema, 1, &to) < 0) continue;
        }
        if (req >= 0) { cur = req; req = -1; pos = 0; }
        if (cur < 0) continue;
        short *o = out[ob];
        ob = (ob + 1) % 3; /* the hardware may still read the previous buffer */
        for (int i = 0; i < BLOCK; i++) {
            short v = pos < snd_len[cur] ? snd[cur][pos] : 0;
            o[i * 2] = o[i * 2 + 1] = v;
            pos++;
        }
        sceKernelDcacheWritebackRange(o, sizeof out[0]);
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, o);
        if (pos >= snd_len[cur] + BLOCK) cur = -1;
    }
    return 0;
}

void sfx_init(void)
{
    make();
    ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, BLOCK, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return;
    sema = sceKernelCreateSema("sfx", 0, 0, 1, 0);
    th = sceKernelCreateThread("sfx", sfx_thread, 0x12, 0x4000, PSP_THREAD_ATTR_USER, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}

void sfx_shutdown(void)
{
    quit = 1;
    if (th >= 0) { sceKernelSignalSema(sema, 1); sceKernelWaitThreadEnd(th, 0); }
    if (ch >= 0) sceAudioChRelease(ch);
}

void snd_play(int id)
{
    if (sema < 0 || id < 0 || id >= NSND) return;
    req = id;
    sceKernelSignalSema(sema, 1);
}
