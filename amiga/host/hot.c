/* Routines of the game replaced for the 68020 (PROMPT.md section 4: hand-written code for measured
 * hot spots). Each does exactly what its original in src/ does: the same work RAM afterwards and
 * the same requests to the VDP in the same order, which gates A-LOGIC and A-DISPLAY-INTENT check on
 * every script. The original stays in the build as a weak definition (amiga/tools/mem_be.py), so
 * leaving this file out of the link gives the reference build to compare against. src/ is unchanged.
 *
 * remap (src/middle.c, middle94.asm): 16-colour tiles sent to VRAM through an 8-byte colour table, a
 * nibble at a time. A rink load sends about 25,000 words through it; the original's C reads the
 * table by 68000 address for every nibble, which took 57% of that pass's time on the 68020 (measured,
 * emulated: NHL94VFY name_entry PROFILE 1227 1227). Here the table is expanded once into a 256-entry
 * byte map, and the tiles are read through a pointer where they lie in one piece. */
#include "game.h"
#include "hal.h"
#include "mem.h"
#include "rom.h"

void remap(uint32_t src, uint32_t table, uint16_t words, uint16_t vram)
{
    uint16_t saved = g.disflags;
    g.disflags |= HI(dfng);
    Vmaddr(vram);
    uint8_t nib[16], map[256];
    for (int n = 0; n < 16; n++) {                        /* nibble n: the table's byte n / 2, its high half when n is even */
        uint8_t c = m8(table + (uint32_t)(n >> 1));
        nib[n] = (n & 1) ? (c & 0xF) : (uint8_t)(c >> 4);
    }
    for (int b = 0; b < 256; b++) map[b] = (uint8_t)(nib[b >> 4] << 4 | nib[b & 0xF]);
    uint32_t count = words ? words : 0x10000, x = src & 0xFFFFFF;   /* (do { } while (--words): 0 is 65,536) */
    const uint8_t *p = 0;
    if (x < ROM_SIZE && 2 * count <= ROM_SIZE - x) p = rom_cart + x;
    else if (x >= 0xE00000 && 2 * count <= 0x10000 - (x & 0xFFFF)) p = (const uint8_t *)&g + (x & 0xFFFF);
    if (p) {
        do { vdp_data((uint16_t)(map[p[0]] << 8 | map[p[1]])); p += 2; } while (--count);
    } else {                                              /* (across the end of the cartridge or of RAM: by address) */
        do { vdp_data((uint16_t)(map[m8(src)] << 8 | map[m8(src + 1)])); src += 2; } while (--count);
    }
    g.disflags = saved;
}
