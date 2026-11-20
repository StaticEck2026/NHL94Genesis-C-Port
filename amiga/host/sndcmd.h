/* The sound command stream (PROMPT.md 3.4, gate A-AUDIO-CMD): what the game hands the sound backend
 * in a pass, as amiga/host/hal_core.c passes it on (hal_core.h's plat_sound_* and plat_psg), hashed
 * in the order made: each command's 33-byte buffer ($02-$22) with the bytes the game patches into
 * the relay (the PCM rate at $8E, the volume page at $83), each restart's PCM pointers ($23-$28), each
 * reset, each PSG byte. The hash starts again at each pass; the host (amiga/tools/halcheck.c -a)
 * writes the reference, NAME.a32, and NHL94VFY <script> AUDIO checks it. */
#pragma once
#include <stdint.h>

#define SNDCMD_START 0x811C9DC5u
static uint32_t sndcmd_h = SNDCMD_START;
static inline void sndcmd_mix(uint32_t x) { sndcmd_h = (sndcmd_h ^ x) * 0x01000193u; }
static inline void sndcmd_command(const uint8_t *z)
{
    sndcmd_mix(0xC0DE0000u);
    for (int i = 0x02; i <= 0x22; i++) sndcmd_mix(z[i]);
    sndcmd_mix((uint32_t)z[0x8E] << 8 | z[0x83]);
}
static inline void sndcmd_restart(const uint8_t *z)
{
    sndcmd_mix(0x2E57A270u);
    for (int i = 0x23; i <= 0x28; i++) sndcmd_mix(z[i]);
    sndcmd_mix((uint32_t)z[0x8E] << 8 | z[0x83]);
}
static inline void sndcmd_reset(int running) { sndcmd_mix(0x2E5E7000u | (uint32_t)(running != 0)); }
static inline void sndcmd_psg(uint8_t v) { sndcmd_mix(0x95600000u | v); }
static inline uint32_t sndcmd_take(void) { uint32_t h = sndcmd_h; sndcmd_h = SNDCMD_START; return h; }
