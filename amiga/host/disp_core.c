/* The display backend's portable half: see disp_core.h. */
#include <string.h>
#include "disp_core.h"

struct disp disp;

/* two pixels (a byte as the game sends it: the left pixel in the high nibble) as their bits in the
 * four bitplanes, at the left of each plane's byte: byte k of the result is plane k */
static uint32_t c2p[256];
static void c2p_make(void)
{
    for (int b = 0; b < 256; b++) {
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) {
            uint32_t bits = (uint32_t)(((b >> (4 + k)) & 1) << 7 | ((b >> k) & 1) << 6);
            v |= bits << (24 - 8 * k);
        }
        c2p[b] = v;
    }
}

/* where the tables are: from the layout registers (2, 3, 4: planes A, window, B; 5: sprites; 12:
 * the width; 13: horizontal scroll; 16: the planes' size) */
static void layout(void)
{
    static const int cells[4] = { 32, 64, 32, 128 };
    struct disp *d = &disp;
    int h40 = d->reg[12] & 0x81;
    d->width = h40 ? 320 : 256;
    d->plane_w = cells[d->reg[16] & 3];
    d->plane_h = cells[(d->reg[16] >> 4) & 3];
    if (d->plane_w * d->plane_h > DISP_PLANE_CELLS) d->plane_h = DISP_PLANE_CELLS / d->plane_w;
    d->win_w = h40 ? 64 : 32;
    d->base[0] = (uint32_t)(d->reg[2] & 0x38) << 10;
    d->base[1] = (uint32_t)(d->reg[4] & 7) << 13;
    d->base[2] = h40 ? (uint32_t)(d->reg[3] & 0x3C) << 10 : (uint32_t)(d->reg[3] & 0x3E) << 10;
    d->size[0] = d->size[1] = (uint32_t)(d->plane_w * d->plane_h * 2);
    d->size[2] = (uint32_t)(d->win_w * 32 * 2);
    d->hscroll = (uint32_t)(d->reg[13] & 0x3F) << 10;
    d->sprites = h40 ? (uint32_t)(d->reg[5] & 0x7E) << 9 : (uint32_t)(d->reg[5] & 0x7F) << 9;
    d->sprites_size = h40 ? 0x280 : 0x200;
    /* which 64-byte blocks of VRAM hold a table: a write anywhere else is only a tile's pixels */
    memset(d->special, 0, sizeof d->special);
    uint32_t at[5] = { d->base[0], d->base[1], d->base[2], d->sprites, d->hscroll }, n[5] = { d->size[0], d->size[1], d->size[2], d->sprites_size, 0x380 };
    static const uint8_t kind[5] = { 1, 1, 1, 2, 4 };
    for (int t = 0; t < 5; t++)
        for (uint32_t o = 0; o < n[t] + 63; o += 64) d->special[((at[t] + o) & 0xFFFF) >> 6] |= kind[t];
}

/* a cell map taken again from what was sent to its table's addresses, and the tiles' use counts */
static void recount(void)
{
    memset(disp.uses, 0, sizeof disp.uses);
    for (int k = 0; k < 3; k++)
        for (uint32_t i = 0; i < disp.size[k] / 2; i++) disp.uses[disp.cell[k][i] & 0x7FF]++;
}
static void reseed(int k)
{
    for (uint32_t i = 0; i < disp.size[k] / 2; i++) disp.cell[k][i] = disp_word((uint16_t)(disp.base[k] + 2 * i));
}

void disp_reset(void)
{
    memset(&disp, 0, sizeof disp);
    if (!c2p[255]) c2p_make();
    layout();
    recount();
}

