/* The drawing side of the display backend: see disp_draw.h. This first version is the plain one:
 * it finds the screen cells that changed (a cell written under them, a tile's pixels, a sprite
 * moved, or everything when the layout or scroll changed) and draws their lines pixel by pixel from
 * disp_core.c's lists by the VDP's layer rules. Its job is to fix what "up to date" means and to be
 * checked against the reference on every frame (amiga/tools/halcheck.c); the 68020's fast paths
 * (cells that line up with the screen's bytes, blank cells, the blitter for objects) replace parts of
 * it one at a time under the same check. */
#include <string.h>
#include "disp_core.h"
#include "disp_draw.h"

struct draw draw;
#define CW 40
#define CH 28
static uint8_t dirty[CH][CW];
static struct { int16_t x, y, w, h; } box[2][80];        /* the sprites' places on screen: this update's and the last's */
static int boxes[2], first = 1;

void draw_reset(void)
{
    memset(&draw, 0, sizeof draw);
    memset(dirty, 0, sizeof dirty);
    boxes[0] = boxes[1] = 0;
    first = 1;
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

/* a tile's colour at x, y of the cell entry e (flips applied) */
static int pix(unsigned e, int x, int y)
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
 * are part of the picture */
static void sprite_line(int line, struct layer *o)
{
    int width = disp.width, h40 = width == 320, total = h40 ? 80 : 64, per_line = h40 ? 20 : 16, dots = h40 ? 320 : 256;
    memset(o->col, 0, sizeof o->col);
    int link = 0, seen = 0, shown = 0, masked = 0;
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
            }
            if (masked || dots <= 0) break;
        }
        if (!next || next >= total) break;
        link = next;
    }
}

/* a line's colour numbers (palette line << 4 | colour; 0 the backdrop): from back to front B low,
 * A low, sprites low, B high, A high, sprites high; the window stands in for plane A in its area */
static void compose_line(int line, uint8_t *out)
{
    int width = disp.width, w0, w1;
    struct layer a, b, s;
    window_span(line, &w0, &w1);
    plane_line(line, 1, hscroll_of(line, 1), 0, &b);
    plane_line(line, 0, hscroll_of(line, 0), (w0 == 0 && w1 > 0 && w1 < width) ? w1 : 0, &a);
    for (int x = w0; x < w1; x++) {
        unsigned e = disp.cell[2][(line >> 3) * disp.win_w + (x >> 3)];
        int c = pix(e, x & 7, line & 7);
        a.col[x] = (uint8_t)(c ? (((e >> 13) & 3) << 4) | c : 0);
        a.pri[x] = (uint8_t)(e >> 15);
    }
    sprite_line(line, &s);
    for (int x = 0; x < width; x++) {
        uint8_t c = 0;
        if (s.col[x] && s.pri[x]) c = s.col[x];
        else if (a.col[x] && a.pri[x]) c = a.col[x];
        else if (b.col[x] && b.pri[x]) c = b.col[x];
        else if (s.col[x]) c = s.col[x];
        else if (a.col[x]) c = a.col[x];
        else if (b.col[x]) c = b.col[x];
        if (x < 8 && (disp.reg[0] & 0x20)) c = 0;         /* the left column blanked */
        out[x] = c;
    }
}

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
    /* the planes' cells that were written: the screen cells they lie under, by the scroll now */
    if (!all && d->any_cell_dirty) {
        if (!whole) all = 1;
        else {
            for (int k = 0; k < 2; k++) {
                int hs = hscroll_of(0, k), vs = d->vs[k], pw = d->plane_w * 8, ph = d->plane_h * 8;
                for (int i = 0; i < d->plane_w * d->plane_h; i++) {
                    if (!(d->cell_dirty[k][i >> 3] >> (i & 7) & 1)) continue;
                    int x = ((i % d->plane_w) * 8 + hs) & (pw - 1), y = ((i / d->plane_w) * 8 - vs) & (ph - 1);
                    for (int wy = 0; wy < 2; wy++) for (int wx = 0; wx < 2; wx++) mark(x - wx * pw, y - wy * ph, 8, 8);   /* (and where the plane wraps) */
                }
            }
            for (int i = 0; i < d->win_w * 32; i++)
                if (d->cell_dirty[2][i >> 3] >> (i & 7) & 1) mark((i % d->win_w) * 8, (i / d->win_w) * 8, 8, 8);
        }
    }
    /* the tiles whose pixels changed: the screen cells that show one */
    if (!all && d->any_tile_dirty) {
        if (!whole) all = 1;
        else {
            for (int k = 0; k < 2; k++) {
                int hs = hscroll_of(0, k), vs = d->vs[k], pw = d->plane_w * 8, ph = d->plane_h * 8;
                for (int i = 0; i < d->plane_w * d->plane_h; i++) {
                    if (!tile_is_dirty(d->cell[k][i])) continue;
                    int x = ((i % d->plane_w) * 8 + hs) & (pw - 1), y = ((i / d->plane_w) * 8 - vs) & (ph - 1);
                    for (int wy = 0; wy < 2; wy++) for (int wx = 0; wx < 2; wx++) mark(x - wx * pw, y - wy * ph, 8, 8);
                }
            }
            for (int cy = 0; cy < CH; cy++) for (int cx = 0; cx < cw; cx++) if (tile_is_dirty(d->cell[2][cy * d->win_w + cx])) dirty[cy][cx] = 1;
        }
    }
    if (all) { memset(dirty, 1, sizeof dirty); draw.full_updates++; }
    /* draw the lines of the dirty cells */
    uint8_t line[320];
    for (int cy = 0; cy < CH; cy++) {
        int any = 0;
        for (int cx = 0; cx < cw; cx++) any |= dirty[cy][cx];
        if (!any) continue;
        for (int y = cy * 8; y < cy * 8 + 8; y++) {
            compose_line(y, line);
            for (int cx = 0; cx < cw; cx++) {
                if (!dirty[cy][cx]) continue;
                for (int k = 0; k < 6; k++) {
                    int b = 0;
                    for (int x = 0; x < 8; x++) b |= ((line[cx * 8 + x] >> k) & 1) << (7 - x);
                    draw.plane[k][y * DRAW_STRIDE + cx] = (uint8_t)b;
                }
            }
        }
        for (int cx = 0; cx < cw; cx++) { draw.cells_drawn += dirty[cy][cx]; dirty[cy][cx] = 0; }
    }
    memset(d->tile_dirty, 0, sizeof d->tile_dirty);
    memset(d->cell_dirty, 0, sizeof d->cell_dirty);
    d->any_tile_dirty = d->any_cell_dirty = d->layout_dirty = d->sprites_dirty = 0;
    first = 0;
}
