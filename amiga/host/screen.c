/* The game's picture on the A1200's own display: see screen.h. */
#include "amiga.h"
#include "libc_amiga.h"
#include "screen.h"
#include "disp_core.h"
#include "disp_obj.h"
#include "prof.h"

UBYTE *screen_plane[6];
volatile ULONG *screen_dbg;
ULONG show_dbg;
int screen_pal;
static void *GfxBase, *IntuitionBase;
static UBYTE *planes;
static UWORD *cop[2];
static int cop_next, profiling, taken;
static struct { void *view; ULONG vbr; void *l3; UWORD intena, dmacon, adkcon; } saved;
#define COP_WORDS 512
#define BPLCON2_KILLEHB 0x0200                  /* six planes are 64 colours, not extra half-brite */

/* the Genesis's 3-bit level as 8 bits: the host build's picture table (MDRT_RGB_GPGX, native/frontend.c) */
static const UBYTE level[8] = { 0, 34, 68, 102, 136, 170, 204, 238 };

static ULONG rgb24(UWORD c)
{
    return (ULONG)level[(c >> 1) & 7] << 16 | (ULONG)level[(c >> 5) & 7] << 8 | level[(c >> 9) & 7];
}

static UWORD *mv(UWORD *c, UWORD reg, UWORD v) { *c++ = reg; *c++ = v; return c; }

static UWORD *colours(UWORD *c);
static UWORD *plane_ptrs(UWORD *c, const UBYTE *line0, int stride)
{
    for (int k = 0; k < 6; k++) {
        ULONG p = (ULONG)(line0 + k * stride);
        c = mv(c, (UWORD)(BPL1PTH + 4 * k), (UWORD)(p >> 16));
        c = mv(c, (UWORD)(BPL1PTH + 4 * k + 2), (UWORD)p);
    }
    return c;
}
static UWORD *wait_line(UWORD *c, int line, int h)       /* a copper WAIT for (line, h); lines past 255 after the $FFDF wait */
{
    static int past;
    if (line < 0) { past = 0; return c; }
    if (line > 255 && !past) { *c++ = 0xFFDF; *c++ = 0xFFFE; past = 1; }
    *c++ = (UWORD)((line & 0xFF) << 8 | (h & 0xFE) | 1);
    *c++ = 0xFFFE;
    return c;
}

static void build(UWORD *c)
{
    const struct disp *d = &disp;
    int on = d->reg[1] & 0x40, w320 = d->width == 320;
    int vstart = screen_pal ? 0x3C : 0x20;              /* 224 lines in the middle of the beam's display */
    int hstart = w320 ? 0x81 : 0xA1;                    /* 320 or 256 pixels, centred */
    c = mv(c, BPLCON0, on ? 0x6200 : 0x0200);           /* six planes, colour; none while the display is off */
    c = mv(c, BPLCON1, 0);
    c = mv(c, BPLCON2, BPLCON2_KILLEHB);
    c = mv(c, BPLCON4, 0x0011);
    c = mv(c, FMODE, 0);
    c = mv(c, DIWSTRT, (UWORD)(vstart << 8 | hstart));
    c = mv(c, DIWSTOP, (UWORD)(((vstart + 224) & 0xFF) << 8 | ((hstart + d->width) & 0xFF)));
    c = mv(c, DDFSTRT, w320 ? 0x38 : 0x48);
    c = mv(c, DDFSTOP, w320 ? 0xD0 : 0xC0);
    c = mv(c, BPL1MOD, w320 ? 0 : 8);
    c = mv(c, BPL2MOD, w320 ? 0 : 8);
    c = plane_ptrs(c, screen_plane[0], SCREEN_PLANE_BYTES);
    c = colours(c);
    if (profiling)                                        /* a copper interrupt every 8 lines of the picture: the profiler's samples */
        for (int l = vstart; l < vstart + 224 && l < 0xFF; l += 8) {
            *c++ = (UWORD)(l << 8 | 0x07); *c++ = 0xFFFE;
            c = mv(c, INTREQ, 0x8010);
        }
    *c++ = 0xFFFF;
    *c = 0xFFFE;
}

