/* The scrolled scene: see disp_obj.h and amiga/notes/p7_design.md. The picture is made with the
 * blitter's two operations (amiga/host/blit.h): copies (restoring a buffer from the rink bitmap) and
 * cookie-cuts through a mask (the rink's high-priority pixels again, plane A, the sprites). The CPU
 * draws into the rink bitmap, the plane A images and the sprite images only when what they show
 * changed, and composes the lines where the VDP's sprite limits apply. On the host the blits run in
 * blit.c's C model, so amiga/tools/halcheck.c's frame-by-frame check covers how they are set up. */
#include <string.h>
#include <stdint.h>
#include "disp_core.h"
#include "disp_obj.h"
#include "disp_draw.h"
#include "blit.h"

struct obj obj;
#define SWMAX 40                                          /* screen bytes a line at most (320 pixels) */
static int SW = 32, WPX = 256;                            /* this picture's: bytes a line and pixels (256 or 320) */
#define SH 224
#define AW 42                                             /* bytes a plane row of the plane A images: up to 320 pixels and a spare word */
#define SPW 6                                             /* bytes a plane row of a sprite image: up to 32 pixels and a spare word */
#define SPR_BYTES (SPW * 32 * 6)
#define NSPR 64
#define MAXR 512
struct rect { int16_t x, y, w, h; };                     /* pixels, in the buffers' coordinates (y wraps at 256) */
static struct rect done[2][MAXR], chg[2][MAXR], adone[2][MAXR];
static int ndone[2], nchg[2], nadone[2], full[2], active;
static int a_ver, b_px0[2] = { -1, -1 }, b_py0[2], b_aver[2];   /* plane A's version, and what each buffer was drawn with */
static uint8_t *a_img, *a_lo_m, *a_hi_m;                 /* plane A and the window on screen: six interleaved planes, and the masks by priority (repeated a plane row) */
static uint8_t a_lo[SH][SWMAX], a_hi[SH][SWMAX];               /* the masks again, a byte a cell row, for the CPU */
static int16_t a_row_lo[SH], a_row_hi[SH];               /* bytes of each line with something in the mask */
static uint8_t *spr_img, *spr_mask;                       /* the sprite images, NSPR of them by link number */
static struct { uint16_t e; uint8_t size, valid; } spr_key[NSPR];
static uint8_t rev[256];
static uint8_t rink_hi_cell[64 * 32];                     /* a rink cell has high-priority pixels */

void obj_reset(uint8_t *chip)
{
    memset(&obj, 0, sizeof obj);
    obj.rink = chip;
    obj.rink_hi = chip + OBJ_BYTES;
    obj.buf[0] = chip + 2 * OBJ_BYTES;
    obj.buf[1] = chip + 3 * OBJ_BYTES;
    a_img = chip + 4 * OBJ_BYTES;
    a_lo_m = a_img + AW * 6 * SH;
    a_hi_m = a_lo_m + AW * 6 * SH;
    spr_img = a_hi_m + AW * 6 * SH;
    spr_mask = spr_img + NSPR * SPR_BYTES;
    memset(a_img, 0, (size_t)(3 * AW * 6 * SH));
    memset(spr_key, 0, sizeof spr_key);
    active = 0;
    b_px0[0] = b_px0[1] = -1;
    for (int b = 0; b < 256; b++) {
        int r = 0;
        for (int i = 0; i < 8; i++) r |= ((b >> i) & 1) << (7 - i);
        rev[b] = (uint8_t)r;
    }
}

