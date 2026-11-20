/* The drawing side of the display backend: see disp_draw.h.
 *
 * What is redrawn: the 8x8 screen cells something changed under (a cell written beneath them,
 * mapped through the scroll; a tile whose pixels changed; a sprite that moved or changed, at its old
 * and new places), or the whole screen when the picture is moved or scrolled.
 *
 * How: a line's eight pixels at a time, in planar form. Each plane's eight pixels come from at most
 * two cells of its map (its tile row as four bitplane bytes, shifted by the fine scroll), with an
 * opaque mask, its palette line as two more planes and its priority as a mask; the window stands in
 * for plane A in its columns; the sprites' pixels on the line are gathered first. The layers are then
 * chosen per pixel by masks, in the VDP's order: sprites high, A high, B high, sprites low, A low, B
 * low. Lines the VDP draws in a way this does not cover (vertical scroll by columns, the window's
 * left-edge quirk with a fine scroll) are drawn a pixel at a time by the same rules. Gate:
 * amiga/tools/halcheck.c turns the planes back into colours and compares every frame of the golden
 * runs with the reference. */
#include <string.h>
#include "disp_core.h"
#include "disp_draw.h"

struct draw draw;
#define CW 40
#define CH 28
static uint8_t dirty[CH][CW];
static struct { int16_t x, y, w, h; } box[2][80];        /* the sprites' places on screen: this update's and the last's */
static int boxes[2], first = 1;
static uint8_t rev[256];                                  /* a byte's bits in the other order (a horizontally flipped tile) */
static uint32_t hi_mask[8], lo_mask[8];                   /* per byte: the bits a shift by s keeps, from the first cell and the second */

void draw_reset(uint8_t *const planes[6])
{
    memset(&draw, 0, sizeof draw);
    for (int k = 0; k < 6; k++) { draw.plane[k] = planes[k]; memset(planes[k], 0, DRAW_PLANE_BYTES); }
    memset(dirty, 0, sizeof dirty);
    boxes[0] = boxes[1] = 0;
    first = 1;
    for (int b = 0; b < 256; b++) {
        int r = 0;
        for (int i = 0; i < 8; i++) r |= ((b >> i) & 1) << (7 - i);
        rev[b] = (uint8_t)r;
    }
    for (int s = 0; s < 8; s++) {
        hi_mask[s] = ((0xFFu << s) & 0xFF) * 0x01010101u;
        lo_mask[s] = s ? (0xFFu >> (8 - s)) * 0x01010101u : 0;
    }
}

static int tile_is_dirty(unsigned t) { t &= 0x7FF; return disp.tile_dirty[t >> 3] >> (t & 7) & 1; }

static void mark(int x, int y, int w, int h)              /* the screen cells a box of pixels touches */
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0 || x >= disp.width || y >= DRAW_LINES) return;
    int x1 = (x + w - 1) >> 3, y1 = (y + h - 1) >> 3;
    if (x1 >= disp.width / 8) x1 = disp.width / 8 - 1;
    if (y1 >= CH) y1 = CH - 1;
    for (int cy = y >> 3; cy <= y1; cy++) for (int cx = x >> 3; cx <= x1; cx++) dirty[cy][cx] = 1;
}

/* ---- a pixel at a time (the lines the planar path does not cover) ---------------------------- */

static int pix(unsigned e, int x, int y)                  /* cell entry e: its tile's colour at x, y (flips applied) */
{
    if (e & 0x0800) x = 7 - x;
    if (e & 0x1000) y = 7 - y;
    uint32_t row = disp.tile[e & 0x7FF][y];
    int c = 0;
    for (int k = 0; k < 4; k++) c |= (int)((row >> (24 - 8 * k + 7 - x)) & 1) << k;
    return c;
}

struct layer { uint8_t col[320], pri[320]; };

static int hscroll_of(int line, int k)
{
    int hl = (disp.reg[11] & 3) == 2 ? (line & ~7) : (disp.reg[11] & 3) == 3 ? line : 0;
    return disp_word((uint16_t)(disp.hscroll + (uint32_t)(hl << 2) + 2 * (uint32_t)k)) & 0x3FF;
}

