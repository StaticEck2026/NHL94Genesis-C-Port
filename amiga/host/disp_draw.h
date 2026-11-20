/* amiga/host/disp_draw.c: the drawing side of the display backend. It keeps one picture of six
 * bitplanes (bit k of a pixel's colour number in plane k; number 0 is the backdrop) up to date with
 * amiga/host/disp_core.c's lists, redrawing only the 8x8 screen cells something changed under. */
#pragma once
#include <stdint.h>

#define DRAW_STRIDE 40                                    /* bytes a line in each bitplane (320 pixels) */
#define DRAW_LINES 224
struct draw {
    uint8_t plane[6][DRAW_STRIDE * DRAW_LINES];
    uint32_t cells_drawn, updates, full_updates;          /* counts: screen cells redrawn, calls, calls that redrew everything */
};
extern struct draw draw;

void draw_reset(void);
void draw_update(void);                                   /* at the game's wait: bring the picture up to date */
