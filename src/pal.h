/* pal.h - the Endesga 32 palette (by Endesga), used for everything */
#ifndef PAL_H
#define PAL_H

#include "gfx.h"

#define HEX(h) RGB(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF)

#define P_RUST HEX(0xbe4a2f)
#define P_ORANGE_BROWN HEX(0xd77643)
#define P_SAND HEX(0xead4aa)
#define P_TAN HEX(0xe4a672)
#define P_CLAY HEX(0xb86f50)
#define P_BROWN HEX(0x733e39)
#define P_DARK_BROWN HEX(0x3e2731)
#define P_DARK_RED HEX(0xa22633)
#define P_RED HEX(0xe43b44)
#define P_ORANGE HEX(0xf77622)
#define P_GOLD HEX(0xfeae34)
#define P_YELLOW HEX(0xfee761)
#define P_GREEN HEX(0x63c74d)
#define P_MID_GREEN HEX(0x3e8948)
#define P_DARK_GREEN HEX(0x265c42)
#define P_DEEP_TEAL HEX(0x193c3e)
#define P_BLUE HEX(0x124e89)
#define P_SKY HEX(0x0099db)
#define P_CYAN HEX(0x2ce8f5)
#define P_WHITE HEX(0xffffff)
#define P_LIGHT_GREY HEX(0xc0cbdc)
#define P_GREY HEX(0x8b9bb4)
#define P_SLATE HEX(0x5a6988)
#define P_DARK_SLATE HEX(0x3a4466)
#define P_NAVY HEX(0x262b44)
#define P_NIGHT HEX(0x181425)
#define P_HOT_PINK HEX(0xff0044)
#define P_PLUM HEX(0x68386c)
#define P_MAGENTA HEX(0xb55088)
#define P_PINK HEX(0xf6757a)
#define P_PEACH HEX(0xe8b796)
#define P_COPPER HEX(0xc28569)

/* the colours a module can be painted in */
#define NUM_MODULE_COLORS 10

#endif
