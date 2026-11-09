/* scene.h - the pixel-art lighthouse coast (drawn at 240x136, shown at 2x) */
#ifndef SCENE_H_INCLUDED
#define SCENE_H_INCLUDED

#include <stdint.h>

#define SCENE_W 240
#define SCENE_H 136
#define SCENE_MAX_MODULES 14

typedef struct {
    int color, style;
    float power;   /* 0..1 how strong the light is */
    int alert;     /* missed something: lamp flickers red */
    int complete;  /* gold lamp + flag, a boat comes in */
} SceneModule;

typedef struct {
    int n;
    SceneModule m[SCENE_MAX_MODULES];
    int sel;       /* highlighted module, -1 none */
    int level;
    float t;       /* time in seconds, for animation */
    int hour, minute; /* clock: the sky follows day, dusk and night */
} SceneState;

extern const uint32_t module_colors[];
extern const char *module_color_names[];
extern const char *module_style_names[];
#define NUM_MODULE_STYLES 4

void scene_init(void);
void scene_update(float dt);
/* draws into a buffer with stride BUF_W, SCENE_W x SCENE_H */
void scene_draw(uint32_t *lowres, const SceneState *s);
/* upscale the low-res scene 2x into the screen buffer */
void scene_blit2x(uint32_t *dst, const uint32_t *lowres);
void scene_module_pos(int i, int *x, int *y); /* low-res position of lighthouse i's lamp */
void scene_burst(int i, uint32_t col);         /* sparkles from a lamp */
void scene_launch(int i, uint32_t col);        /* a boat sails in to lighthouse i */
/* a single lighthouse (used for previews), at pixel scale `scale`; cy = middle */
void scene_draw_module_at(int cx, int cy, const SceneModule *m, float t, int scale);

#endif