/* The scrolled scene's planes are fetched 64 bits at a time (AGA's FMODE 3): a quarter of the Chip RAM
 * slots the 16-bit fetch takes, which the blitter and the CPU get instead. The fetch window is the
 * 320-pixel one ($38 to $D0: six fetches, 48 bytes a line) and the picture's 256 pixels are shown
 * from 32 pixels into it; the planes point at a 64-bit boundary at or before the scroll position and
 * BPLCON1 delays them by the rest, up to 63 pixels (its AGA bits for 16 and 32). */
#define OBJ_FMODE3 1
void screen_prepare_obj(const UBYTE *buf, int px0, int py0)
{
    const struct disp *d = &disp;
    UWORD *c = cop[cop_next];
    int on = d->reg[1] & 0x40, vstart = screen_pal ? 0x3C : 0x20;
    int w320 = d->width == 320, hstart = w320 ? 0x81 : 0xA1;
    UWORD con1;
    int first, fetched;
    if (OBJ_FMODE3 && !w320) {
        /* 256 pixels: 64-bit fetches from $38 (six), the picture 32 pixels in; the planes at a 64-bit
         * boundary at or before the scroll position, the delay the rest (0-63) */
        int F = ((px0 + 31) >> 6) << 6, delay = F + 32 - px0;
        con1 = (UWORD)((delay & 15) | (delay & 15) << 4 | ((delay >> 4) & 3) << 10 | ((delay >> 4) & 3) << 14);
        first = F >> 3; fetched = 48;
        c = mv(c, FMODE, 3);
        c = mv(c, DDFSTRT, 0x38);
        c = mv(c, DDFSTOP, 0xD0);
    } else {
        /* 16-bit fetches, one word more than the picture for the fine scroll (256 pixels: $40-$C0, 17
         * words; 320: $30-$D0, 21) (the 64-bit fetch's window for 320 pixels is not used: it did not
         * show the picture right in an emulated run) */
        int f = px0 & 15, w0 = f ? (px0 >> 4) : (px0 >> 4) - 1, delay = f ? 16 - f : 0;
        con1 = (UWORD)(delay << 4 | delay);
        first = 2 * w0; fetched = w320 ? 42 : 34;
        c = mv(c, FMODE, 0);
        c = mv(c, DDFSTRT, w320 ? 0x30 : 0x40);
        c = mv(c, DDFSTOP, w320 ? 0xD0 : 0xC0);
    }
    c = mv(c, BPLCON0, on ? 0x6200 : 0x0200);
    c = mv(c, BPLCON1, con1);
    c = mv(c, BPLCON2, BPLCON2_KILLEHB);
    c = mv(c, BPLCON4, 0x0011);
    c = mv(c, DIWSTRT, (UWORD)(vstart << 8 | hstart));
    c = mv(c, DIWSTOP, (UWORD)(((vstart + 224) & 0xFF) << 8 | ((hstart + d->width) & 0xFF)));
    c = mv(c, BPL1MOD, (UWORD)(OBJ_LS - fetched));
    c = mv(c, BPL2MOD, (UWORD)(OBJ_LS - fetched));
    c = plane_ptrs(c, buf + py0 * OBJ_LS + first, OBJ_RW);
    c = colours(c);
    wait_line(c, -1, 0);
    int split = 256 - py0;                                /* the screen line where the buffer's line 0 comes round again */
    int split_done = split >= 224;
    for (int l = 0; l < 224; l += 8) {                    /* (the profiler's copper interrupts and the split, in line order) */
        if (!split_done && split - 1 <= l) {
            c = wait_line(c, vstart + split - 1, 0xD0);   /* (after the last fetch of the line before: 12 moves end before the next fetch) */
            c = plane_ptrs(c, buf + first, OBJ_RW);
            split_done = 1;
        }
        if (profiling && vstart + l < 0xFF) { c = wait_line(c, vstart + l, 0x06); c = mv(c, INTREQ, 0x8010); }
    }
    if (!split_done) {
        c = wait_line(c, vstart + split - 1, 0xD0);
        c = plane_ptrs(c, buf + first, OBJ_RW);
    }
    *c++ = 0xFFFF;
    *c = 0xFFFE;
}

void screen_commit(void)
{
    CREGL(COP1LC) = (ULONG)cop[cop_next];                 /* (taken by the copper at the next vertical blank) */
    cop_next ^= 1;
}

void screen_present_obj(const UBYTE *buf, int px0, int py0)
{
    screen_prepare_obj(buf, px0, py0);
    screen_commit();
}