void disp_reg(int r, uint8_t v)
{
    if (r < 0 || r >= 24 || disp.reg[r] == v) { if (r >= 0 && r < 24) disp.reg[r] = v; return; }
    uint32_t base[3] = { disp.base[0], disp.base[1], disp.base[2] }, size[3] = { disp.size[0], disp.size[1], disp.size[2] };
    uint32_t hs = disp.hscroll, sp = disp.sprites, spn = disp.sprites_size;
    disp.reg[r] = v;
    if ((1u << r) & 0x7303Cu) {                           /* (only 2-5, 12, 13, 16-18 place the tables or size the picture) */
    layout();
    int moved = 0;
    for (int k = 0; k < 3; k++) if (base[k] != disp.base[k] || size[k] != disp.size[k]) { reseed(k); moved = 1; }
    if (moved) recount();
    if (hs != disp.hscroll) { for (int i = 0; i < 0x380; i++) disp.hsc[i] = disp_byte((uint16_t)(disp.hscroll + (uint32_t)i)); disp.scroll_dirty = 1; }
    if (sp != disp.sprites || spn != disp.sprites_size) {
        for (uint32_t i = 0; i < 0x280; i++) disp.spr[i] = disp_byte((uint16_t)(disp.sprites + i));   /* (the latched bytes stay: the VDP's do)*/
        disp.sprites_dirty = 1;
    }
    }
    if (r == 11) disp.scroll_dirty = 1;
    /* a register that places, sizes or scrolls the picture means the bitmap is drawn again. Not
     * these: 1 (display on and off is the copper's business, the bitmap stays up to date behind it;
     * its other bits are interrupts and DMA), 7 (the backdrop is colour 0), 10 (the line interrupt),
     * 11 (the scroll modes: drawn as a scroll change), 15 and 19 on (the requests' own registers) */
    if (r != 1 && r != 7 && r != 10 && r != 11 && r != 15 && r < 19) { disp.layout_dirty = 1; disp.why[r]++; }
}

void disp_cram(int i, uint16_t c) { disp.pal[i & 63] = c; }
void disp_vsram(int i, uint16_t v)
{
    if (i < 0 || i >= 40 || disp.vs[i] == v) return;
    disp.vs[i] = v;
    disp.scroll_dirty = 1;
    disp.why[24]++;
}

uint8_t disp_byte(uint16_t a)
{
    uint32_t row = disp.tile[a >> 5][(a >> 2) & 7];
    int x = (a & 3) * 2, b = 0;
    for (int k = 0; k < 4; k++) {
        int p = (int)(row >> (24 - 8 * k)) & 0xFF;
        b |= ((p >> (7 - x)) & 1) << (4 + k) | ((p >> (6 - x)) & 1) << k;
    }
    return (uint8_t)b;
}
uint16_t disp_word(uint16_t a) { return (uint16_t)(disp_byte(a) << 8 | disp_byte((uint16_t)(a + 1))); }

/* a byte arrives: into its tile, into the sprite list when it is one of an entry's first four, and
 * into each cell map whose table holds its address (the other byte of the cell from the tile cache) */
static void put(uint16_t a, uint8_t b)
{
    struct disp *d = &disp;
    uint32_t *row = &d->tile[a >> 5][(a >> 2) & 7];
    int x = (a & 3) * 2;
    uint32_t now = (*row & ~(0xC0C0C0C0u >> x)) | (c2p[b] >> x);
    uint32_t off = (uint16_t)(a - d->sprites);            /* the sprite table: each entry's first four bytes are latched */
    if (off < d->sprites_size && (off & 7) < 4 && d->sat[off] != b) { d->sat[off] = b; d->sprites_dirty = 1; }   /* (even when VRAM already holds the byte) */
    if (now == *row) return;                              /* the same byte again changes nothing else */
    *row = now;
    d->tile_dirty[a >> 8] |= (uint8_t)(1 << ((a >> 5) & 7));
    d->any_tile_dirty = 1;
    if (off < d->sprites_size) { d->spr[off] = b; d->sprites_dirty = 1; }
    uint32_t hoff = (uint16_t)(a - d->hscroll);           /* the horizontal scroll table */
    if (hoff < 0x380) {
        d->hsc[hoff] = b;
        d->scroll_dirty = 1;
        d->why[25]++;
    }
    int table = 0;
    for (int k = 0; k < 3; k++) {
        uint32_t o = (uint16_t)(a - d->base[k]);          /* (a table past the top of VRAM goes on from address 0) */
        if (o >= d->size[k]) continue;
        uint16_t *c = &d->cell[k][o >> 1];
        d->uses[*c & 0x7FF]--;
        *c = (a & 1) ? (uint16_t)((*c & 0xFF00) | b) : (uint16_t)((*c & 0x00FF) | b << 8);
        d->uses[*c & 0x7FF]++;
        d->cell_dirty[k][o >> 4] |= (uint8_t)(1 << ((o >> 1) & 7));
        d->any_cell_dirty = 1;
        d->plane_dirty[k] = 1;
        table = 1;
    }
    if (table) d->cells_written++; else d->tile_bytes_written++;
}

