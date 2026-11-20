/* halcheck: amiga/host/hal_core.c (the Amiga port's answers to the game's hardware requests),
 * built for the host with the same src/ the Amiga runs, played over a script and checked frame by
 * frame against the reference host build's golden run (out/golden/NAME.golden, make golden). Each
 * golden line holds the FNV-1a 64 hashes of work RAM (as the 68000 has it) and of the VDP's state
 * (registers, VRAM, CRAM, VSRAM) at the game's wait. This tool keeps a VRAM copy only to hash it:
 * the Amiga program keeps none (PROMPT.md section 4).
 *
 * Usage: halcheck ROM GOLDEN FRAMES [SCRIPT]  -> exit 0 when every frame's RAM and VDP hashes match.
 * -o FILE writes the per-frame RAM hashes the Amiga's verify build checks (amiga/host/hash.h,
 * 4 bytes a frame, big-endian). */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "mem.h"
#include "rom.h"
#include "rules.h"
#include "rosters.h"
#include "hal_core.h"
#include "hash.h"
#include "intent.h"
#include "disp_core.h"
#include "disp_draw.h"
#include "mdrt/vdp.h"                                     /* the host build's renderer: the picture reference only */

#define FNV0 0xCBF29CE484222325ull
static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}

/* this side's copy of VRAM, inside the host renderer's state so that it can draw the reference
 * picture from it (ext/pulseman's render.c; a host tool only, never part of the Amiga build). A byte
 * of a sprite's first four also goes into the renderer's sprite cache, as its own VDP does it */
static mdrt_vdp refv;
#define vram refv.s.vram
static unsigned long st_cell_bytes, st_tile_bytes, st_sat_bytes, st_hs_bytes;   /* this pass's VRAM bytes by what they are (HALCHECK_STATS) */
static void vput(uint16_t a, uint8_t b)
{
    {                                                     /* which table the byte is in, by the layout registers now */
        int h40x = core.reg[12] & 0x81;
        uint32_t ta = (uint32_t)(core.reg[2] & 0x38) << 10, tb = (uint32_t)(core.reg[4] & 7) << 13;
        uint32_t tw = h40x ? (uint32_t)(core.reg[3] & 0x3C) << 10 : (uint32_t)(core.reg[3] & 0x3E) << 10;
        uint32_t hs = (uint32_t)(core.reg[13] & 0x3F) << 10, sb = h40x ? (uint32_t)(core.reg[5] & 0x7E) << 9 : (uint32_t)(core.reg[5] & 0x7F) << 9;
        static const int cells[4] = { 32, 64, 32, 128 };
        uint32_t plane = (uint32_t)(cells[core.reg[16] & 3] * cells[(core.reg[16] >> 4) & 3] * 2);
        if (a >= sb && a < sb + (h40x ? 0x280u : 0x200u)) st_sat_bytes++;
        else if (a >= hs && a < hs + 0x380) st_hs_bytes++;
        else if ((a >= ta && a < ta + plane) || (a >= tb && a < tb + plane) || (a >= tw && a < tw + 0x1000)) st_cell_bytes++;
        else st_tile_bytes++;
    }
    vram[a] = b;
    int h40 = core.reg[12] & 0x81;
    uint32_t base = h40 ? (uint32_t)(core.reg[5] & 0x7E) << 9 : (uint32_t)(core.reg[5] & 0x7F) << 9, off = (uint32_t)a - base;
    if (a >= base && off < (h40 ? 0x280u : 0x200u) && (off & 7) < 4) refv.sat[off] = b;
}
static unsigned long n_words, n_block_words, n_bytes, n_fill, n_cram;   /* this frame's requests (HALCHECK_FRAMES) */
/* each request goes into the display intent hash (intent.h: the Amiga's reference, -i) and, for the
 * golden check, into this side's copy of VRAM */
