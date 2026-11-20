/* amiga/host/disp_draw.c: the drawing side of the display backend. It keeps one picture of six
 * bitplanes (bit k of a pixel's colour number in plane k; number 0 is the backdrop) up to date with
 * amiga/host/disp_core.c's lists, redrawing only the 8x8 screen cells something changed under. The
 * planes are the platform's (Chip RAM on the Amiga), DRAW_STRIDE bytes a line, DRAW_LINES lines. */
#pragma once
#include <stdint.h>

#define DRAW_STRIDE 40                                    /* bytes a line in each bitplane (320 pixels) */
#define DRAW_LINES 224
#define DRAW_PLANE_BYTES (DRAW_STRIDE * DRAW_LINES)
struct draw {
    uint8_t *plane[6];
    uint32_t cells_drawn, updates, full_updates;          /* screen cells redrawn, calls, calls that redrew everything */
    uint32_t bytes_fast, bytes_slow;                      /* line bytes composed a plane byte at a time / a pixel at a time */
};
extern struct draw draw;

void draw_reset(uint8_t *const planes[6]);                /* the six planes, cleared */
void draw_update(void);                                   /* at the game's wait: bring the picture up to date */
