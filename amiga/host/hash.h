/* The per-pass work RAM hash of the A-LOGIC gate, the same on the Amiga and in amiga/tools/halcheck:
 * a Fletcher-style pair of 32-bit sums over RAM's 16384 big-endian longs (as the 68000 has them),
 * cheap on a 68020 (about 11 ms a pass at 14 MHz) where FNV-1a 64 would take most of a second. */
#pragma once
#include <stdint.h>

static inline uint32_t ram_hash32(const uint8_t *b)
{
    uint32_t a = 0x9E3779B9u, s = 0;
    for (uint32_t i = 0; i < 0x10000; i += 4) {
#if defined(__mc68000__) || defined(__mc68020__)
        uint32_t w = *(const uint32_t *)(b + i);                  /* (the machine's own byte order) */
#else
        uint32_t w = (uint32_t)b[i] << 24 | (uint32_t)b[i + 1] << 16 | (uint32_t)b[i + 2] << 8 | b[i + 3];
#endif
        a += w;
        s += a;
    }
    return a ^ (s << 7 | s >> 25);
}