static UWORD *colours(UWORD *c)
{
    const struct disp *d = &disp;
    /* the colours: number 0 is the backdrop (register 7); 1 to 63 are the palette's own (16, 32 and 48,
     * a palette line's transparent colour, never appear in the planes) */
    for (int bank = 0; bank < 2; bank++)
        for (int lo = 0; lo < 2; lo++) {
            c = mv(c, BPLCON3, (UWORD)(bank << 13 | 0x0C00 | (lo ? BPLCON3_LOCT : 0)));
            for (int i = 0; i < 32; i++) {
                int n = bank * 32 + i;
                ULONG v = rgb24(n ? d->pal[n] : d->pal[d->reg[7] & 0x3F]);
                UWORD w = lo ? (UWORD)(((v >> 16) & 0xF) << 8 | ((v >> 8) & 0xF) << 4 | (v & 0xF))           /* the low nibbles */
                             : (UWORD)(((v >> 20) & 0xF) << 8 | ((v >> 12) & 0xF) << 4 | ((v >> 4) & 0xF));   /* the high nibbles */
                c = mv(c, (UWORD)(COLOR00 + 2 * i), w);
            }
        }
    return mv(c, BPLCON3, 0x0C00);
}

int screen_open(int pal, int profile)
{
    screen_pal = pal;
    profiling = profile;
    GfxBase = (void *)CALL(SysBase, LVO_OpenLibrary, REG(A1, "graphics.library"), REG(D0, 39));
    IntuitionBase = (void *)CALL(SysBase, LVO_OpenLibrary, REG(A1, "intuition.library"), REG(D0, 39));
    planes = (UBYTE *)CALL(SysBase, LVO_AllocMem, REG(D0, 6 * SCREEN_PLANE_BYTES), REG(D1, MEMF_CHIP | MEMF_CLEAR));
    cop[0] = (UWORD *)CALL(SysBase, LVO_AllocMem, REG(D0, 2 * COP_WORDS * 2 + 64), REG(D1, MEMF_CHIP | MEMF_CLEAR));
    if (!GfxBase || !planes || !cop[0]) { screen_close(); return 0; }
    cop[1] = cop[0] + COP_WORDS;
    if (!screen_dbg) {                                    /* (the program may give one that lives longer) */
        screen_dbg = (volatile ULONG *)(cop[0] + 2 * COP_WORDS);
        screen_dbg[0] = 0x4E484C39; screen_dbg[1] = 0x34444247;   /* "NHL94DBG" */
    }
    screen_dbg[6] = (ULONG)screen_open;                   /* (where the program was loaded: PCs to symbols) */
    show_dbg = (ULONG)screen_dbg;
    for (int k = 0; k < 6; k++) screen_plane[k] = planes + k * SCREEN_PLANE_BYTES;
    build(cop[0]);
    saved.view = GFX_ACTIVIEW(GfxBase);
    CALL(GfxBase, LVO_LoadView, REG(A1, 0));
    CALL(GfxBase, LVO_WaitTOF);
    CALL(GfxBase, LVO_WaitTOF);
    CALL(GfxBase, LVO_OwnBlitter);
    CALL(GfxBase, LVO_WaitBlit);
    CALL(SysBase, LVO_Forbid);
    saved.vbr = get_vbr();                              /* (0 on a 68020 with Kickstart's own vectors) */
    taken = 1;
    saved.intena = CREG(INTENAR);
    saved.dmacon = CREG(DMACONR);
    saved.adkcon = CREG(ADKCONR);
    CREG(INTENA) = 0x7FFF;
    CREG(INTREQ) = 0x7FFF;
    CREG(INTREQ) = 0x7FFF;
    CREG(DMACON) = 0x7FFF;
    void *volatile *vec = (void *volatile *)(saved.vbr + 0x6C);
    saved.l3 = *vec;
    *vec = (void *)show_handler;                        /* (vblanks counted; copper interrupts sampled when profiling) */
    vb_hook = 0;
    vb_count = 0;
    CREG(BEAMCON0) = pal ? 0x0020 : 0x0000;
    CREG(FMODE) = 0;
    CREG(BPLCON3) = 0x0C00;
    CREG(BPLCON4) = 0x0011;
    for (int i = 0; i < 16; i++) CREG(SPR0PTH + 2 * i) = 0;
    CREGL(COP1LC) = (ULONG)cop[0];
    CREG(COPJMP1) = 0;
    cop_next = 1;
    CREG(DMACON) = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER;
    CREG(INTENA) = (UWORD)(INTF_SETCLR | INTF_INTEN | INTF_VERTB | (profile ? 0x0010 : 0));
    return 1;
}

