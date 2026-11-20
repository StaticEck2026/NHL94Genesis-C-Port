/* amiga/host/hal_core.c's state, and what each platform (the Amiga program, amiga/tools/halcheck)
 * gives it. */
#pragma once
#include <stdint.h>

struct core {
    /* the VDP's request state */
    uint8_t reg[24], code, pending, fill, fifo_idx;
    uint16_t addr, addr_hi, cram[64], vsram[40], fifo[4];
    int pal, vblank, in_vblank;
    uint32_t beam;
    /* I/O, pads */
    uint8_t io[16];
    uint16_t buttons[4];
    int four_way;
    /* the Z80's RAM (a mailbox), save RAM, the interrupt mask */
    uint8_t z80[0x2000], sram[0x2000];
    int z80_running, sram_dirty;
    uint16_t sr;
    /* counts */
    long frames, handshakes;
    uint32_t dmas, dma_blocks, fills, copies, vram_reads, commands, restarts, odd_targets;
};
extern struct core core;

void core_power_on(int pal);
int core_vdp_width(void);

/* the platform's side */
void plat_frame_done(long frame);                 /* at each game-loop wait, before the vblank */
uint16_t plat_buttons(int pad, long handshake);   /* a pad's buttons (MDRT_PAD_* bits) for the next frame */
void plat_vram_byte(uint16_t addr, uint8_t b);    /* a VRAM byte the game wrote: the display backend */
void plat_vram_block(uint16_t addr, const uint8_t *src, uint32_t words);   /* a DMA's words to VRAM at an even address, in order */
void plat_vram_word(uint16_t addr, uint16_t w);   /* one word to VRAM at an even address (high byte first) */
void plat_vram_fill(uint16_t addr, uint8_t b, uint32_t n, uint8_t step);   /* a VRAM fill: byte k of n goes to (addr + k * step) ^ 1 */
uint8_t plat_vram_read(uint16_t addr);            /* a VRAM byte back (VRAM copy and reads: counted) */
void plat_cram(int index, uint16_t colour);       /* a palette entry (9 bits, $0BGR) */
void plat_reg(int r, uint8_t v);                  /* a VDP register */
void plat_sound_command(const uint8_t *z80ram);   /* a command in Z80 RAM ($02-$22 and the patched bytes) */
void plat_sound_restart(const uint8_t *z80ram);  /* the relay started again ($96 = $29): its PCM pointers at $23 */
void plat_sound_reset(int running);
void plat_psg(uint8_t v);