/* a word of a tile's pixels outside every table (most writes: tile loads, the VRAM clear): four
 * pixels into the planar row, and the tile marked when they changed */
static inline void tile_word(uint16_t a, uint16_t w)
{
    uint32_t *row = &disp.tile[a >> 5][(a >> 2) & 7];
    int x = (a & 2) * 2;
    uint32_t now = (*row & ~(0xF0F0F0F0u >> x)) | ((c2p[w >> 8] | c2p[w & 0xFF] >> 2) >> x);
    if (now == *row) return;
    *row = now;
    disp.tile_dirty[a >> 8] |= (uint8_t)(1 << ((a >> 5) & 7));
    disp.any_tile_dirty = 1;
    disp.tile_bytes_written += 2;
}

void disp_vram_byte(uint16_t a, uint8_t b) { put(a, b); }
/* a word in the sprite table only: the tile cache, the table's bytes and the latched entries */
static void sprite_word(uint16_t a, uint16_t w)
{
    struct disp *d = &disp;
    uint32_t off = (uint16_t)(a - d->sprites);
    if (off < d->sprites_size) {
        uint8_t hi = (uint8_t)(w >> 8), lo = (uint8_t)w;
        if ((off & 7) < 4 && (d->sat[off] != hi || d->sat[off + 1] != lo)) { d->sat[off] = hi; d->sat[off + 1] = lo; d->sprites_dirty = 1; }
        if (d->spr[off] != hi || d->spr[off + 1] != lo) { d->spr[off] = hi; d->spr[off + 1] = lo; d->sprites_dirty = 1; }
    }
    tile_word(a, w);
}

void disp_vram_word(uint16_t a, uint16_t w)
{
    if (!(a & 1)) {
        uint8_t k = disp.special[a >> 6];
        if (!k) { tile_word(a, w); return; }
        if (k == 2) { sprite_word(a, w); return; }
    }
    put(a, (uint8_t)(w >> 8));
    put((uint16_t)(a + 1), (uint8_t)w);
}
void disp_vram_long(uint16_t a, uint32_t v) { disp_vram_word(a, (uint16_t)(v >> 16)); disp_vram_word((uint16_t)(a + 2), (uint16_t)v); }
void disp_vram_block(uint16_t a, const uint8_t *src, uint32_t words)
{
    if (a & 1) { for (uint32_t k = 0; k < 2 * words; k++) put((uint16_t)(a + k), src[k]); return; }
    for (; words; words--, src += 2, a = (uint16_t)(a + 2)) {
        uint8_t k = disp.special[a >> 6];
        if (!k) tile_word(a, (uint16_t)(src[0] << 8 | src[1]));
        else if (k == 2) sprite_word(a, (uint16_t)(src[0] << 8 | src[1]));
        else { put(a, src[0]); put((uint16_t)(a + 1), src[1]); }
    }
}
void disp_vram_fill(uint16_t a, uint8_t b, uint32_t n, uint8_t step)
{
    while (n--) { put(a ^ 1, b); a = (uint16_t)(a + step); }
}
void disp_vram_copy(uint16_t a, uint16_t src, uint32_t n, uint8_t step)
{
    while (n--) { put(a ^ 1, disp_byte(src ^ 1)); src++; a = (uint16_t)(a + step); }
}