void plat_vram_byte(uint16_t a, uint8_t b) { intent_byte(a, b); disp_vram_byte(a, b); vput(a, b); n_bytes++; }
uint8_t plat_vram_read(uint16_t a) { return vram[a]; }
void plat_vram_block(uint16_t a, const uint8_t *src, uint32_t words) { intent_block(a, src, words); disp_vram_block(a, src, words); for (uint32_t k = 0; k < 2 * words; k++) vput((uint16_t)(a + k), src[k]); n_block_words += words; }
void plat_vram_word(uint16_t a, uint16_t w) { intent_word(a, w); disp_vram_word(a, w); vput(a, (uint8_t)(w >> 8)); vput((uint16_t)(a + 1), (uint8_t)w); n_words++; }
void plat_vram_long(uint16_t a, uint32_t v) { intent_long(a, v); disp_vram_long(a, v); for (int k = 0; k < 4; k++) vput((uint16_t)(a + k), (uint8_t)(v >> (24 - 8 * k))); n_words += 2; }
void plat_vram_fill(uint16_t a, uint8_t b, uint32_t n, uint8_t step) { intent_fill(a, b, n, step); disp_vram_fill(a, b, n, step); n_fill += n; while (n--) { vput(a ^ 1, b); a = (uint16_t)(a + step); } }
void plat_vram_copy(uint16_t a, uint16_t src, uint32_t n, uint8_t step)
{
    intent_copy(a, src, n, step);
    disp_vram_copy(a, src, n, step);
    while (n--) { vput(a ^ 1, vram[src ^ 1]); src++; a = (uint16_t)(a + step); }
}
void plat_cram(int i, uint16_t c) { intent_cram(i, c); disp_cram(i, c); n_cram++; }
void plat_vsram(int i, uint16_t v) { intent_vsram(i, v); disp_vsram(i, v); }
static int lit_before;                                 /* palette entries not black when this frame began */
static int saw_blank;                                  /* this frame: the display was switched off at some point */
void plat_reg(int r, uint8_t v) { intent_reg(r, v); disp_reg(r, v); if (r == 1 && !(v & 0x40)) saw_blank = 1; }
void plat_sound_command(const uint8_t *z) { (void)z; }
void plat_sound_reset(int running) { (void)running; }
void plat_sound_restart(const uint8_t *z) { (void)z; }
void plat_psg(uint8_t v) { (void)v; }

/* the script: "FRAME:BUTTON+BUTTON,..." held from that frame (native/main.c read_script) */
static struct { long frame; uint16_t b; } *script;
static size_t script_len;
static int load_script(const char *path)
{
    static const struct { const char *n; uint16_t bit; } names[] = {
        { "UP", 1 }, { "DOWN", 2 }, { "LEFT", 4 }, { "RIGHT", 8 }, { "B", 0x10 }, { "C", 0x20 }, { "A", 0x40 }, { "START", 0x80 },
    };
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    static char line[1 << 20];
    size_t cap = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        for (char *it = strtok(line, ",\r\n"); it; it = strtok(NULL, ",\r\n")) {
            char *colon = strchr(it, ':');
            if (!colon) continue;
            if (script_len == cap) script = realloc(script, (cap = cap ? 2 * cap : 256) * sizeof *script);
            script[script_len].frame = atol(it);
            uint16_t b = 0;
            for (char *nm = colon + 1; *nm;) {
                size_t n = strcspn(nm, "+"), i = 0;
                while (i < 8 && (strlen(names[i].n) != n || strncmp(names[i].n, nm, n))) i++;
                if (i == 8) { fprintf(stderr, "%s: button %.*s is not a 3-button pad's\n", path, (int)n, nm); return 0; }
                b |= names[i].bit;
                nm += n + (nm[n] == '+');
            }
            script[script_len++].b = b;
        }
    }
    fclose(f);
    return 1;
}
uint16_t plat_buttons(int pad, long h)
{
    if (pad != 0) return 0;
    uint16_t b = 0;
    for (size_t i = 0; i < script_len; i++) if (script[i].frame <= h) b = script[i].b;
    return b;
}

/* The picture amiga/host/disp_core.c's lists describe, drawn here on the host only, to check that
 * they hold everything the picture needs: the same rules as the host build's renderer (layers,
 * priority, the window, the sprite limits), but read from the tile cache in planar form, the cell
 * maps, the latched sprite entries, and the palette and scroll as the backend keeps them. */