void screen_present(void)
{
    build(cop[cop_next]);
    CREGL(COP1LC) = (ULONG)cop[cop_next];               /* (taken by the copper at the next vertical blank) */
    cop_next ^= 1;
}

void screen_wait_vblank(void)
{
    ULONG n = vb_count;
    while (vb_count == n) { }
}

int screen_frame_lines(void) { return screen_pal ? 312 : 262; }

volatile UWORD vb_vpos;                                   /* (prof_asm.S: the line the last vblank's interrupt saw) */
ULONG screen_lines(void)
{
    ULONG n, v, pend, at;
    do {
        n = vb_count;
        at = vb_vpos;
        v = (ULONG)(CREG(VPOSR) & 7) << 8 | (CREG(VHPOSR) >> 8);
        pend = CREG(INTREQR) & 0x0020;
    } while (n != vb_count);
    /* the lines since the last vblank's interrupt, wherever on the beam it came (an emulated A1200 has it
     * a few lines before line 0: counting from line 0 made the time go back a frame); a vblank whose
     * interrupt is waiting behind another level 3 handler counts already */
    ULONG L = (ULONG)screen_frame_lines();
    long d = (long)v - (long)at;
    if (d < -(long)L / 2) d += (long)L;                   /* (round the beam's wrap) */
    if (pend) {                                           /* the vblank is here, its interrupt not taken: just after it */
        n++;
        if (d < 0) d = 0;                                 /* (the last interrupt was taken a few lines late) */
    } else if (d < 0) d += (long)L;
    ULONG r = n * L + (ULONG)d;
    static ULONG last;                                    /* (never back: what is left of the interrupt's latency) */
    if (r < last) r = last;
    last = r;
    return r;
}

void screen_close(void)
{
    if (taken) {
        CREG(INTENA) = 0x7FFF;
        CREG(DMACON) = 0x7FFF;
        CREG(INTREQ) = 0x7FFF;
        CREG(INTREQ) = 0x7FFF;
        *(void *volatile *)(saved.vbr + 0x6C) = saved.l3;
        CREG(BEAMCON0) = (GFX_DISPLAYFLAGS(GfxBase) & DISPLAY_PAL) ? 0x0020 : 0x0000;   /* (the OS's own beam again) */
        CREGL(COP1LC) = (ULONG)GFX_COPINIT(GfxBase);
        CREG(COPJMP1) = 0;
        CREG(ADKCON) = 0x7FFF;
        CREG(ADKCON) = (UWORD)(DMAF_SETCLR | saved.adkcon);
        CREG(DMACON) = (UWORD)(DMAF_SETCLR | saved.dmacon);
        CREG(INTENA) = (UWORD)(INTF_SETCLR | saved.intena);
        CALL(GfxBase, LVO_LoadView, REG(A1, saved.view));
        CALL(GfxBase, LVO_WaitTOF);
        CALL(GfxBase, LVO_WaitTOF);
        CALL(GfxBase, LVO_DisownBlitter);
        CALL(SysBase, LVO_Permit);
        if (IntuitionBase) CALL(IntuitionBase, LVO_RethinkDisplay);
        taken = 0;
    }
    if (planes) CALL(SysBase, LVO_FreeMem, REG(A1, planes), REG(D0, 6 * SCREEN_PLANE_BYTES));
    show_dbg = 0;
    if (screen_dbg == (volatile ULONG *)(cop[0] + 2 * COP_WORDS)) screen_dbg = 0;
    if (cop[0]) CALL(SysBase, LVO_FreeMem, REG(A1, cop[0]), REG(D0, 2 * COP_WORDS * 2 + 64));
    if (IntuitionBase) CALL(SysBase, LVO_CloseLibrary, REG(A1, IntuitionBase));
    if (GfxBase) CALL(SysBase, LVO_CloseLibrary, REG(A1, GfxBase));
    planes = 0; cop[0] = cop[1] = 0; GfxBase = IntuitionBase = 0;
}