int obj_scene(void)
{
    const struct disp *d = &disp;
    if ((d->width != 256 && d->width != 320) || (d->reg[11] & 7) || d->plane_w != 64 || d->plane_h != 32 || d->vs[0]) return 0;
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

/* ---- the rink bitmap: plane B's cells, and its high-priority pixels as a mask (a plane row each) - */

static void rink_cell(int i)
{
    obj.rink_cells++;
    int cx = i & 63, cy = i >> 6;
    unsigned e = disp.cell[1][i];
    rink_hi_cell[i] = 0;
    for (int r = 0; r < 8; r++) {
        uint8_t p6[6], m;
        cell_row(e, r, p6, &m);
        uint8_t hi = (e & 0x8000) ? m : 0;
        rink_hi_cell[i] |= hi != 0;
        int y = cy * 8 + r;
        for (int copy = 0; copy < 2; copy++) {
            int bx = cx + copy * 64;
            if (bx >= OBJ_RW) break;
            uint8_t *l = obj.rink + y * OBJ_LS + bx, *h = obj.rink_hi + y * OBJ_LS + bx;
            for (int k = 0; k < 6; k++) { l[k * OBJ_RW] = p6[k]; h[k * OBJ_RW] = hi; }
            if (cx >= 40) break;                          /* (the first 40 columns are repeated: the widest picture) */
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

/* ---- the blits ---------------------------------------------------------------------------------- */

/* the words of a rectangle of a buffer, rows by six planes, split where the 256 lines wrap */
static void blit_rect(uint8_t *dst, const uint8_t *src_a, const uint8_t *src_b, int x, int y, int w, int h, uint16_t con0)
{
    if (x < 0) { w += x; x = 0; }
    if (x + w > OBJ_RW * 8) w = OBJ_RW * 8 - x;
    if (w <= 0 || h <= 0) return;
    int w0 = x >> 4, w1 = (x + w - 1) >> 4, words = w1 - w0 + 1;
    y &= 255;
    while (h > 0) {
        int rows = h < 256 - y ? h : 256 - y;
        uint32_t off = (uint32_t)y * OBJ_LS + 2u * (uint32_t)w0;
        struct blit bl = { 0 };
        bl.a = src_a + off;
        bl.b = src_b ? src_b + off : 0;
        bl.c = src_b ? dst + off : 0;
        bl.d = dst + off;
        bl.amod = bl.bmod = bl.cmod = bl.dmod = (int16_t)(OBJ_RW - 2 * words);
        bl.con0 = con0;
        bl.fwm = bl.lwm = 0xFFFF;
        bl.w = (uint16_t)words;
        bl.h = (uint16_t)(rows * 6);
        blit_run(&bl);
        h -= rows;
        y = 0;
    }
    obj.restored++;
}
static void op_copy(uint8_t *buf, const struct rect *r)  /* the rink bitmap into a buffer */
{
    uint32_t w0 = blit_words;
    blit_rect(buf, obj.rink, 0, r->x, r->y, r->w, r->h, BC0_USEA | BC0_USED | MINTERM_COPY_A);
    obj.w_restore += blit_words - w0;
}
static void op_recut(uint8_t *buf, int x, int y, int w, int h)   /* the rink's high-priority pixels again over a rectangle */
{
    uint32_t w0 = blit_words;
    blit_rect(buf, obj.rink_hi, obj.rink, x, y, w, h, BC0_USEA | BC0_USEB | BC0_USEC | BC0_USED | MINTERM_COOKIE);
    obj.w_recut += blit_words - w0;
}

/* an image (six interleaved planes and its mask, `bpr` bytes a plane row) cut into a buffer with its
 * left edge at pixel x of buffer line y, `words` words of each row (the last one taken only for the
 * shift: its own pixels masked off when it is one more than the image needs), rows lines of it */
static void op_cookie_w(uint8_t *buf, const uint8_t *img, const uint8_t *mask, int bpr, int words, int x, int y, int rows)
{
    int s = x & 15;
    y &= 255;
    while (rows > 0) {
        int n = rows < 256 - y ? rows : 256 - y;
        uint8_t *d = buf + (uint32_t)y * OBJ_LS + 2u * (uint32_t)(x >> 4);
        struct blit bl = { 0 };
        bl.a = mask; bl.b = img; bl.c = d; bl.d = d;
        bl.amod = bl.bmod = (int16_t)(bpr - 2 * words);
        bl.cmod = bl.dmod = (int16_t)(OBJ_RW - 2 * words);
        bl.con0 = (uint16_t)(s << 12 | BC0_USEA | BC0_USEB | BC0_USEC | BC0_USED | MINTERM_COOKIE);
        bl.con1 = (uint16_t)(s << 12);
        bl.fwm = 0xFFFF;
        bl.lwm = s ? 0 : 0xFFFF;                          /* (the extra word: only what shifts in from the one before) */
        bl.w = (uint16_t)words;
        bl.h = (uint16_t)(n * 6);
        blit_run(&bl);
        img += n * 6 * bpr;
        mask += n * 6 * bpr;
        rows -= n;
        y = 0;
    }
}

static inline int tile_dirty_used(unsigned t) { t &= 0x7FF; return disp.tile_dirty[t >> 3] >> (t & 7) & 1; }

/* a tile whose pixels changed and that some cell names */
static int used_tiles_changed(void)
{
    for (int i = 0; i < DISP_TILES / 8; i++) {
        unsigned m = disp.tile_dirty[i];
        for (int j = 0; m; j++, m >>= 1) if ((m & 1) && disp.uses[i * 8 + j]) return 1;
    }
    return 0;
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

static int in_window(int cx, int cy)                     /* a screen cell is the window's (its rows and columns are whole cells) */
{
    int w0, w1;
    window_span(cy * 8, &w0, &w1);
    return cx * 8 >= w0 && cx * 8 < w1;
}

/* one screen cell of plane A or the window into the A images, and the rows' counts */
static void a_cell(int cx, int cy, int win)
{
    obj.a_cells++;
    unsigned e = win ? disp.cell[2][cy * disp.win_w + cx] : disp.cell[0][cy * 64 + cx];
    for (int r = 0; r < 8; r++) {
        int y = cy * 8 + r;
        uint8_t p6[6], m;
        cell_row(e, r, p6, &m);
        uint8_t hi = (e & 0x8000) ? m : 0, lo = (e & 0x8000) ? 0 : m;
        for (int k = 0; k < 6; k++) {
            uint32_t o = (uint32_t)(y * 6 + k) * AW + (uint32_t)cx;
            a_img[o] = p6[k];
            a_hi_m[o] = hi;
            a_lo_m[o] = lo;
        }
        a_row_hi[y] = (int16_t)(a_row_hi[y] + (hi != 0) - (a_hi[y][cx] != 0));
        a_row_lo[y] = (int16_t)(a_row_lo[y] + (lo != 0) - (a_lo[y][cx] != 0));
        a_hi[y][cx] = hi;
        a_lo[y][cx] = lo;
    }
}

static void a_layer(void)                                 /* all of it */
{
    for (int cy = 0; cy < SH / 8; cy++) for (int cx = 0; cx < SW; cx++) a_cell(cx, cy, in_window(cx, cy));
}

/* the cells of plane A and the window that were written or show a changed tile (the screen's part:
 * A does not scroll in a scrolled scene) */
static void a_changes(int tiles)
{
    a_ver++;
    for (int cy = 0; cy < SH / 8; cy++)
        for (int cx = 0; cx < SW; cx++) {
            int win = in_window(cx, cy), i = win ? cy * disp.win_w + cx : cy * 64 + cx, k = win ? 2 : 0;
            if (!((disp.cell_dirty[k][i >> 3] >> (i & 7) & 1) || (tiles && tile_dirty_used(disp.cell[k][i])))) continue;
            a_cell(cx, cy, win);
        }
}

/* what changed under plane A this pass: the screen lines (rows) and on each the pixels from row_x0 to
 * row_x1 (screen coordinates); plane A is cut again only there */
static int16_t row_x0[SH], row_x1[SH];
static void mark_span(uint8_t *rows, int y, int x0, int x1)
{
    if (x0 < 0) x0 = 0;
    if (x1 > WPX) x1 = WPX;
    if (y < 0 || y >= SH || x1 <= x0) return;
    if (!rows[y]) { rows[y] = 1; row_x0[y] = (int16_t)x0; row_x1[y] = (int16_t)x1; return; }
    if (x0 < row_x0[y]) row_x0[y] = (int16_t)x0;
    if (x1 > row_x1[y]) row_x1[y] = (int16_t)x1;
}

static void add_a(int b, int x, int y, int w, int h)      /* (a cut of plane A, for its restore later; the same one twice is kept once) */
{
    for (int i = 0; i < nadone[b]; i++)
        if (adone[b][i].x == x && adone[b][i].y == y && adone[b][i].w == w && adone[b][i].h == h) return;
    add(adone[b], &nadone[b], &full[b], x, y, w, h);
}

/* plane A of one priority over the screen lines marked in `rows` that have some of it, in bands of
 * lines, and in each band only the runs of words that have some of it within what changed */
static void op_a(uint8_t *buf, int b, const uint8_t *mask, const uint8_t (*fast)[SWMAX], const int16_t *count, const uint8_t *rows)
{
    for (int y = 0; y < SH; y++) {
        if (!rows[y] || !count[y]) continue;
        int y1 = y, s0 = WPX, s1 = 0;
        while (y1 < SH && rows[y1] && count[y1]) {
            if (row_x0[y1] < s0) s0 = row_x0[y1];
            if (row_x1[y1] > s1) s1 = row_x1[y1];
            y1++;
        }
        int w0 = s0 >> 4, w1 = (s1 - 1) >> 4;
        uint8_t any[SWMAX / 2];                           /* (the Fast RAM copy of the mask, a word a column: Chip RAM reads are slow) */
        memset(any, 0, sizeof any);
        for (int l = y; l < y1; l++) {
            const uint8_t *f = fast[l];
            for (int w = w0; w <= w1; w++) any[w] |= f[2 * w] | f[2 * w + 1];
        }
        for (int w = w0; w <= w1; w++) {
            if (!any[w]) continue;
            int r0 = w;
            while (w + 1 <= w1 && any[w + 1]) w++;
            int x = obj.px0 + r0 * 16, words = w - r0 + 1 + ((x & 15) ? 1 : 0);
            uint32_t bw = blit_words;
            op_cookie_w(buf, a_img + (uint32_t)y * 6 * AW + 2u * (uint32_t)r0, mask + (uint32_t)y * 6 * AW + 2u * (uint32_t)r0, AW, words, x, obj.py0 + y, y1 - y);
            obj.w_a += blit_words - bw;
            add_a(b, x & ~15, (obj.py0 + y) & 255, words * 16, y1 - y);
        }
        y = y1;
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
    int h40 = WPX == 320, total = h40 ? 80 : 64, per_line = h40 ? 20 : 16, link = 0, seen = 0;
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
            if (count[l] > per_line || dots[l] > WPX || (rawx == 0 && count[l] > 1)) cpu_line[l] = 1;
        }
        if (y < SH && y + sh * 8 > 0 && x < WPX && x + sw * 8 > 0) {
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

/* a sprite's image and mask in its slot: its tiles (down the columns first), flips and palette line;
 * each of its rows written whole (the bytes right of the sprite and the spare word zero), the rows
 * below it not touched (no blit reads them) */
static void spr_build(const struct spr *s)
{
    int sw = s->w / 8, sh = s->h / 8;
    static uint32_t ti[SPR_BYTES / 4], tm[SPR_BYTES / 4];  /* (made in Fast RAM, then copied a long at a time: Chip RAM byte writes are slow) */
    uint8_t *img = (uint8_t *)ti, *mask = (uint8_t *)tm;
    obj.sprite_builds++;
    for (int ty = 0; ty < s->h; ty++) {
        int row = (s->e & 0x1000) ? s->h - 1 - ty : ty;
        uint8_t *pi = img + (uint32_t)ty * 6 * SPW, *pm = mask + (uint32_t)ty * 6 * SPW;
        memset(pi, 0, 6 * SPW);
        uint8_t m6[SPW] = { 0 };
        for (int tx = 0; tx < sw; tx++) {
            int col = (s->e & 0x0800) ? sw - 1 - tx : tx;
            unsigned t = ((s->e & 0x7FF) + (unsigned)(col * sh + (row >> 3))) & 0x7FF;
            uint8_t p[6], m;
            cell_row(t | (s->e & 0x6800), row & 7, p, &m);
            for (int k = 0; k < 6; k++) pi[k * SPW + tx] = p[k];
            m6[tx] = m;
        }
        for (int k = 0; k < 6; k++) memcpy(pm + k * SPW, m6, SPW);
    }
    uint32_t *di = (uint32_t *)(void *)(spr_img + s->link * SPR_BYTES), *dm = (uint32_t *)(void *)(spr_mask + s->link * SPR_BYTES);
    for (int i = 0, n = s->h * 6 * SPW / 4; i < n; i++) { di[i] = ti[i]; dm[i] = tm[i]; }
    spr_key[s->link].e = (uint16_t)s->e;
    spr_key[s->link].size = (uint8_t)(sw << 4 | sh);
    spr_key[s->link].valid = 1;
}

static int spr_tiles_dirty(const struct spr *s)
{
    int n = (s->w / 8) * (s->h / 8);
    for (int t = 0; t < n; t++) if (tile_dirty_used(((s->e & 0x7FF) + (unsigned)t) & 0x7FF)) return 1;
    return 0;
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

/* one sprite, cookie-cut, from its tiles (they go down the columns first) */
static void op_sprite_cpu(uint8_t *buf, int b, const struct spr *s)
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
            if (x <= -8 || x >= WPX) continue;
            if (x < 0) { m = (uint8_t)(m & (0xFF >> -x)); }
            if (x + 8 > WPX) m = (uint8_t)(m & (0xFF << (x + 8 - WPX)));
            put8(buf, obj.px0 + x, obj.py0 + y, p6, m);
        }
    }
}

/* one sprite, cookie-cut into the buffer; the lines off the screen are left out */
static void op_sprite(uint8_t *buf, int b, const struct spr *s, int tiles)
{
    if (!spr_key[s->link].valid || spr_key[s->link].e != (uint16_t)s->e || spr_key[s->link].size != (uint8_t)((s->w / 8) << 4 | s->h / 8))
        spr_build(s);
    int y0 = s->y < 0 ? 0 : s->y, y1 = s->y + s->h > SH ? SH : s->y + s->h;
    if (y1 <= y0) return;
    int x = obj.px0 + s->x;
    if (x < 0 || (x >> 4) + SPW / 2 > OBJ_RW / 2) {        /* (past the buffer's edge: the CPU, clipped to the screen) */
        blit_wait();
        op_sprite_cpu(buf, b, s);
        return;
    }
    const uint8_t *img = spr_img + s->link * SPR_BYTES + (uint32_t)(y0 - s->y) * 6 * SPW;
    const uint8_t *mask = spr_mask + s->link * SPR_BYTES + (uint32_t)(y0 - s->y) * 6 * SPW;
    int words = (s->w + 15) / 16 + ((x & 15) ? 1 : 0);   /* (the sprite's words, and one for the shift) */
    uint32_t bw = blit_words;
    op_cookie_w(buf, img, mask, SPW, words, x, obj.py0 + y0, y1 - y0);
    obj.w_sprite += blit_words - bw;
    add(done[b], &ndone[b], &full[b], x & ~15, (obj.py0 + y0) & 255, words * 16, y1 - y0);
}

/* ---- the CPU's lines: composed eight pixels at a time, shifted into the buffer -------------------- */

static void cpu_compose(uint8_t *buf, int b, int y)
{
    static uint8_t lp[6][40];
    uint8_t *const out[6] = { lp[0], lp[1], lp[2], lp[3], lp[4], lp[5] };
    disp_planar_line(y, out);
    int by = (obj.py0 + y) & 255, s0 = obj.px0 & 7, b0 = obj.px0 >> 3;
    uint8_t *l = buf + by * OBJ_LS;
    static uint32_t tl[(SWMAX + 4) / 4];                 /* (the line's plane row made in Fast RAM, at the buffer's byte parity, then copied in longs) */
    for (int k = 0; k < 6; k++) {
        uint8_t *d = l + k * OBJ_RW + b0, *t = (uint8_t *)tl + ((uintptr_t)d & 1);
        int n = SW;
        if (!s0) memcpy(t, lp[k], SW);
        else {
            uint8_t keep = (uint8_t)(0xFF << (8 - s0));
            t[0] = (uint8_t)((d[0] & keep) | (lp[k][0] >> s0));
            for (int i = 1; i < SW; i++) t[i] = (uint8_t)(lp[k][i - 1] << (8 - s0) | lp[k][i] >> s0);
            t[SW] = (uint8_t)((d[SW] & (0xFF >> s0)) | (uint8_t)(lp[k][SW - 1] << (8 - s0)));
            n = SW + 1;
        }
        if ((uintptr_t)d & 1) { *d++ = *t++; n--; }
        memcpy(d, t, (size_t)n);
    }
    add(done[b], &ndone[b], &full[b], obj.px0, by, WPX + 8, 1);
    obj.cpu_lines++;
}

/* ---- the update -------------------------------------------------------------------------------- */

static void mark_rect(uint8_t *rows, const struct rect *r)   /* the screen pixels a buffer rectangle covers */
{
    int x0 = (r->x & ~15) - obj.px0, x1 = ((r->x + r->w + 15) & ~15) - obj.px0;   /* (whole words of the buffer: the blits' extent) */
    for (int j = 0; j < r->h; j++) {
        int y = (r->y + j - obj.py0) & 255;
        if (y < SH) mark_span(rows, y, x0, x1);
    }
}

void obj_update(void)
{
    struct disp *d = &disp;
    obj.updates++;
    SW = d->width / 8;
    WPX = d->width;
    int rebuild = !active || d->layout_dirty;
    int tiles = !rebuild && d->any_tile_dirty && used_tiles_changed();
    if (rebuild) {
        for (int i = 0; i < 64 * 32; i++) rink_cell(i);
        memset(a_row_lo, 0, sizeof a_row_lo);
        memset(a_row_hi, 0, sizeof a_row_hi);
        memset(a_lo, 0, sizeof a_lo);
        memset(a_hi, 0, sizeof a_hi);
        a_layer();
        a_ver++;
        memset(spr_key, 0, sizeof spr_key);
        blit_wait();
        memcpy(obj.buf[0], obj.rink, OBJ_BYTES);
        memcpy(obj.buf[1], obj.rink, OBJ_BYTES);
        ndone[0] = ndone[1] = nchg[0] = nchg[1] = nadone[0] = nadone[1] = full[0] = full[1] = 0;
        active = 1;
        obj.rebuilds++;
    } else {
        blit_wait();                                      /* (the CPU writes the bitmaps the blits read) */
        for (int i = 0; i < 64 * 32; i++) {               /* the rink's cells that changed */
            if (!tiles && !d->plane_dirty[1]) break;
            if (!tiles && !(i & 7) && !d->cell_dirty[1][i >> 3]) { i += 7; continue; }
            int w = (d->cell_dirty[1][i >> 3] >> (i & 7) & 1) || (tiles && tile_dirty_used(d->cell[1][i]));
            if (!w) continue;
            rink_cell(i);
            int x = (i & 63) * 8, y = (i >> 6) * 8;
            for (int b = 0; b < 2; b++) {
                add(chg[b], &nchg[b], &full[b], x, y, 8, 8);
                if (x < 320) add(chg[b], &nchg[b], &full[b], x + 512, y, 8, 8);
            }
        }
        if (d->plane_dirty[0] || d->plane_dirty[2] || tiles) a_changes(tiles);
    }
    int b = obj.shown ^ 1;
    uint8_t *buf = obj.buf[b];
    obj.px0 = (512 - disp_hscroll(0, 1)) & 511;
    /* (the buffer's first 320 pixels are there again from 512: a 256-pixel picture at the left edge is
     * shown from that copy, so that the sprites partly left of the screen are blitted, not drawn by the
     * CPU after the blitter's queue has run out) */
    if (obj.px0 < 32 && obj.px0 + 512 + WPX + 16 <= OBJ_RW * 8) obj.px0 += 512;
    obj.py0 = d->vs[1] & 255;
    /* plane A stays in this buffer when the scroll and plane A are what they were when it was drawn:
     * then it is drawn again only on the lines where something is restored or drawn */
    int stay = !full[b] && b_px0[b] == obj.px0 && b_py0[b] == obj.py0 && b_aver[b] == a_ver;
    static uint8_t rows[SH];
    memset(rows, !stay, sizeof rows);
    if (!stay) for (int y = 0; y < SH; y++) { row_x0[y] = 0; row_x1[y] = (int16_t)WPX; }
    if (full[b]) { blit_wait(); memcpy(buf, obj.rink, OBJ_BYTES); }
    else {
        for (int i = 0; i < ndone[b]; i++) { op_copy(buf, &done[b][i]); if (stay) mark_rect(rows, &done[b][i]); }
        for (int i = 0; i < nchg[b]; i++) { op_copy(buf, &chg[b][i]); if (stay) mark_rect(rows, &chg[b][i]); }
        if (!stay) for (int i = 0; i < nadone[b]; i++) op_copy(buf, &adone[b][i]);
    }
    if (!stay) nadone[b] = 0;
    ndone[b] = nchg[b] = full[b] = 0;
    memset(cpu_line, 0, sizeof cpu_line);
    sprite_list();
    if (d->any_tile_dirty)                                /* the cached sprite images that show a changed tile, drawn this pass or not */
        for (int i = 0; i < NSPR; i++) {
            if (!spr_key[i].valid) continue;
            struct spr k = { 0, 0, (spr_key[i].size >> 4) * 8, (spr_key[i].size & 15) * 8, 0, i, spr_key[i].e };
            if (spr_tiles_dirty(&k)) spr_key[i].valid = 0;
        }
    if (stay)
        for (int i = 0; i < nsp; i++)
            for (int y = sp[i].y < 0 ? 0 : sp[i].y; y < sp[i].y + sp[i].h && y < SH; y++) mark_span(rows, y, sp[i].x, sp[i].x + sp[i].w);
    /* the painter's order: A low, sprites low, the rink's high pixels again, A high, sprites high */
    op_a(buf, b, a_lo_m, a_lo, a_row_lo, rows);
    for (int i = nsp - 1; i >= 0; i--) if (!sp[i].pri) op_sprite(buf, b, &sp[i], tiles);
    for (int i = 0; i < nsp; i++)
        if (!sp[i].pri) {
            int y0 = sp[i].y < 0 ? 0 : sp[i].y, y1 = sp[i].y + sp[i].h > SH ? SH : sp[i].y + sp[i].h;
            if (y1 <= y0) continue;
            int x0 = obj.px0 + (sp[i].x < 0 ? 0 : sp[i].x), any = 0;
            for (int cy = (obj.py0 + y0) >> 3; cy <= (obj.py0 + y1 - 1) >> 3 && !any; cy++)   /* (only where the rink has high pixels) */
                for (int cx = x0 >> 3; cx <= (x0 + sp[i].w + 15) >> 3 && !any; cx++) any = rink_hi_cell[(cy & 31) * 64 + (cx & 63)];
            if (!any) continue;
            op_recut(buf, x0, obj.py0 + y0, sp[i].w + 16, y1 - y0);
            for (int y = y0; y < y1; y++) mark_span(rows, y, (x0 & ~15) - obj.px0, ((x0 + sp[i].w + 31) & ~15) - obj.px0);   /* (plane A high again where the recut went: its whole words) */
        }
    for (int y = 0; y < SH; y++) {                        /* (and over plane A low on the lines it was drawn) */
        if (!rows[y] || !a_row_lo[y]) continue;
        int y1 = y;
        while (y1 < SH && rows[y1] && a_row_lo[y1]) { row_x0[y1] = 0; row_x1[y1] = (int16_t)WPX; y1++; }
        op_recut(buf, obj.px0, obj.py0 + y, WPX + 16, y1 - y);
        y = y1;
    }
    op_a(buf, b, a_hi_m, a_hi, a_row_hi, rows);
    for (int i = nsp - 1; i >= 0; i--) if (sp[i].pri) op_sprite(buf, b, &sp[i], tiles);
    int any_cpu = 0;
    for (int y = 0; y < SH; y++) any_cpu |= cpu_line[y];
    if (any_cpu) {
        blit_wait();
        for (int y = 0; y < SH; y++) if (cpu_line[y]) cpu_compose(buf, b, y);
    }
    b_px0[b] = obj.px0;
    b_py0[b] = obj.py0;
    b_aver[b] = a_ver;
    obj.shown = b;
    if (d->any_tile_dirty) memset(d->tile_dirty, 0, sizeof d->tile_dirty);
    for (int k = 0; k < 3; k++) if (d->plane_dirty[k]) memset(d->cell_dirty[k], 0, sizeof d->cell_dirty[k]);
    d->any_tile_dirty = d->any_cell_dirty = d->layout_dirty = d->sprites_dirty = d->scroll_dirty = 0;
    d->plane_dirty[0] = d->plane_dirty[1] = d->plane_dirty[2] = 0;
}

void obj_leave(void) { active = 0; b_px0[0] = b_px0[1] = -1; }

/* the shown window as six planes of 40 bytes a line (a 256-pixel picture at the left), for the
 * planes' hash both sides compute */
void obj_window(uint8_t *const planes[6])
{
    blit_wait();
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
