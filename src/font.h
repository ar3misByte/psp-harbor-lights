/* font.h - proportional pixel fonts */
#ifndef FONT_H
#define FONT_H

#include <stdint.h>

typedef struct {
    signed char adv, xo, yo; /* advance, offset from pen (yo: from baseline, negative = up) */
    unsigned char w, h;
    unsigned short off;      /* into data, rows packed MSB first */
} Glyph;

typedef struct {
    int ascent, descent;
    const Glyph *g;          /* 95 glyphs, ASCII 32..126 */
    const unsigned char *data;
} Font;

extern const Font font_body;  /* VT323 16px: normal text */
extern const Font font_small; /* Silkscreen 8px: labels */
extern const Font font_title; /* Press Start 2P 8px: headings */
extern const Font font_big;   /* Jersey 10 20px: big numbers */

int text_w(const Font *f, const char *s, int scale);
int text_h(const Font *f, int scale);
/* y is the top of the line box */
void text(const Font *f, int x, int y, const char *s, uint32_t col, int scale);
void text_sh(const Font *f, int x, int y, const char *s, uint32_t col, uint32_t shadow, int scale);
void text_center(const Font *f, int cx, int y, const char *s, uint32_t col, int scale);
void text_right(const Font *f, int rx, int y, const char *s, uint32_t col, int scale);
/* clipped to maxw, ending in ".." when it doesn't fit */
void text_fit(const Font *f, int x, int y, const char *s, uint32_t col, int scale, int maxw);

#endif
