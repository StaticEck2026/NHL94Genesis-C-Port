/* amiga/host/disp_core.c: the display backend's portable half. It takes the display requests
 * hal_core.c hands the platform (hal_core.h's plat_ calls, the stream gate A-DISPLAY-INTENT hashes)
 * and keeps them in the forms the Amiga side draws from:
 *
 *   the tile cache   every 8x8 tile in planar form (four bitplanes a row), converted as its bytes
 *                    arrive; it is also the only record of what was sent to an address, so a
 *                    request that names earlier bytes (a VRAM copy) is answered from it
 *   the cell maps    planes A and B and the window: which tile, palette line, flips and priority
 *                    each cell holds, written through as the bytes arrive and taken again from the
 *                    tile cache when a layout register moves a table
 *   the sprite list  the first four bytes of each entry as the VDP latches them (position down the
 *                    screen, size, link), kept by the table's place when they were written
 *   palette, scroll  the 64 colours, the vertical scroll entries, the layout registers
 *
 * Nothing here draws. amiga/tools/halcheck.c (host) turns these lists into a picture and compares
 * it with the reference for every frame of every golden run; the Amiga's drawing code works from
 * the same lists and marks (dirty cells and tiles). */
#pragma once
#include <stdint.h>

#define DISP_TILES 2048
#define DISP_PLANE_CELLS 4096                             /* a plane is 32x32 to 128x32 or 32x128 cells: at most 4096 */

struct disp {
    uint8_t reg[24];
    uint16_t pal[64], vs[40];
    uint32_t tile[DISP_TILES][8];                         /* a tile's rows: byte k (from the top) is bitplane k, the left pixel its bit 7 */
    uint16_t cell[3][DISP_PLANE_CELLS];                   /* planes A and B and the window, row by row, in the table's own width */
    uint8_t sat[0x280];                                   /* each sprite entry's bytes 0 to 3, as latched */
    /* the layout, from the registers */
    int width, plane_w, plane_h, win_w;                   /* 256 or 320 pixels; cells */
    uint32_t base[3], size[3];                            /* the tables of A, B and the window: VRAM address and bytes */
    uint32_t hscroll, sprites, sprites_size;
    /* marks for the drawing side (disp_draw.c clears them when it has drawn) */
    uint8_t tile_dirty[DISP_TILES / 8];                   /* a tile's pixels changed */
    uint8_t cell_dirty[3][DISP_PLANE_CELLS / 8];          /* a cell of A, B or the window was written */
    uint8_t any_tile_dirty, any_cell_dirty, layout_dirty; /* layout_dirty: a register that places or scrolls the picture, a scroll entry */
    uint8_t sprites_dirty;                                /* a byte of the sprite table was written */
    uint32_t cells_written, tile_bytes_written;
    uint32_t why[26];                                     /* what set layout_dirty, counted: a register (0 to 23), a scroll entry (24), the horizontal scroll table (25) */
};
extern struct disp disp;

void disp_reset(void);
void disp_reg(int r, uint8_t v);
void disp_cram(int i, uint16_t c);
void disp_vsram(int i, uint16_t v);
void disp_vram_byte(uint16_t a, uint8_t b);
void disp_vram_word(uint16_t a, uint16_t w);
void disp_vram_long(uint16_t a, uint32_t v);
void disp_vram_block(uint16_t a, const uint8_t *src, uint32_t words);
void disp_vram_fill(uint16_t a, uint8_t b, uint32_t n, uint8_t step);
void disp_vram_copy(uint16_t a, uint16_t src, uint32_t n, uint8_t step);
/* what was sent to an address, from the tile cache */
uint8_t disp_byte(uint16_t a);
uint16_t disp_word(uint16_t a);