static int dpix(unsigned e, int x, int y)                /* cell entry e: its tile's colour at x, y (flips applied) */
{
    if (e & 0x0800) x = 7 - x;
    if (e & 0x1000) y = 7 - y;
    uint32_t row = disp.tile[e & 0x7FF][y];
    int c = 0;
    for (int k = 0; k < 4; k++) c |= (int)((row >> (24 - 8 * k + 7 - x)) & 1) << k;
    return c;
}
struct dlayer { uint8_t col[320], pri[320]; };
static void dplane(int line, int k, int hscroll, int bug_x, struct dlayer *o)
{
    int w = disp.plane_w, h = disp.plane_h, width = disp.width, fine = hscroll & 15;
    int left = width == 320 ? (disp.vs[38] & disp.vs[39]) : 0;
    for (int x = 0; x < width; x++) {
        int vs = disp.vs[k];
        if (disp.reg[11] & 4) vs = x < fine ? left : disp.vs[(((x - fine) >> 4) * 2 + k) % 40];
        int py = (line + vs) & (h * 8 - 1);
        int px = (x - hscroll + (bug_x && x >= bug_x && x < bug_x + fine ? 16 : 0)) & (w * 8 - 1);
        unsigned e = disp.cell[k][(py >> 3) * w + (px >> 3)];
        int c = dpix(e, px & 7, py & 7);
        o->col[x] = (uint8_t)(c ? (((e >> 13) & 3) << 4) | c : 0);
        o->pri[x] = (uint8_t)(e >> 15);
    }
}
static void dsprites(int line, struct dlayer *o)
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
                int p = dpix(t, tx & 7, row & 7);
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
static void dline(int line, uint16_t *o)
{
    int width = disp.width;
    uint16_t back = disp.pal[disp.reg[7] & 0x3F];
    if (!(disp.reg[1] & 0x40)) { for (int x = 0; x < width; x++) o[x] = back; return; }
    struct dlayer a, b, s;
    int hl = (disp.reg[11] & 3) == 2 ? (line & ~7) : (disp.reg[11] & 3) == 3 ? line : 0;
    int hsa = disp_word((uint16_t)(disp.hscroll + (uint32_t)(hl << 2))) & 0x3FF, hsb = disp_word((uint16_t)(disp.hscroll + (uint32_t)(hl << 2) + 2)) & 0x3FF;
    int w0 = 0, w1 = 0, wy = (disp.reg[18] & 0x1F) * 8;
    if ((disp.reg[18] & 0x80) ? line >= wy : line < wy) w1 = width;
    else {
        int wx = (disp.reg[17] & 0x1F) * 16;
        if (wx > width) wx = width;
        if (disp.reg[17] & 0x80) { w0 = wx; w1 = width; } else w1 = wx;
    }
    dplane(line, 1, hsb, 0, &b);
    dplane(line, 0, hsa, (w0 == 0 && w1 > 0 && w1 < width) ? w1 : 0, &a);
    for (int x = w0; x < w1; x++) {
        unsigned e = disp.cell[2][(line >> 3) * disp.win_w + (x >> 3)];
        int c = dpix(e, x & 7, line & 7);
        a.col[x] = (uint8_t)(c ? (((e >> 13) & 3) << 4) | c : 0);
        a.pri[x] = (uint8_t)(e >> 15);
    }
    dsprites(line, &s);
    for (int x = 0; x < width; x++) {
        uint8_t c = 0;
        if (s.col[x] && s.pri[x]) c = s.col[x];
        else if (a.col[x] && a.pri[x]) c = a.col[x];
        else if (b.col[x] && b.pri[x]) c = b.col[x];
        else if (s.col[x]) c = s.col[x];
        else if (a.col[x]) c = a.col[x];
        else if (b.col[x]) c = b.col[x];
        if (x < 8 && (disp.reg[0] & 0x20)) c = 0;
        o[x] = c ? disp.pal[c] : back;
    }
}

static FILE *golden, *out, *intent_out, *stats, *planes_out;

/* HALCHECK_STATS=FILE: a line a frame of what the picture is made of, for the display backend's
 * design (amiga/tools/dispstats.py reads it): the mode, each plane's visible cells that are not
 * empty, their palette lines and priority, the sprites on screen, the scroll, and the pass's VRAM
 * bytes by kind */
