/* D-2's pacing: the game's passes at the Genesis's own rate, counted by CIA-B timer A (level 6).
 * PROMPT.md 3.3: NTSC 53 693 175 / (3420 x 262) = 59.923 Hz, PAL 53 203 424 / (3420 x 313) = 49.701 Hz.
 * The timer is claimed from ciab.resource while the OS runs (pace_claim), run while the machine is
 * taken (pace_start, pace_stop) and given back (pace_release). Its period alternates between two
 * whole numbers of E clock ticks so that the mean is the Genesis rate exactly (to 1/65536 tick). */
#ifndef PACE_H
#define PACE_H
#include "amiga.h"

extern volatile ULONG pace_count;                         /* Genesis vblanks since pace_start */

int pace_claim(void *sysbase);                            /* 0: the timer is someone else's (the pacing is the beam's then) */
void pace_start(ULONG eclock_hz, int pal);
void pace_stop(void);
void pace_release(void);

/* the beam against the timer, over n vblanks (the machine taken, pace_start done): the vblank rate in
 * millihertz, and its error against the Genesis rate in parts per million */
void pace_measure(int n, ULONG *vb_mhz, long *ppm);
ULONG pace_genesis_mhz(void);
#endif
