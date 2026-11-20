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
static uint8_t dirty[DRAW_LINES][CW];                    /* the line bytes to draw again */
static uint8_t line_dirty[DRAW_LINES];                   /* a byte of the line is marked */
static uint16_t last_hs[DRAW_LINES][2], last_vs[2];      /* the scroll each line was drawn with */
static struct { int16_t x, y, w, h; } box[2][80];        /* the sprites' places on screen: this update's and the last's */
static int boxes[2], first = 1;
static uint8_t rev[256];                                  /* a byte's bits in the other order (a horizontally flipped tile) */
static uint32_t hi_mask[8], lo_mask[8];                   /* per byte: the bits a shift by s keeps, from the first cell and the second */

void draw_reset(uint8_t *const planes[6])
{
    memset(&draw, 0, sizeof draw);
    for (int k = 0; k < 6; k++) { draw.plane[k] = planes[k]; memset(planes[k], 0, DRAW_PLANE_BYTES); }
    memset(dirty, 0, sizeof dirty);
    memset(line_dirty, 0, sizeof line_dirty);
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

static void mark_span(int y, int x, int w)               /* the line bytes pixels x to x + w - 1 of line y lie in */
{
    if (x < 0) { w += x; x = 0; }
    if (w <= 0 || x >= disp.width || y < 0 || y >= DRAW_LINES) return;
    int x1 = (x + w - 1) >> 3;
    if (x1 >= disp.width / 8) x1 = disp.width / 8 - 1;
    for (int b = x >> 3; b <= x1; b++) dirty[y][b] = 1;
    line_dirty[y] = 1;
}
static void mark(int x, int y, int w, int h)              /* the line bytes a box of pixels touches */
{
    for (int l = y < 0 ? 0 : y; l < y + h && l < DRAW_LINES; l++) mark_span(l, x, w);
}

/* a cell of plane k (index i in its map) was written or shows a changed tile: where it lies on each
 * of the lines it shows on, by each line's scroll (and where the plane wraps) */
static void mark_plane_cell(int k, int i)
{
    int pw = disp.plane_w * 8, vmask = disp.plane_h * 8 - 1, cx = (i % disp.plane_w) * 8, cy = (i / disp.plane_w) * 8;
    for (int r = 0; r < 8; r++) {
        int y = (cy + r - disp.vs[k]) & vmask;
        if (y >= DRAW_LINES) continue;
        int x = (cx + disp_hscroll(y, k)) & (pw - 1);
        mark_span(y, x, 8);
        mark_span(y, x - pw, 8);
    }
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

static int hscroll_of(int line, int k) { return disp_hscroll(line, k); }

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
            const uint8_t *q = disp.spr + link * 8;
            unsigned e = (unsigned)(q[4] << 8 | q[5]);
            int rawx = (q[6] << 8 | q[7]) & 0x1FF, x = rawx - 128;
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
        const uint8_t *q = disp.spr + link * 8;
        unsigned e = (unsigned)(q[4] << 8 | q[5]);
        int x = ((q[6] << 8 | q[7]) & 0x1FF) - 128;
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

static void draw_line_to(int y, const uint8_t *want, int cw, uint8_t *const *out, uint32_t off, int any_sprites);
static void draw_line(int y, const uint8_t *want, int cw)
{
    int sprites = 0;
    for (int i = 0; i < boxes[1] && !sprites; i++) sprites = y >= box[1][i].y && y < box[1][i].y + box[1][i].h;
    draw_line_to(y, want, cw, draw.plane, (uint32_t)y * DRAW_STRIDE, sprites);
}

/* a whole line, composed eight pixels at a time, into six planes of the caller's (disp_obj.c's
 * lines where the VDP's sprite limits apply) */
void disp_planar_line(int y, uint8_t *const out[6])
{
    static const uint8_t all[40] = { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };
    draw_line_to(y, all, disp.width / 8, out, 0, 1);
}

static void draw_line_to(int y, const uint8_t *want, int cw, uint8_t *const *out, uint32_t off, int any_sprites)
{
    struct disp *d = &disp;
    int width = d->width, w0, w1, pw = d->plane_w, pmask = pw * 8 - 1, vmask = d->plane_h * 8 - 1;
    window_span(y, &w0, &w1);
    int hsa = hscroll_of(y, 0), hsb = hscroll_of(y, 1);
    int slow = (d->reg[11] & 4) || (w0 == 0 && w1 > 0 && w1 < width && (hsa & 15));
    static struct layer s;
    static uint8_t any[40];
    int sprites = any_sprites;
    if (sprites && !slow) sprites = sprite_line(y, &s, any);
    if (slow) {
        static uint8_t line[320];
        compose_line_slow(y, line);
        for (int bx = 0; bx < cw; bx++) {
            if (!want[bx]) continue;
            for (int k = 0; k < 6; k++) {
                int b = 0;
                for (int x = 0; x < 8; x++) b |= ((line[bx * 8 + x] >> k) & 1) << (7 - x);
                out[k][off + (uint32_t)bx] = (uint8_t)b;
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
        out[0][off + (uint32_t)bx] = (uint8_t)(c >> 24);
        out[1][off + (uint32_t)bx] = (uint8_t)(c >> 16);
        out[2][off + (uint32_t)bx] = (uint8_t)(c >> 8);
        out[3][off + (uint32_t)bx] = (uint8_t)c;
        out[4][off + (uint32_t)bx] = (uint8_t)p4;
        out[5][off + (uint32_t)bx] = (uint8_t)p5;
        draw.bytes_fast++;
    }
}

/* a tile whose pixels changed and that some cell of A, B or the window names */
static int used_tile_dirty(void)
{
    for (int i = 0; i < DISP_TILES / 8; i++) {
        unsigned m = disp.tile_dirty[i];
        for (int j = 0; m; j++, m >>= 1) if ((m & 1) && disp.uses[i * 8 + j]) return 1;
    }
    return 0;
}

void draw_invalidate(void) { first = 1; }
int disp_compose_line(int line, uint8_t *out) { compose_line_slow(line, out); return 0; }

void draw_update(void)
{
    struct disp *d = &disp;
    int cw = d->width / 8, all = first || d->layout_dirty || (d->reg[11] & 4);
    draw.updates++;
    /* the scroll: the lines whose scroll changed since they were drawn (a vertical scroll moves them all) */
    if (!all && d->scroll_dirty) {
        if (d->vs[0] != last_vs[0] || d->vs[1] != last_vs[1]) all = 1;
        else
            for (int y = 0; y < DRAW_LINES; y++)
                if (disp_hscroll(y, 0) != last_hs[y][0] || disp_hscroll(y, 1) != last_hs[y][1]) mark_span(y, 0, d->width);
    }
    if (all || d->scroll_dirty) {
        for (int y = 0; y < DRAW_LINES; y++) { last_hs[y][0] = disp_hscroll(y, 0); last_hs[y][1] = disp_hscroll(y, 1); }
        last_vs[0] = d->vs[0];
        last_vs[1] = d->vs[1];
    }
    /* the sprites: where they were and where they are, when anything about them changed */
    int sprite_tiles = sprite_boxes(1);
    if (!all && (d->sprites_dirty || sprite_tiles || boxes[0] || boxes[1])) {
        int same = boxes[0] == boxes[1] && !d->sprites_dirty && !sprite_tiles && !memcmp(box[0], box[1], sizeof box[0][0] * (size_t)boxes[0]);
        if (!same) for (int w = 0; w < 2; w++) for (int i = 0; i < boxes[w]; i++) mark(box[w][i].x, box[w][i].y, box[w][i].w, box[w][i].h);
    }
    memcpy(box[0], box[1], sizeof box[0]);
    boxes[0] = boxes[1];
    /* the cells that were written, and the cells that show a tile whose pixels changed */
    int tiles = !all && d->any_tile_dirty && used_tile_dirty();
    if (!all && (d->any_cell_dirty || tiles)) {
        for (int k = 0; k < 2; k++)
            for (int i = 0; i < d->plane_w * d->plane_h; i++)
                if ((d->any_cell_dirty && (d->cell_dirty[k][i >> 3] >> (i & 7) & 1)) || (tiles && tile_is_dirty(d->cell[k][i]))) mark_plane_cell(k, i);
        for (int i = 0; i < d->win_w * 32; i++)
            if ((d->any_cell_dirty && (d->cell_dirty[2][i >> 3] >> (i & 7) & 1)) || (tiles && tile_is_dirty(d->cell[2][i])))
                mark((i % d->win_w) * 8, (i / d->win_w) * 8, 8, 8);
    }
    if (all) { memset(dirty, 1, sizeof dirty); memset(line_dirty, 1, sizeof line_dirty); draw.full_updates++; }
    for (int y = 0; y < DRAW_LINES; y++) {
        if (!line_dirty[y]) continue;
        draw_line(y, dirty[y], cw);
        for (int b = 0; b < cw; b++) { draw.cells_drawn += dirty[y][b]; dirty[y][b] = 0; }
        line_dirty[y] = 0;
    }
    memset(d->tile_dirty, 0, sizeof d->tile_dirty);
    memset(d->cell_dirty, 0, sizeof d->cell_dirty);
    d->any_tile_dirty = d->any_cell_dirty = d->layout_dirty = d->sprites_dirty = d->scroll_dirty = 0;
    first = 0;
}
