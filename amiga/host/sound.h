/* amiga/host/sound.c: the game's sound on Paula (PROMPT.md 7.3, D-3). It takes what the game hands
 * the relay (hal_core.h's plat_sound_* and plat_psg: the command buffer, the PCM restarts, the PSG's
 * bytes) and plays it on Paula's four channels once a pass. */
#pragma once
#include <stdint.h>

/* where the sound backend writes Paula's registers (the Amiga) or a model of them (the host) */
struct paula_voice { const int8_t *ptr; uint32_t len; uint16_t per, vol; int8_t restart; const int8_t *loop; uint32_t loop_len; };
extern struct paula_voice paula[4];

int sound_start(uint8_t *chip, uint32_t chip_bytes, const uint8_t *rom);   /* chip: the platform's memory for samples */
void sound_command(const uint8_t *z80ram);
void sound_restart(const uint8_t *z80ram);
void sound_reset(int running);
void sound_psg(uint8_t v);
void sound_frame(void);                   /* once a pass: envelopes, voices; then paula[] holds what to play */
#define SOUND_CHIP_BYTES (256u * 1024u)
