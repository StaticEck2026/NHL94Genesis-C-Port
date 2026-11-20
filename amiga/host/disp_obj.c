/* The scrolled scene: see disp_obj.h and amiga/notes/p7_design.md. This first version does every
 * operation with the CPU, in C (the operations are the blitter's: copy, and cookie-cut through a
 * mask), so that it can be checked against the reference picture on the host frame by frame; the
 * Amiga's blitter takes them over afterwards under the same check. */
#include <string.h>
#include "disp_core.h"
#include "disp_obj.h"

struct obj obj;
#define SW 32                                             /* screen bytes a line (256 pixels) */
#define SH 224
#define MAXR 512
struct rect { int16_t x, y, w, h; };                     /* pixels, in the buffers' coordinates (y wraps at 256) */
static struct rect done[2][MAXR], chg[2][MAXR];
static int ndone[2], nchg[2], full[2], active;
static uint8_t a_img[SH][6][SW], a_lo[SH][SW], a_hi[SH][SW];   /* plane A and the window on screen, with their opaque masks by priority */
static int a_any_lo, a_any_hi;
static uint8_t rev[256];

void obj_reset(uint8_t *rink, uint8_t *rink_hi, uint8_t *buf0, uint8_t *buf1)
{
    memset(&obj, 0, sizeof obj);
    obj.rink = rink; obj.rink_hi = rink_hi; obj.buf[0] = buf0; obj.buf[1] = buf1;
    active = 0;
    for (int b = 0; b < 256; b++) {
        int r = 0;
        for (int i = 0; i < 8; i++) r |= ((b >> i) & 1) << (7 - i);
        rev[b] = (uint8_t)r;
    }
}

int obj_scene(void)
{
    const struct disp *d = &disp;
    if (d->width != 256 || (d->reg[11] & 7) || d->plane_w != 64 || d->plane_h != 32 || d->vs[0]) return 0;
    if (disp_hscroll(0, 0)) return 0;
    if (d->reg[0] & 0x20) return 0;                       /* (the left column blanked: not in a match) */
    return 1;
}

/* ---- pixels of tiles ------------------------------------------------------------------------- */

static inline uint32_t tile_row(unsigned e, int r)       /* four plane bytes (byte k plane k, from the top), flips applied */
{
    uint32_t v = disp.tile[e & 0x7FF][(e & 0x1000) ? 7 - r : r];
    if (e & 0x0800) v = (uint32_t)rev[v >> 24] << 24 | (uint32_t)rev[(v >> 16) & 0xFF] << 16 | (uint32_t)rev[(v >> 8) & 0xFF] << 8 | rev[v & 0xFF];
    return v;
}
static inline uint8_t opaque(uint32_t v) { v |= v >> 16; v |= v >> 8; return (uint8_t)v; }

/* a cell entry's row r as the six plane bytes of a colour number (palette line in planes 4 and 5) */
static inline void cell_row(unsigned e, int r, uint8_t *p6, uint8_t *mask)
{
    uint32_t v = tile_row(e, r);
    uint8_t o = opaque(v);
    p6[0] = (uint8_t)(v >> 24); p6[1] = (uint8_t)(v >> 16); p6[2] = (uint8_t)(v >> 8); p6[3] = (uint8_t)v;
    p6[4] = (e & 0x2000) ? o : 0;
    p6[5] = (e & 0x4000) ? o : 0;
    *mask = o;
}

/* ---- the rink bitmap: plane B's cells ---------------------------------------------------------- */

static void rink_cell(int i)
{
    int cx = i & 63, cy = i >> 6;
    unsigned e = disp.cell[1][i];
    for (int r = 0; r < 8; r++) {
        uint8_t p6[6], m;
        cell_row(e, r, p6, &m);
        uint8_t hi = (e & 0x8000) ? m : 0;
        int y = cy * 8 + r;
        for (int copy = 0; copy < 2; copy++) {
            int bx = cx + copy * 64;
            if (bx >= OBJ_RW) break;
            uint8_t *l = obj.rink + y * OBJ_LS + bx;
            for (int k = 0; k < 6; k++) l[k * OBJ_RW] = p6[k];
            obj.rink_hi[y * OBJ_RW + bx] = hi;
            if (cx >= 32) break;                          /* (only the first 32 columns are repeated) */
        }
    }
}

