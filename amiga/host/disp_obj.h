/* amiga/host/disp_obj.c: the scrolled scene (amiga/notes/p7_design.md). When the picture is 256 or 320 wide,
 * plane B scrolls as one piece and plane A does not scroll (the match), plane B is drawn once into
 * its own planar bitmap (the rink bitmap: the plane's 512 pixels and its first 256 again, 256 lines,
 * six planes interleaved a line) and the picture is two display buffers made from it, shown in turn
 * with the hardware's scroll, with plane A, the window and the sprites drawn into them each pass as
 * objects in the VDP's priority order. Lines where the VDP's sprite limits cut in are composed by the
 * CPU. Other pictures stay with disp_draw.c. */
#pragma once
#include <stdint.h>

#define OBJ_RW 104                                        /* bytes a plane line of the rink bitmaps (832 pixels: the plane's 512 and 320 again) */
#define OBJ_RH 256
#define OBJ_LS (OBJ_RW * 6)                               /* bytes a line: six planes interleaved */
#define OBJ_BYTES (OBJ_LS * OBJ_RH)
#define OBJ_SPR_SLOTS 128                                 /* sprite images cached by what they show */
#define OBJ_CHIP_BYTES (4 * OBJ_BYTES + 3 * 42 * 6 * 224 + 2 * OBJ_SPR_SLOTS * 6 * 32 * 6)   /* everything the blitter reads or writes */
struct obj {
    uint8_t *rink, *rink_hi, *buf[2];                     /* in the platform's block (Chip RAM on the Amiga); rink_hi: the mask, repeated a plane row */
    int shown;                                            /* the buffer to show after obj_update */
    int px0, py0;                                         /* where the screen's top left pixel lies in it */
    uint32_t updates, restored, drawn, cpu_lines, rebuilds;
    uint32_t rink_cells, sprite_builds, sprites_drawn, a_cells;   /* counts, for the cost */
    uint32_t w_restore, w_recut, w_a, w_sprite;           /* words blitted, by kind */
};
extern struct obj obj;

void obj_reset(uint8_t *chip);                            /* chip: OBJ_CHIP_BYTES, the platform's (Chip RAM on the Amiga) */
int obj_scene(void);                                      /* the picture now is one this path draws */
void obj_update(void);                                    /* at the game's wait, when obj_scene(): the next buffer */
int obj_pixel(int x, int y);                              /* the shown buffer's colour number at screen pixel x, y */
void obj_window(uint8_t *const planes[6]);                /* the shown window as six 40-byte-a-line planes (the planes' hash) */
void obj_leave(void);                                     /* the picture went back to disp_draw.c: rebuild on the way in */