static void window_span(int line, int *x0, int *x1)
{
    int width = disp.width, wy = (disp.reg[18] & 0x1F) * 8;
    *x0 = *x1 = 0;
    if ((disp.reg[18] & 0x80) ? line >= wy : line < wy) { *x1 = width; return; }
    int wx = (disp.reg[17] & 0x1F) * 16;
    if (wx > width) wx = width;
    if (disp.reg[17] & 0x80) { *x0 = wx; *x1 = width; } else *x1 = wx;
}

static void plane_line(int line, int k, int hscroll, int bug_x, struct layer *o)
{
    int w = disp.plane_w, h = disp.plane_h, width = disp.width, fine = hscroll & 15;
    int left = width == 320 ? (disp.vs[38] & disp.vs[39]) : 0;
    for (int x = 0; x < width; x++) {
        int vs = disp.vs[k];
        if (disp.reg[11] & 4) vs = x < fine ? left : disp.vs[(((x - fine) >> 4) * 2 + k) % 40];
        int py = (line + vs) & (h * 8 - 1);
        int px = (x - hscroll + (bug_x && x >= bug_x && x < bug_x + fine ? 16 : 0)) & (w * 8 - 1);
        unsigned e = disp.cell[k][(py >> 3) * w + (px >> 3)];
        int c = pix(e, px & 7, py & 7);
        o->col[x] = (uint8_t)(c ? (((e >> 13) & 3) << 4) | c : 0);
        o->pri[x] = (uint8_t)(e >> 15);
    }
}

/* the sprites on a line, in link order; the VDP's limits a line (sprites, pixels, the mask at X 0)
 * are part of the picture. -> whether any pixel was drawn; any[b]: a pixel in byte b */
static int sprite_line(int line, struct layer *o, uint8_t *any)
{
    int width = disp.width, h40 = width == 320, total = h40 ? 80 : 64, per_line = h40 ? 20 : 16, dots = h40 ? 320 : 256;
    int link = 0, seen = 0, shown = 0, masked = 0, drawn = 0;
    memset(o->col, 0, (size_t)width);
    memset(any, 0, (size_t)width / 8);
    while (seen++ < total) {
        const uint8_t *c = disp.sat + link * 8;
        int y = ((c[0] << 8 | c[1]) & 0x3FF) - 128, sh = (c[2] & 3) + 1, sw = ((c[2] >> 2) & 3) + 1, next = c[3] & 0x7F;
        if (line >= y && line < y + sh * 8) {
            if (++shown > per_line) break;
            uint16_t at = (uint16_t)(disp.sprites + (uint32_t)(link << 3));
            unsigned e = disp_word((uint16_t)(at + 4));
            int rawx = disp_word((uint16_t)(at + 6)) & 0x1FF, x = rawx - 128;
            if (rawx == 0 && shown > 1) masked = 1;
            int row = line - y;
            if (e & 0x1000) row = sh * 8 - 1 - row;
            for (int cx = 0; cx < sw * 8; cx++) {
                if (dots-- <= 0) break;
                int sx = x + cx;
                if (masked || sx < 0 || sx >= width || o->col[sx]) continue;
                int tx = (e & 0x0800) ? sw * 8 - 1 - cx : cx;
                unsigned t = ((e & 0x7FF) + (unsigned)((tx >> 3) * sh + (row >> 3))) & 0x7FF;
                int p = pix(t, tx & 7, row & 7);
                if (!p) continue;
                o->col[sx] = (uint8_t)((((e >> 13) & 3) << 4) | p);
                o->pri[sx] = (uint8_t)(e >> 15);
                any[sx >> 3] = 1;
                drawn = 1;
            }
            if (masked || dots <= 0) break;
        }
        if (!next || next >= total) break;
        link = next;
    }
    return drawn;
}

