/* sound.c's voices onto Paula: see audio.h. A voice to start again: its DMA off, two raster lines for
 * Paula to let go, its first block's pointer and length, the DMA on, two more lines for Paula to take
 * them, then the block it repeats after (Paula takes the location and length again each time a block
 * ends). Otherwise only the period and the volume change. */
#include "amiga.h"
#include "audio.h"
#include "sound.h"

#define AUD0LC  0x0A0
#define AUD0LEN 0x0A4
#define AUD0PER 0x0A6
#define AUD0VOL 0x0A8
#define DMAF_AUDIO 0x000F

static void lines(int n)
{
    UWORD l = CREG(VHPOSR) >> 8;
    while (n > 0) { UWORD m = CREG(VHPOSR) >> 8; if (m != l) { l = m; n--; } }
}

void audio_start(void)
{
    CREG(DMACON) = DMAF_AUDIO;
    for (int v = 0; v < 4; v++) CREG(AUD0VOL + 16 * v) = 0;
    CREG(INTENA) = 0x0780;                                /* (no audio interrupts) */
}

void audio_apply(void)
{
    UWORD restart = 0;
    for (int v = 0; v < 4; v++) if (paula[v].restart && paula[v].ptr) restart |= (UWORD)(1 << v);
    if (restart) {
        CREG(DMACON) = restart;
        lines(2);
        for (int v = 0; v < 4; v++)
            if (restart >> v & 1) {
                CREGL(AUD0LC + 16 * v) = (ULONG)paula[v].ptr;
                CREG(AUD0LEN + 16 * v) = (UWORD)(paula[v].len >> 1);
            }
        CREG(DMACON) = (UWORD)(DMAF_SETCLR | DMAF_MASTER | restart);
        lines(2);
        for (int v = 0; v < 4; v++)
            if (restart >> v & 1) {
                CREGL(AUD0LC + 16 * v) = (ULONG)paula[v].loop;
                CREG(AUD0LEN + 16 * v) = (UWORD)(paula[v].loop_len >> 1);
            }
    }
    for (int v = 0; v < 4; v++) {
        CREG(AUD0PER + 16 * v) = paula[v].per ? paula[v].per : 124;
        CREG(AUD0VOL + 16 * v) = paula[v].vol > 64 ? 64 : paula[v].vol;
    }
}

void audio_stop(void)
{
    for (int v = 0; v < 4; v++) CREG(AUD0VOL + 16 * v) = 0;
    CREG(DMACON) = DMAF_AUDIO;
}
