/* amiga/host/screen.c: the game's picture on the A1200's own display. The machine is taken the
 * system-legal way (PROMPT.md section 5, as amiga/host/boot.c does) and one low-resolution playfield
 * of six bitplanes in Chip RAM shows disp_draw.c's planes; a copper list sets the 64 colours (24-bit,
 * from the Genesis's 3-bit levels through the host build's table), the width (256 or 320), and turns
 * the planes off while the game has the display off. Copper lists are double-buffered: the one built
 * after a pass is taken at the next vertical blank. */
#pragma once
#include "amiga.h"

#define SCREEN_PLANE_BYTES (40 * 224)
extern UBYTE *screen_plane[6];
extern int screen_pal;                    /* 1: a 50 Hz beam (PAL), 0: 60 Hz (NTSC, D-2's default) */

int screen_open(int pal);                 /* 0: not enough Chip RAM (nothing taken) */
void screen_present(void);                /* the copper list for the state disp_core.c holds now; shown from the next vblank */
void screen_wait_vblank(void);            /* until the next vertical blank begins */
ULONG screen_lines(void);                 /* raster lines since screen_open: the time base while the machine is taken */
int screen_frame_lines(void);             /* lines a frame of this beam */
void screen_close(void);