/* a line's colour numbers a pixel at a time (palette line << 4 | colour; 0 the backdrop) */
static void compose_line_slow(int line, uint8_t *out)
{
    int width = disp.width, w0, w1;
    static struct layer a, b, s;
    static uint8_t any[40];
    window_span(line, &w0, &w1);
    plane_line(line, 1, hscroll_of(line, 1), 0, &b);
    plane_line(line, 0, hscroll_of(line, 0), (w0 == 0 && w1 > 0 && w1 < width) ? w1 : 0, &a);
    for (int x = w0; x < w1; x++) {
        unsigned e = disp.cell[2][(line >> 3) * disp.win_w + (x >> 3)];
        int c = pix(e, x & 7, line & 7);
        a.col[x] = (uint8_t)(c ? (((e >> 13) & 3) << 4) | c : 0);
        a.pri[x] = (uint8_t)(e >> 15);
    }
    sprite_line(line, &s, any);
    for (int x = 0; x < width; x++) {
        uint8_t c = 0;
        if (s.col[x] && s.pri[x]) c = s.col[x];
        else if (a.col[x] && a.pri[x]) c = a.col[x];
        else if (b.col[x] && b.pri[x]) c = b.col[x];
        else if (s.col[x]) c = s.col[x];
        else if (a.col[x]) c = a.col[x];
        else if (b.col[x]) c = b.col[x];
        if (x < 8 && (disp.reg[0] & 0x20)) c = 0;
        out[x] = c;
    }
}

/* ---- eight pixels at a time, in planar form -------------------------------------------------- */

struct lb { uint32_t c; uint32_t o, p4, p5, pr; };        /* c: four colour planes (byte k plane k, from the top); masks in the low byte */

static inline uint32_t tile_row(unsigned e, int r)
{
    uint32_t v = disp.tile[e & 0x7FF][(e & 0x1000) ? 7 - r : r];
    if (e & 0x0800) v = (uint32_t)rev[v >> 24] << 24 | (uint32_t)rev[(v >> 16) & 0xFF] << 16 | (uint32_t)rev[(v >> 8) & 0xFF] << 8 | rev[v & 0xFF];
    return v;
}
static inline uint32_t opaque(uint32_t v) { v |= v >> 16; v |= v >> 8; return v & 0xFF; }

/* eight pixels of a cell map from plane pixel px0 on: the cells at px0 and after it in row `row` */
static inline void fetch(const uint16_t *row, int cells_mask, int px0, int r, struct lb *o)
{
    int s = px0 & 7, c0 = px0 >> 3;
    unsigned e0 = row[c0];
    uint32_t v0 = tile_row(e0, r), o0 = opaque(v0);
    if (!s) {
        o->c = v0;
        o->o = o0;
        o->p4 = (e0 & 0x2000) ? o0 : 0;
        o->p5 = (e0 & 0x4000) ? o0 : 0;
        o->pr = (e0 & 0x8000) ? 0xFF : 0;
        return;
    }
    unsigned e1 = row[(c0 + 1) & cells_mask];
    uint32_t v1 = tile_row(e1, r), o1 = opaque(v1);
    uint32_t m0 = (o0 << s) & 0xFF, m1 = o1 >> (8 - s);
    o->c = ((v0 << s) & hi_mask[s]) | ((v1 >> (8 - s)) & lo_mask[s]);
    o->o = m0 | m1;
    o->p4 = ((e0 & 0x2000) ? m0 : 0) | ((e1 & 0x2000) ? m1 : 0);
    o->p5 = ((e0 & 0x4000) ? m0 : 0) | ((e1 & 0x4000) ? m1 : 0);
    o->pr = ((e0 & 0x8000) ? (0xFFu << s) & 0xFF : 0) | ((e1 & 0x8000) ? 0xFFu >> (8 - s) : 0);
}

/* the sprite layer's eight pixels at byte b of the line, from the pixels sprite_line gathered */
static inline void gather(const struct layer *s, int b, struct lb *o)
{
    uint32_t c = 0, op = 0, p4 = 0, p5 = 0, pr = 0;
    for (int i = 0; i < 8; i++) {
        unsigned v = s->col[b * 8 + i];
        if (!v) continue;
        uint32_t bit = 0x80u >> i;
        op |= bit;
        if (v & 1) c |= bit << 24;
        if (v & 2) c |= bit << 16;
        if (v & 4) c |= bit << 8;
        if (v & 8) c |= bit;
        if (v & 16) p4 |= bit;
        if (v & 32) p5 |= bit;
        if (s->pri[b * 8 + i]) pr |= bit;
    }
    o->c = c; o->o = op; o->p4 = p4; o->p5 = p5; o->pr = pr;
}

static inline void take(const struct lb *l, uint32_t m, uint32_t *c, uint32_t *p4, uint32_t *p5)
{
    *c |= l->c & (m * 0x01010101u);
    *p4 |= l->p4 & m;
    *p5 |= l->p5 & m;
}

/* ---- the update ------------------------------------------------------------------------------ */

