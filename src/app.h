/* app.h - Star Station app (portable: the PSP and PC builds both drive this) */
#ifndef APP_H
#define APP_H

#include <stdint.h>
#include "model.h"

enum {
    K_UP = 1 << 0, K_DOWN = 1 << 1, K_LEFT = 1 << 2, K_RIGHT = 1 << 3,
    K_X = 1 << 4, K_O = 1 << 5, K_TRI = 1 << 6, K_SQ = 1 << 7,
    K_L = 1 << 8, K_R = 1 << 9, K_START = 1 << 10, K_SELECT = 1 << 11
};

enum { SND_MOVE, SND_OK, SND_BACK, SND_CHECK, SND_UNCHECK, SND_LEVEL, SND_LAUNCH, SND_ERR, SND_TYPE };

/* provided by the platform */
void snd_play(int id);

void app_init(const char *save_path, const DateTime *now);
/* one frame: input, update, draw into screen (stride BUF_W) */
void app_frame(uint32_t *screen, unsigned buttons, float dt, const DateTime *now, int battery);
void app_save_now(void);
Station *app_station(void); /* for tests */

#endif
