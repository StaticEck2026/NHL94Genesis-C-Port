/* The display intent stream (PROMPT.md 3.2, gate A-DISPLAY-INTENT): every display request the game
 * makes, as amiga/host/hal_core.c hands it to the platform (the plat_ calls of hal_core.h), hashed in
 * the order made. The hash starts again at each pass, so a difference names its pass. The host
 * (amiga/tools/halcheck.c) writes the reference, NAME.i32, 4 bytes a pass, big-endian; the Amiga
 * (NHL94VFY <script> INTENT) must make the same numbers. A display backend takes exactly these
 * calls, so equal hashes say it is asked for the same picture, pass by pass.
 *
 * What is in the stream: words, bytes, fills and copies to VRAM (tiles, the planes' cells, the sprite
 * and scroll tables); palette and vertical-scroll entries; the registers that say how the picture is
 * laid out. Not in it: the registers that only carry a request (auto-increment, DMA length and
 * source), which the calls above already express. */
#pragma once
#include <stdint.h>

#define INTENT_START 0x811C9DC5u
static uint32_t intent_h = INTENT_START;

static inline void intent_mix(uint32_t x) { intent_h = (intent_h ^ x) * 0x01000193u; }
static inline void intent_word(uint16_t a, uint16_t w) { intent_mix((uint32_t)a << 16 | w); }
static inline void intent_long(uint16_t a, uint32_t v) { intent_word(a, (uint16_t)(v >> 16)); intent_word((uint16_t)(a + 2), (uint16_t)v); }
static inline void intent_block(uint16_t a, const uint8_t *src, uint32_t words)
{
    for (; words; words--, src += 2, a = (uint16_t)(a + 2)) intent_word(a, (uint16_t)(src[0] << 8 | src[1]));
}
static inline void intent_byte(uint16_t a, uint8_t b) { intent_mix(0xB17E0000u); intent_mix((uint32_t)a << 8 | b); }
static inline void intent_fill(uint16_t a, uint8_t b, uint32_t n, uint8_t step)
{
    intent_mix(0xF1110000u); intent_mix((uint32_t)a << 16 | (uint32_t)b << 8 | step); intent_mix(n);
}
static inline void intent_copy(uint16_t dst, uint16_t src, uint32_t n, uint8_t step)
{
    intent_mix(0xC0B10000u); intent_mix((uint32_t)dst << 16 | src); intent_mix(n << 8 | step);
}
static inline void intent_cram(int i, uint16_t c) { intent_mix(0xC0100000u); intent_mix((uint32_t)i << 16 | c); }
static inline void intent_vsram(int i, uint16_t v) { intent_mix(0x5C011000u); intent_mix((uint32_t)i << 16 | v); }
/* a register: display on and off and the modes (0, 1, 12), where the tables are (2 to 5, 13), the
 * backdrop (7), the line interrupt's count (10), the scroll modes (11), the planes' size (16), the
 * window (17, 18) */
static inline void intent_reg(int r, uint8_t v)
{
    if (!((1u << r) & 0x00073CBFu)) return;
    intent_mix(0x4E600000u);
    intent_mix((uint32_t)r << 8 | v);
}
/* the pass is over: its hash, and the next starts */
static inline uint32_t intent_take(void) { uint32_t h = intent_h; intent_h = INTENT_START; return h; }