/* the sprites' boxes on screen now, and whether any uses a tile whose pixels changed */
static int sprite_boxes(int which)
{
    int h40 = disp.width == 320, total = h40 ? 80 : 64, link = 0, seen = 0, n = 0, tiles_changed = 0;
    while (seen++ < total) {
        const uint8_t *c = disp.sat + link * 8;
        int y = ((c[0] << 8 | c[1]) & 0x3FF) - 128, sh = (c[2] & 3) + 1, sw = ((c[2] >> 2) & 3) + 1, next = c[3] & 0x7F;
        uint16_t at = (uint16_t)(disp.sprites + (uint32_t)(link << 3));
        unsigned e = disp_word((uint16_t)(at + 4));
        int x = (disp_word((uint16_t)(at + 6)) & 0x1FF) - 128;
        if (y < DRAW_LINES && y + sh * 8 > 0 && x < disp.width && x + sw * 8 > 0) {
            box[which][n].x = (int16_t)x; box[which][n].y = (int16_t)y;
            box[which][n].w = (int16_t)(sw * 8); box[which][n].h = (int16_t)(sh * 8);
            n++;
            if (disp.any_tile_dirty) for (int t = 0; t < sw * sh; t++) tiles_changed |= tile_is_dirty((e & 0x7FF) + (unsigned)t);
        }
        if (!next || next >= total) break;
        link = next;
    }
    boxes[which] = n;
    return tiles_changed;
}

static void draw_line(int y, const uint8_t *want, int cw)
{
    struct disp *d = &disp;
    int width = d->width, w0, w1, pw = d->plane_w, pmask = pw * 8 - 1, vmask = d->plane_h * 8 - 1;
    window_span(y, &w0, &w1);
    int hsa = hscroll_of(y, 0), hsb = hscroll_of(y, 1);
    int slow = (d->reg[11] & 4) || (w0 == 0 && w1 > 0 && w1 < width && (hsa & 15));
    static struct layer s;
    static uint8_t any[40];
    int sprites = 0;
    for (int i = 0; i < boxes[1] && !sprites; i++) sprites = y >= box[1][i].y && y < box[1][i].y + box[1][i].h;
    if (sprites && !slow) sprites = sprite_line(y, &s, any);
    uint32_t off = (uint32_t)y * DRAW_STRIDE;
    if (slow) {
        static uint8_t line[320];
        compose_line_slow(y, line);
        for (int bx = 0; bx < cw; bx++) {
            if (!want[bx]) continue;
            for (int k = 0; k < 6; k++) {
                int b = 0;
                for (int x = 0; x < 8; x++) b |= ((line[bx * 8 + x] >> k) & 1) << (7 - x);
                draw.plane[k][off + (uint32_t)bx] = (uint8_t)b;
            }
            draw.bytes_slow++;
        }
        return;
    }
    int pya = (y + d->vs[0]) & vmask, pyb = (y + d->vs[1]) & vmask;
    const uint16_t *rowa = &d->cell[0][(pya >> 3) * pw], *rowb = &d->cell[1][(pyb >> 3) * pw];
    const uint16_t *roww = &d->cell[2][(y >> 3) * d->win_w];
    int lcb = d->reg[0] & 0x20;
    for (int bx = 0; bx < cw; bx++) {
        if (!want[bx]) continue;
        struct lb a, b, sp;
        int x = bx * 8;
        if (x >= w0 && x < w1) fetch(roww, d->win_w - 1, x, y & 7, &a);
        else fetch(rowa, pw - 1, (x - hsa) & pmask, pya & 7, &a);
        fetch(rowb, pw - 1, (x - hsb) & pmask, pyb & 7, &b);
        uint32_t c = 0, p4 = 0, p5 = 0, sel = 0, m;
        if (sprites && any[bx]) {
            gather(&s, bx, &sp);
            m = sp.o & sp.pr;          take(&sp, m, &c, &p4, &p5); sel = m;
            m = a.o & a.pr & ~sel;     take(&a, m, &c, &p4, &p5); sel |= m;
            m = b.o & b.pr & ~sel;     take(&b, m, &c, &p4, &p5); sel |= m;
            m = sp.o & ~sp.pr & ~sel;  take(&sp, m, &c, &p4, &p5); sel |= m;
        } else {
            m = a.o & a.pr;            take(&a, m, &c, &p4, &p5); sel = m;
            m = b.o & b.pr & ~sel;     take(&b, m, &c, &p4, &p5); sel |= m;
        }
        m = a.o & ~a.pr & ~sel & 0xFF; take(&a, m, &c, &p4, &p5); sel |= m;
        m = b.o & ~b.pr & ~sel & 0xFF; take(&b, m, &c, &p4, &p5);
        if (!bx && lcb) c = p4 = p5 = 0;                  /* the left column blanked */
        draw.plane[0][off + (uint32_t)bx] = (uint8_t)(c >> 24);
        draw.plane[1][off + (uint32_t)bx] = (uint8_t)(c >> 16);
        draw.plane[2][off + (uint32_t)bx] = (uint8_t)(c >> 8);
        draw.plane[3][off + (uint32_t)bx] = (uint8_t)c;
        draw.plane[4][off + (uint32_t)bx] = (uint8_t)p4;
        draw.plane[5][off + (uint32_t)bx] = (uint8_t)p5;
        draw.bytes_fast++;
    }
}