static void add(struct rect *list, int *n, int *overflow, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (*n >= MAXR) { *overflow = 1; return; }
    list[*n].x = (int16_t)x; list[*n].y = (int16_t)y; list[*n].w = (int16_t)w; list[*n].h = (int16_t)h;
    (*n)++;
}

/* ---- the operations (the blitter's, done here by the CPU) ------------------------------------- */

static void op_copy(uint8_t *dst, const struct rect *r)  /* the rink bitmap into a buffer: whole bytes */
{
    int x0 = r->x >> 3, x1 = (r->x + r->w - 1) >> 3;
    if (x0 < 0) x0 = 0;
    if (x1 >= OBJ_RW) x1 = OBJ_RW - 1;
    for (int j = 0; j < r->h; j++) {
        int y = (r->y + j) & 255;
        for (int k = 0; k < 6; k++) memcpy(dst + y * OBJ_LS + k * OBJ_RW + x0, obj.rink + y * OBJ_LS + k * OBJ_RW + x0, (size_t)(x1 - x0 + 1));
    }
    obj.restored++;
}

/* eight pixels (six plane bytes and a mask, the left pixel in bit 7) into the buffer at pixel x of line y */
static inline void put8(uint8_t *buf, int x, int y, const uint8_t *p6, uint8_t m)
{
    if (!m) return;
    int s = x & 7, b = x >> 3;
    uint8_t *l = buf + (y & 255) * OBJ_LS;
    uint8_t m0 = (uint8_t)(m >> s), m1 = s ? (uint8_t)(m << (8 - s)) : 0;
    for (int k = 0; k < 6; k++) {
        uint8_t v0 = (uint8_t)(p6[k] >> s);
        if (m0 && b >= 0) l[k * OBJ_RW + b] = (uint8_t)((l[k * OBJ_RW + b] & ~m0) | (v0 & m0));
        if (m1 && b + 1 < OBJ_RW) {
            uint8_t v1 = (uint8_t)(p6[k] << (8 - s));
            l[k * OBJ_RW + b + 1] = (uint8_t)((l[k * OBJ_RW + b + 1] & ~m1) | (v1 & m1));
        }
    }
}

/* the rink's high-priority pixels again, over a rectangle of the buffer (aligned: same coordinates) */
static void op_recut(uint8_t *buf, const struct rect *r)
{
    int x0 = r->x >> 3, x1 = (r->x + r->w - 1) >> 3;
    if (x1 >= OBJ_RW) x1 = OBJ_RW - 1;
    for (int j = 0; j < r->h; j++) {
        int y = (r->y + j) & 255;
        const uint8_t *hi = obj.rink_hi + y * OBJ_RW;
        for (int b = x0; b <= x1; b++) {
            uint8_t m = hi[b];
            if (!m) continue;
            for (int k = 0; k < 6; k++) {
                uint8_t *p = buf + y * OBJ_LS + k * OBJ_RW + b;
                *p = (uint8_t)((*p & ~m) | (obj.rink[y * OBJ_LS + k * OBJ_RW + b] & m));
            }
        }
    }
}

/* ---- plane A and the window, in screen coordinates -------------------------------------------- */

static void window_span(int line, int *x0, int *x1)
{
    int width = disp.width, wy = (disp.reg[18] & 0x1F) * 8;
    *x0 = *x1 = 0;
    if ((disp.reg[18] & 0x80) ? line >= wy : line < wy) { *x1 = width; return; }
    int wx = (disp.reg[17] & 0x1F) * 16;
    if (wx > width) wx = width;
    if (disp.reg[17] & 0x80) { *x0 = wx; *x1 = width; } else *x1 = wx;
}

