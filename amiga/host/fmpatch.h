/* amiga/host/fmpatch.c: one of the game's FM patches (the 30 bytes the relay writes to the YM2612:
 * the operators' registers $30-$9C, then $B0 and $B4) as a looped 8-bit waveform for Paula. */
#pragma once
#include <stdint.h>

/* the operators' registers in a patch: group g ($30, $40, ... $90) and slot (register order S1, S3,
 * S2, S4) */
#define FM_PATCH_BYTES 30
static inline uint8_t fm_reg(const uint8_t *patch, int group, int op /* 1-4 */)
{
    static const uint8_t slot[5] = { 0, 0, 2, 1, 3 };
    return patch[group * 4 + slot[op]];
}
int fm_carriers(int alg);                                 /* how many carriers, counted from operator 4 down */
int fm_is_carrier(int alg, int op);

/* the waveform of two periods of the patch's lowest frequency (operators with MUL 0 sound at half
 * the others' rate) in `len` samples (a power of two, 8 to 256), signed 8-bit, the carriers at full
 * level and the modulators at their own total level. -> its peak (0 when the patch is silent) */
int fm_render(const uint8_t *patch, int len, int8_t *out);
