/* amiga/host/blit.h: one blit, as the Amiga's blitter takes it (ascending mode, area mode), and the
 * means to run it: the blitter itself on the Amiga, a C model of the same operation on the host
 * (amiga/tools/halcheck.c), so that the host's frame-by-frame check also checks how the blits are
 * set up. Words are big-endian in memory, as the Amiga's. */
#pragma once
#include <stdint.h>

struct blit {
    const uint8_t *a, *b, *c;                             /* the sources (NULL: not used) */
    uint8_t *d;
    int16_t amod, bmod, cmod, dmod;                       /* bytes added after each row */
    uint16_t con0, con1;                                  /* BLTCON0 (A shift, USEx, minterm), BLTCON1 (B shift) */
    uint16_t fwm, lwm;                                    /* the first and last words' masks on A */
    uint16_t w, h;                                        /* words a row, rows */
};
#define BC0_USEA 0x0800
#define BC0_USEB 0x0400
#define BC0_USEC 0x0200
#define BC0_USED 0x0100
#define MINTERM_COPY_A 0xF0                               /* D = A */
#define MINTERM_COOKIE 0xCA                               /* D = A ? B : C */

void blit_run(const struct blit *bl);                     /* waits for the one before, then starts it */
void blit_wait(void);                                     /* until the last one has finished */
extern uint32_t blit_count, blit_words;