static void a_layer(void)
{
    a_any_lo = a_any_hi = 0;
    for (int y = 0; y < SH; y++) {
        int w0, w1;
        window_span(y, &w0, &w1);
        for (int bx = 0; bx < SW; bx++) {
            int x = bx * 8;
            unsigned e = (x >= w0 && x < w1) ? disp.cell[2][(y >> 3) * disp.win_w + bx] : disp.cell[0][(y >> 3) * 64 + bx];
            uint8_t p6[6], m;
            cell_row(e, y & 7, p6, &m);
            for (int k = 0; k < 6; k++) a_img[y][k][bx] = p6[k];
            a_hi[y][bx] = (e & 0x8000) ? m : 0;
            a_lo[y][bx] = (e & 0x8000) ? 0 : m;
            a_any_hi |= a_hi[y][bx];
            a_any_lo |= a_lo[y][bx];
        }
    }
}

/* plane A's pixels of one priority into the buffer, by runs of bytes with something in them */
static void op_a(uint8_t *buf, int b, uint8_t (*mask)[SW])
{
    for (int y = 0; y < SH; y++)
        for (int bx = 0; bx < SW; bx++) {
            if (!mask[y][bx]) continue;
            int run = bx;
            while (run < SW && mask[y][run]) run++;
            add(done[b], &ndone[b], &full[b], obj.px0 + bx * 8, (obj.py0 + y) & 255, (run - bx) * 8 + 8, 1);
            for (int c = bx; c < run; c++) {
                uint8_t p6[6];
                for (int k = 0; k < 6; k++) p6[k] = a_img[y][k][c];
                put8(buf, obj.px0 + c * 8, obj.py0 + y, p6, mask[y][c]);
            }
            bx = run;
        }
}

/* ---- the sprites ------------------------------------------------------------------------------ */

struct spr { int x, y, w, h, pri, link; unsigned e; };
static struct spr sp[80];
static int nsp;
static uint8_t cpu_line[SH];

/* the sprites in link order, and the lines the VDP's limits change (those are composed by the CPU) */
static void sprite_list(void)
{
    int total = 64, per_line = 16, link = 0, seen = 0;
    static int16_t count[SH], dots[SH];
    memset(count, 0, sizeof count);
    memset(dots, 0, sizeof dots);
    nsp = 0;
    while (seen++ < total) {
        const uint8_t *c = disp.sat + link * 8, *q = disp.spr + link * 8;
        int y = ((c[0] << 8 | c[1]) & 0x3FF) - 128, sh = (c[2] & 3) + 1, sw = ((c[2] >> 2) & 3) + 1, next = c[3] & 0x7F;
        unsigned e = (unsigned)(q[4] << 8 | q[5]);
        int rawx = (q[6] << 8 | q[7]) & 0x1FF, x = rawx - 128;
        for (int l = y < 0 ? 0 : y; l < y + sh * 8 && l < SH; l++) {
            count[l]++;
            dots[l] = (int16_t)(dots[l] + sw * 8);
            if (count[l] > per_line || dots[l] > 256 || (rawx == 0 && count[l] > 1)) cpu_line[l] = 1;
        }
        if (y < SH && y + sh * 8 > 0 && x < 256 && x + sw * 8 > 0) {
            sp[nsp].x = x; sp[nsp].y = y; sp[nsp].w = sw * 8; sp[nsp].h = sh * 8;
            sp[nsp].pri = (e >> 15) & 1; sp[nsp].e = e; sp[nsp].link = link;
            nsp++;
        }
        if (!next || next >= total) break;
        link = next;
    }
    /* a high-priority sprite under an earlier low-priority one: the VDP takes the earlier one's pixel
     * and its priority; drawing by priority would not, so those lines go to the CPU too */
    for (int i = 0; i < nsp; i++) {
        if (!sp[i].pri) continue;
        for (int j = 0; j < i; j++) {
            if (sp[j].pri) continue;
            if (sp[j].x < sp[i].x + sp[i].w && sp[i].x < sp[j].x + sp[j].w && sp[j].y < sp[i].y + sp[i].h && sp[i].y < sp[j].y + sp[j].h) {
                int y0 = sp[i].y > sp[j].y ? sp[i].y : sp[j].y, y1 = (sp[i].y + sp[i].h < sp[j].y + sp[j].h) ? sp[i].y + sp[i].h : sp[j].y + sp[j].h;
                for (int l = y0 < 0 ? 0 : y0; l < y1 && l < SH; l++) cpu_line[l] = 1;
            }
        }
    }
}