static int tile_blank(unsigned t)
{
    const uint8_t *p = vram + ((t & 0x7FF) << 5);
    for (int i = 0; i < 32; i++) if (p[i]) return 0;
    return 1;
}
static unsigned vw(uint32_t a) { a &= 0xFFFE; return (unsigned)(vram[a] << 8 | vram[a + 1]); }
static void frame_stats(long frame)
{
    static const int cells[4] = { 32, 64, 32, 128 };
    int h40 = core.reg[12] & 0x81, width = h40 ? 320 : 256;
    int pw = cells[core.reg[16] & 3], ph = cells[(core.reg[16] >> 4) & 3];
    uint32_t tab[2] = { (uint32_t)(core.reg[2] & 0x38) << 10, (uint32_t)(core.reg[4] & 7) << 13 };
    uint32_t hs = (uint32_t)(core.reg[13] & 0x3F) << 10;
    int n[2] = { 0, 0 }, pal[2] = { 0, 0 }, pri[2] = { 0, 0 }, hsv[2], vsv[2];
    for (int k = 0; k < 2; k++) {
        hsv[k] = (int)(vw(hs + 2 * (uint32_t)k) & 0x3FF);
        vsv[k] = core.vsram[k];
        for (int cy = 0; cy <= 28; cy++)
            for (int cx = 0; cx <= width / 8; cx++) {
                int py = ((cy * 8 + vsv[k]) & (ph * 8 - 1)) >> 3, px = ((cx * 8 - hsv[k]) & (pw * 8 - 1)) >> 3;
                unsigned e = vw(tab[k] + (uint32_t)((py * pw + px) << 1));
                if (tile_blank(e)) continue;
                n[k]++;
                pal[k] |= 1 << ((e >> 13) & 3);
                pri[k] += e >> 15;
            }
    }
    /* the window: its rows or columns (R17, R18) */
    int wy = (core.reg[18] & 0x1F), wx = (core.reg[17] & 0x1F);
    int wrows = (core.reg[18] & 0x80) ? 28 - (wy < 28 ? wy : 28) : (wy < 28 ? wy : 28), wcols = (core.reg[17] & 0x80) ? width / 16 - wx : wx;
    /* the sprites, in link order */
    uint32_t sb = h40 ? (uint32_t)(core.reg[5] & 0x7E) << 9 : (uint32_t)(core.reg[5] & 0x7F) << 9;
    int total = h40 ? 80 : 64, link = 0, seen = 0, sn = 0, sarea = 0, spal = 0, spri = 0, sbig = 0;
    static int perline[240];
    memset(perline, 0, sizeof perline);
    while (seen++ < total) {
        uint32_t e = sb + (uint32_t)(link << 3);
        int y = (int)(vw(e) & 0x3FF) - 128, sh = (vram[e + 2] & 3) + 1, sw = ((vram[e + 2] >> 2) & 3) + 1, next = vram[e + 3] & 0x7F;
        unsigned t = vw(e + 4);
        int x = (int)(vw(e + 6) & 0x1FF) - 128;
        if (y < 224 && y + sh * 8 > 0 && x < width && x + sw * 8 > 0) {
            sn++;
            sarea += sw * sh * 64;
            spal |= 1 << ((t >> 13) & 3);
            spri += (int)(t >> 15);
            for (int l = y < 0 ? 0 : y; l < y + sh * 8 && l < 224; l++) if (++perline[l] > sbig) sbig = perline[l];
        }
        if (!next || next >= total) break;
        link = next;
    }
    int lit = 0;
    for (int i = 0; i < 64; i++) lit += core.cram[i] != 0;
    fprintf(stats, "%ld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lu,%lu,%lu,%lu,%06X\n", frame, width, core.reg[1] & 0x40 ? 1 : 0, lit,
            core.reg[11] & 7, pw, ph, n[0], pal[0], pri[0], n[1], pal[1], pri[1], wrows, wcols, sn, sarea, sbig, spal, spri,
            hsv[0], vsv[0], hsv[1], vsv[1], st_cell_bytes, st_tile_bytes, st_sat_bytes, st_hs_bytes, (unsigned)g.VBint);
}
static long frames_wanted, checked, bad_ram, bad_vdp, first_bad = -1, intent_passes, bad_pic, first_bad_pic, bad_disp, first_bad_disp, bad_draw, first_bad_draw;
static jmp_buf done;
void plat_frame_done(long frame)
{
    static uint8_t ram[RAM_SIZE];
    const char *want = getenv("HALCHECK_FRAMES");         /* "406,431,...": what those frames asked of the VDP */
    if (want) {
        char key[24];
        snprintf(key, sizeof key, ",%ld,", frame);
        static char list[4096];
        if (!list[0]) snprintf(list, sizeof list, ",%s,", want);
        if (strstr(list, key)) {
            int lit = 0;
            for (int i = 0; i < 64; i++) lit += core.cram[i] != 0;
            printf("(%d lit at its start) ", lit_before);
            printf("frame %ld: %lu words by the data port, %lu words by DMA blocks, %lu bytes, %lu fill bytes, %lu palette writes, VBint $%06X; display %s%s, %d colours lit\n",
                   frame, n_words, n_block_words, n_bytes, n_fill, n_cram, (unsigned)g.VBint,
                   core.reg[1] & 0x40 ? "on" : "off", saw_blank ? " (switched off in this pass)" : "", lit);
        }
    }
    lit_before = 0;
    for (int i = 0; i < 64; i++) lit_before += core.cram[i] != 0;
    n_words = n_block_words = n_bytes = n_fill = n_cram = 0;
    saw_blank = !(core.reg[1] & 0x40);
    ram_pack(ram);
    uint64_t hr = fnv(FNV0, ram, sizeof ram);
    uint64_t hv = fnv(fnv(fnv(fnv(FNV0, core.reg, 24), vram, sizeof vram), core.cram, sizeof core.cram), core.vsram, sizeof core.vsram);
    /* the picture the host build shows for this frame: its renderer on this side's VDP state */
    static uint16_t pic[240 * 320];
    memcpy(refv.s.reg, core.reg, 24);
    memcpy(refv.s.cram, core.cram, sizeof refv.s.cram);
    memcpy(refv.s.vsram, core.vsram, sizeof refv.s.vsram);
    mdrt_vdp_render(&refv, pic, 320);
    uint64_t hp = FNV0;
    for (int y = 0; y < mdrt_vdp_height(&refv); y++) hp = fnv(hp, pic + y * 320, (size_t)mdrt_vdp_width(&refv) * 2);
    uint64_t hd = FNV0;                                   /* and the picture disp_core's lists describe */
    for (int y = 0; y < 224; y++) { dline(y, pic); hd = fnv(hd, pic, (size_t)disp.width * 2); }
    /* and the six bitplanes the drawing side keeps up to date, turned back into colours (with the
     * display switched off the Amiga shows no bitplanes: the backdrop) */
    draw_update();
    uint64_t hw = FNV0;
    for (int y = 0; y < 224; y++) {
        for (int x = 0; x < disp.width; x++) {
            int c = 0;
            for (int k = 0; k < 6; k++) c |= ((draw.plane[k][y * DRAW_STRIDE + (x >> 3)] >> (7 - (x & 7))) & 1) << k;
            pic[x] = (c && (disp.reg[1] & 0x40)) ? disp.pal[c] : disp.pal[disp.reg[7] & 0x3F];
        }
        hw = fnv(hw, pic, (size_t)disp.width * 2);
    }
    {                                                     /* HALCHECK_PPM=DIR HALCHECK_FRAMES=...: the reference picture of those frames */
        const char *dir = getenv("HALCHECK_PPM"), *which = getenv("HALCHECK_FRAMES");
        char key[24], list[4096];
        snprintf(key, sizeof key, ",%ld,", frame);
        if (dir && which && snprintf(list, sizeof list, ",%s,", which) > 0 && strstr(list, key)) {
            static const uint8_t lv[8] = { 0, 34, 68, 102, 136, 170, 204, 238 };
            char path[512];
            snprintf(path, sizeof path, "%s/frame-%05ld.ppm", dir, frame);
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6 %d 224 255\n", disp.width);
                for (int y = 0; y < 224; y++) {
                    dline(y, pic);
                    for (int x = 0; x < disp.width; x++) {
                        uint16_t c = (disp.reg[1] & 0x40) ? pic[x] : disp.pal[disp.reg[7] & 0x3F];
                        uint8_t rgb[3] = { lv[(c >> 1) & 7], lv[(c >> 5) & 7], lv[(c >> 9) & 7] };
                        fwrite(rgb, 1, 3, f);
                    }
                }
                fclose(f);
            }
        }
    }
    if (planes_out) {                                     /* the Amiga's reference for its planes (NHL94VFY <script> SHOW) */
        uint32_t h = planes_hash32(draw.plane);
        uint8_t be[4] = { (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h };
        fwrite(be, 1, 4, planes_out);
    }
    if (out) {                                            /* the Amiga's reference: ram_hash32, big-endian */
        uint32_t h = ram_hash32(ram);
        uint8_t be[4] = { (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h };
        fwrite(be, 1, 4, out);
    }
    if (stats) frame_stats(frame);
    st_cell_bytes = st_tile_bytes = st_sat_bytes = st_hs_bytes = 0;
    uint32_t hi = intent_take();                          /* this pass's display requests (intent.h) */
    if (intent_out) {
        uint8_t be[4] = { (uint8_t)(hi >> 24), (uint8_t)(hi >> 16), (uint8_t)(hi >> 8), (uint8_t)hi };
        fwrite(be, 1, 4, intent_out);
    }
    if (hi != INTENT_START) intent_passes++;
    char line[256];
    unsigned long long gr, gv, gz, gp;
    if (golden && fgets(line, sizeof line, golden) && sscanf(line, "%llx %llx %llx %llx", &gr, &gv, &gz, &gp) == 4) {
        checked++;
        if (gr != hr) { bad_ram++; if (first_bad < 0) first_bad = frame; }
        if (gv != hv) { bad_vdp++; if (first_bad < 0) first_bad = frame; }
        if (gp != hp) { if (!bad_pic) first_bad_pic = frame; bad_pic++; }
        if (gp != hd) { if (!bad_disp) first_bad_disp = frame; bad_disp++; }
        if (gp != hw) { if (!bad_draw) first_bad_draw = frame; bad_draw++; }
    }
    if (frame >= frames_wanted) longjmp(done, 1);
}

int main(int argc, char **argv_in)
{
    char **volatile argv = argv_in;
    const char *volatile outpath = NULL, *volatile intentpath = NULL, *volatile planespath = NULL;
    volatile int nargs = argc;
    while (nargs > 2 && (!strcmp(argv[1], "-o") || !strcmp(argv[1], "-i") || !strcmp(argv[1], "-p"))) {
        if (argv[1][1] == 'o') outpath = argv[2]; else if (argv[1][1] == 'i') intentpath = argv[2]; else planespath = argv[2];
        argv += 2;
        nargs -= 2;
    }
    argc = nargs;
    if (argc < 4) { fprintf(stderr, "usage: halcheck [-o RAM_HASHES] [-i INTENT_HASHES] [-p PLANE_HASHES] ROM GOLDEN FRAMES [SCRIPT]\n"); return 2; }
    if (!rom_load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 2; }
    golden = fopen(argv[2], "r");
    frames_wanted = atol(argv[3]);
    if (argc > 4 && !load_script(argv[4])) return 2;
    if (outpath) out = fopen(outpath, "wb");
    if (intentpath) intent_out = fopen(intentpath, "wb");
    if (planespath) planes_out = fopen(planespath, "wb");
    if (getenv("HALCHECK_STATS")) stats = fopen(getenv("HALCHECK_STATS"), "w");
    memcpy(rules, rule_classic, sizeof rules);            /* (the vanilla game: native/vanilla/stubs.c) */
    rom_art = NULL;
    rom_limit = ROM_SIZE;
    rosters_retail();
    core_power_on(0);
    disp_reset();
    static uint8_t planes[6][DRAW_PLANE_BYTES];
    uint8_t *const pp[6] = { planes[0], planes[1], planes[2], planes[3], planes[4], planes[5] };
    draw_reset(pp);
    if (!setjmp(done)) run68k(Start);
    if (out) fclose(out);
    if (intent_out) fclose(intent_out);
    if (planes_out) fclose(planes_out);
    if (stats) fclose(stats);
    printf("halcheck %s: %ld frames checked, RAM differs in %ld, VDP in %ld; %ld passes with display requests", argv[2], checked, bad_ram, bad_vdp, intent_passes);
    if (first_bad >= 0) printf(", first at frame %ld", first_bad);
    printf("; picture differs in %ld", bad_pic);
    if (bad_pic) printf(", first at frame %ld", first_bad_pic);
    printf("; the backend's lists give another picture in %ld", bad_disp);
    if (bad_disp) printf(", first at frame %ld", first_bad_disp);
    printf("; the drawn bitplanes differ in %ld", bad_draw);
    if (bad_draw) printf(", first at frame %ld", first_bad_draw);
    printf(" (%u screen cells drawn in %u updates, %u of them whole screens; line bytes %u planar, %u a pixel at a time)", draw.cells_drawn, draw.updates, draw.full_updates, draw.bytes_fast, draw.bytes_slow);
    if (getenv("HALCHECK_WHY")) for (int i = 0; i < 26; i++) if (disp.why[i]) printf(" why[%d]=%u", i, disp.why[i]);
    printf(" (DMAs %u, fills %u, VRAM copies %u, VRAM reads %u, sound commands %u, restarts %u, other $96 %u)\n",
           core.dmas, core.fills, core.copies, core.vram_reads, core.commands, core.restarts, core.odd_targets);
    return checked && !bad_ram && !bad_vdp && !bad_pic && !bad_disp && !bad_draw ? 0 : 1;
}