void draw_update(void)
{
    struct disp *d = &disp;
    int cw = d->width / 8, all = first || d->layout_dirty;
    int whole = !(d->reg[11] & 7);                        /* each plane scrolls as one piece */
    draw.updates++;
    /* the sprites: where they were and where they are, when anything about them changed */
    int sprite_tiles = sprite_boxes(1);
    if (!all && (d->sprites_dirty || sprite_tiles || boxes[0] || boxes[1])) {
        int same = boxes[0] == boxes[1] && !d->sprites_dirty && !sprite_tiles && !memcmp(box[0], box[1], sizeof box[0][0] * (size_t)boxes[0]);
        if (!same) for (int w = 0; w < 2; w++) for (int i = 0; i < boxes[w]; i++) mark(box[w][i].x, box[w][i].y, box[w][i].w, box[w][i].h);
    }
    memcpy(box[0], box[1], sizeof box[0]);
    boxes[0] = boxes[1];
    /* the planes' cells that were written, and the cells that show a tile whose pixels changed: the
     * screen cells they lie under, by the scroll now (and where the plane wraps) */
    if (!all && (d->any_cell_dirty || d->any_tile_dirty)) {
        if (!whole) all = 1;
        else {
            for (int k = 0; k < 2; k++) {
                int hs = hscroll_of(0, k), vs = d->vs[k], pw = d->plane_w * 8, ph = d->plane_h * 8;
                for (int i = 0; i < d->plane_w * d->plane_h; i++) {
                    if (!((d->any_cell_dirty && (d->cell_dirty[k][i >> 3] >> (i & 7) & 1)) || (d->any_tile_dirty && tile_is_dirty(d->cell[k][i])))) continue;
                    int x = ((i % d->plane_w) * 8 + hs) & (pw - 1), y = ((i / d->plane_w) * 8 - vs) & (ph - 1);
                    for (int wy = 0; wy < 2; wy++) for (int wx = 0; wx < 2; wx++) mark(x - wx * pw, y - wy * ph, 8, 8);
                }
            }
            for (int i = 0; i < d->win_w * 32; i++)
                if ((d->any_cell_dirty && (d->cell_dirty[2][i >> 3] >> (i & 7) & 1)) || (d->any_tile_dirty && tile_is_dirty(d->cell[2][i])))
                    mark((i % d->win_w) * 8, (i / d->win_w) * 8, 8, 8);
        }
    }
    if (all) { memset(dirty, 1, sizeof dirty); draw.full_updates++; }
    for (int cy = 0; cy < CH; cy++) {
        int any = 0;
        for (int cx = 0; cx < cw; cx++) any |= dirty[cy][cx];
        if (!any) continue;
        for (int y = cy * 8; y < cy * 8 + 8; y++) draw_line(y, dirty[cy], cw);
        for (int cx = 0; cx < cw; cx++) { draw.cells_drawn += dirty[cy][cx]; dirty[cy][cx] = 0; }
    }
    memset(d->tile_dirty, 0, sizeof d->tile_dirty);
    memset(d->cell_dirty, 0, sizeof d->cell_dirty);
    d->any_tile_dirty = d->any_cell_dirty = d->layout_dirty = d->sprites_dirty = 0;
    first = 0;
}