/* one sprite, cookie-cut, from its tiles (they go down the columns first) */
static void op_sprite(uint8_t *buf, int b, const struct spr *s)
{
    int sw = s->w / 8, sh = s->h / 8;
    add(done[b], &ndone[b], &full[b], obj.px0 + s->x, (obj.py0 + s->y) & 255, s->w + 8, s->h);
    for (int ty = 0; ty < s->h; ty++) {
        int y = s->y + ty;
        if (y < 0 || y >= SH) continue;
        int row = (s->e & 0x1000) ? s->h - 1 - ty : ty;
        for (int tx = 0; tx < sw; tx++) {
            int col = (s->e & 0x0800) ? sw - 1 - tx : tx;
            unsigned t = ((s->e & 0x7FF) + (unsigned)(col * sh + (row >> 3))) & 0x7FF;
            unsigned e = t | (s->e & 0x6000) | (s->e & 0x0800);   /* (the tile, its palette line and horizontal flip; the row is chosen here) */
            uint8_t p6[6], m;
            cell_row(e, row & 7, p6, &m);
            int x = s->x + tx * 8;
            if (x <= -8 || x >= 256) continue;
            if (x < 0) { m = (uint8_t)(m & (0xFF >> -x)); }
            if (x + 8 > 256) m = (uint8_t)(m & (0xFF << (x + 8 - 256)));
            put8(buf, obj.px0 + x, obj.py0 + y, p6, m);
        }
    }
}

/* ---- the CPU's lines: the whole line composed, a pixel at a time ------------------------------ */

int disp_compose_line(int line, uint8_t *out);           /* (disp_draw.c: colour numbers, the VDP's rules) */

static void cpu_compose(uint8_t *buf, int b, int y)
{
    static uint8_t line[320];
    disp_compose_line(y, line);
    int by = (obj.py0 + y) & 255;
    for (int x = 0; x < 256; x++) {
        int px = obj.px0 + x, bb = px >> 3, bit = 7 - (px & 7);
        for (int k = 0; k < 6; k++) {
            uint8_t *p = buf + by * OBJ_LS + k * OBJ_RW + bb;
            *p = (uint8_t)((*p & ~(1 << bit)) | (((line[x] >> k) & 1) << bit));
        }
    }
    add(done[b], &ndone[b], &full[b], obj.px0, by, 264, 1);
    obj.cpu_lines++;
}

/* ---- the update -------------------------------------------------------------------------------- */

static int tile_dirty(unsigned t) { t &= 0x7FF; return disp.tile_dirty[t >> 3] >> (t & 7) & 1; }

void obj_update(void)
{
    struct disp *d = &disp;
    obj.updates++;
    int rebuild = !active || d->layout_dirty;
    if (rebuild) {
        for (int i = 0; i < 64 * 32; i++) rink_cell(i);
        memcpy(obj.buf[0], obj.rink, OBJ_BYTES);
        memcpy(obj.buf[1], obj.rink, OBJ_BYTES);
        ndone[0] = ndone[1] = nchg[0] = nchg[1] = full[0] = full[1] = 0;
        active = 1;
        obj.rebuilds++;
    } else {
        /* the rink's cells that changed: written, or showing a tile whose pixels changed */
        for (int i = 0; i < 64 * 32; i++) {
            int w = (d->any_cell_dirty && (d->cell_dirty[1][i >> 3] >> (i & 7) & 1)) || (d->any_tile_dirty && tile_dirty(d->cell[1][i]));
            if (!w) continue;
            rink_cell(i);
            int x = (i & 63) * 8, y = (i >> 6) * 8;
            for (int b = 0; b < 2; b++) {
                add(chg[b], &nchg[b], &full[b], x, y, 8, 8);
                if (x < 256) add(chg[b], &nchg[b], &full[b], x + 512, y, 8, 8);
            }
        }
    }
    a_layer();                                            /* (plane A and the window: drawn again each pass, for now) */
    int b = obj.shown ^ 1;
    uint8_t *buf = obj.buf[b];
    /* restore what the objects covered in this buffer, and the rink's changes since it was drawn */
    if (full[b]) memcpy(buf, obj.rink, OBJ_BYTES);
    else {
        for (int i = 0; i < ndone[b]; i++) op_copy(buf, &done[b][i]);
        for (int i = 0; i < nchg[b]; i++) op_copy(buf, &chg[b][i]);
    }
    ndone[b] = nchg[b] = full[b] = 0;
    obj.px0 = (512 - disp_hscroll(0, 1)) & 511;
    obj.py0 = d->vs[1] & 255;
    memset(cpu_line, 0, sizeof cpu_line);
    sprite_list();
    /* the painter's order: A low, sprites low, the rink's high pixels again, A high, sprites high */
    if (a_any_lo) op_a(buf, b, a_lo);
    for (int i = nsp - 1; i >= 0; i--) if (!sp[i].pri) op_sprite(buf, b, &sp[i]);
    if (a_any_lo || nsp) {
        for (int y = 0; y < SH; y++) {
            int any = 0;
            for (int bx = 0; bx < SW && !any; bx++) any = a_lo[y][bx];
            if (any) { struct rect r = { (int16_t)obj.px0, (int16_t)((obj.py0 + y) & 255), 264, 1 }; op_recut(buf, &r); }
        }
        for (int i = 0; i < nsp; i++)
            if (!sp[i].pri) {
                int y0 = sp[i].y < 0 ? 0 : sp[i].y, y1 = sp[i].y + sp[i].h > SH ? SH : sp[i].y + sp[i].h;
                struct rect r = { (int16_t)(obj.px0 + (sp[i].x < 0 ? 0 : sp[i].x)), (int16_t)((obj.py0 + y0) & 255), (int16_t)(sp[i].w + 8), (int16_t)(y1 - y0) };
                if (r.h > 0) op_recut(buf, &r);
            }
    }
    if (a_any_hi) op_a(buf, b, a_hi);
    for (int i = nsp - 1; i >= 0; i--) if (sp[i].pri) op_sprite(buf, b, &sp[i]);
    for (int y = 0; y < SH; y++) if (cpu_line[y]) cpu_compose(buf, b, y);
    obj.shown = b;
    memset(d->tile_dirty, 0, sizeof d->tile_dirty);
    memset(d->cell_dirty, 0, sizeof d->cell_dirty);
    d->any_tile_dirty = d->any_cell_dirty = d->layout_dirty = d->sprites_dirty = d->scroll_dirty = 0;
}

void obj_leave(void) { active = 0; }

/* the shown window as six planes of 40 bytes a line (the 256 pixels at the left), for the planes'
 * hash both sides compute */
void obj_window(uint8_t *const planes[6])
{
    int s = obj.px0 & 7;
    for (int y = 0; y < SH; y++) {
        const uint8_t *l = obj.buf[obj.shown] + ((obj.py0 + y) & 255) * OBJ_LS + (obj.px0 >> 3);
        for (int k = 0; k < 6; k++) {
            uint8_t *o = planes[k] + y * 40;
            for (int bx = 0; bx < SW; bx++) o[bx] = (uint8_t)(s ? (l[k * OBJ_RW + bx] << s | l[k * OBJ_RW + bx + 1] >> (8 - s)) : l[k * OBJ_RW + bx]);
            for (int bx = SW; bx < 40; bx++) o[bx] = 0;
        }
    }
}

int obj_pixel(int x, int y)
{
    int px = obj.px0 + x, by = (obj.py0 + y) & 255, c = 0;
    for (int k = 0; k < 6; k++) c |= ((obj.buf[obj.shown][by * OBJ_LS + k * OBJ_RW + (px >> 3)] >> (7 - (px & 7))) & 1) << k;
    return c;
}
