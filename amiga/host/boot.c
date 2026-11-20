/* NHL94AGA 0.1.0, phase P1 (amiga/STATUS.md): the boot and the system-legal takeover, with no game
 * yet. It checks the machine (Kickstart V39+, a 68020 or better, AGA), draws its report with
 * graphics.library while the OS still owns the display, stops the floppy motors through
 * trackdisk.device, takes the machine (PROMPT.md section 5), shows a copper screen (a 24-bit AGA
 * gradient and a moving raster bar over the report), and gives everything back on the left mouse
 * button or a fire button. Then it checks that Chip and total free memory are what they were, and
 * writes its log to the Shell and to NHL94AGA.log beside the program (RAM: if that disk is
 * write-protected). Gate A-FIRMWARE. */
#include "amiga.h"
#include "version.h"

const char nhl94aga_vertag[] __attribute__((used)) = "\0$VER: " NHL94AGA_VSTRING;

static void *SysBase, *DOSBase, *GfxBase, *IntuitionBase;
static void *wbmsg;
static ULONG out;                        /* the Shell's output (0 when started from Workbench) */

/* ---- text: the log and the report's lines, built in buffers ---------------------------------- */

struct buf { char *p; ULONG n, cap; };
static char logmem[2048];
static struct buf logb = { logmem, 0, sizeof logmem };
static struct buf *cur = &logb;

static void put(const char *s) { while (*s && cur->n < cur->cap - 1) cur->p[cur->n++] = *s++; cur->p[cur->n] = 0; }
static void putu(ULONG v)
{
    char t[12];
    int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) { char c[2] = { t[--n], 0 }; put(c); }
}
static void put3(ULONG v) { if (v < 100) put("0"); if (v < 10) put("0"); putu(v); }   /* 40.068 */
static void puth(ULONG v, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) { char c[2] = { hex[(v >> (4 * i)) & 15], 0 }; put(c); }
}

static ULONG len(const char *s) { ULONG n = 0; while (s[n]) n++; return n; }

static void dos_write(ULONG fh, const char *s, ULONG n)
{
    if (fh && DOSBase) CALL(DOSBase, LVO_Write, REG(D1, fh), REG(D2, s), REG(D3, n));
}

static void write_log(void)
{
    dos_write(out, logb.p, logb.n);
    static const char *const paths[] = { "PROGDIR:NHL94AGA.log", "RAM:NHL94AGA.log" };
    for (unsigned i = 0; i < 2 && DOSBase; i++) {
        ULONG fh = CALL(DOSBase, LVO_Open, REG(D1, paths[i]), REG(D2, MODE_NEWFILE));
        if (!fh) continue;
        dos_write(fh, logb.p, logb.n);
        CALL(DOSBase, LVO_Close, REG(D1, fh));
        if (out) { static const char m[] = "Log written to "; dos_write(out, m, len(m)); dos_write(out, paths[i], len(paths[i])); dos_write(out, "\n", 1); }
        break;
    }
}

/* a refusal: to the Shell, or in a requester when started from Workbench */
static void refuse(const char *why)
{
    put("REFUSED: "); put(why); put("\n");
    if (!out && IntuitionBase) {
        ULONG es[5] = { 20, 0, (ULONG)"NHL94AGA", (ULONG)why, (ULONG)"OK" };
        CALL(IntuitionBase, LVO_EasyRequestArgs, REG(A0, 0), REG(A1, es), REG(A2, 0), REG(A3, 0));
    }
}

/* ---- the floppy motors, off through trackdisk.device before the machine is taken -------------- */

static void motors_off(void)
{
    void *port = (void *)CALL(SysBase, LVO_CreateMsgPort);
    if (!port) return;
    void *io = (void *)CALL(SysBase, LVO_CreateIORequest, REG(A0, port), REG(D0, 48));
    if (io) {
        for (ULONG unit = 0; unit < 4; unit++) {
            if (CALL(SysBase, LVO_OpenDevice, REG(A0, "trackdisk.device"), REG(D0, unit), REG(A1, io), REG(D1, 0)) != 0) continue;
            IO_COMMAND(io) = TD_MOTOR;
            IO_LENGTH(io) = 0;
            CALL(SysBase, LVO_DoIO, REG(A1, io));
            CALL(SysBase, LVO_CloseDevice, REG(A1, io));
        }
        CALL(SysBase, LVO_DeleteIORequest, REG(A0, io));
    }
    CALL(SysBase, LVO_DeleteMsgPort, REG(A0, port));
}

/* ---- the screen: one 320 x 256 plane for the report, a copper list for the rest --------------- */

#define W 320
#define H 256
#define PLANE_BYTES (W / 8 * H)
#define COPPER_WORDS 1024
static UWORD *copper;                     /* Chip RAM */
static UBYTE *plane;                      /* Chip RAM */
static UWORD *bar_wait_on, *bar_wait_off; /* the raster bar's two WAITs, moved each vblank */
static int bar_y = 0xD8, bar_dir = 1;       /* (below the gradient: copper WAITs must go down the screen) */
static volatile ULONG frames_shown;

static void draw_report(const char *const *lines, int count)
{
    UBYTE bm[BITMAP_SIZE], rp[RASTPORT_SIZE];
    CALL(GfxBase, LVO_InitBitMap, REG(A0, bm), REG(D0, 1), REG(D1, W), REG(D2, H));
    BM_PLANE(bm, 0) = plane;
    CALL(GfxBase, LVO_InitRastPort, REG(A1, rp));
    RP_BITMAP(rp) = bm;
    CALL(GfxBase, LVO_SetFont, REG(A1, rp), REG(A0, GFX_DEFAULTFONT(GfxBase)));
    CALL(GfxBase, LVO_SetAPen, REG(A1, rp), REG(D0, 1));
    for (int i = 0; i < count; i++) {
        ULONG n = len(lines[i]);                      /* (no layer clips Text: keep it inside the plane) */
        while (n && CALL(GfxBase, LVO_TextLength, REG(A1, rp), REG(A0, lines[i]), REG(D0, n)) > W - 32) n--;
        CALL(GfxBase, LVO_Move, REG(A1, rp), REG(D0, 16), REG(D1, 20 + 12 * i));
        CALL(GfxBase, LVO_Text, REG(A1, rp), REG(A0, lines[i]), REG(D0, n));
    }
    CALL(GfxBase, LVO_WaitBlit);
}

static UWORD *cmove(UWORD *c, UWORD reg, UWORD v) { *c++ = reg; *c++ = v; return c; }
static UWORD *cwait(UWORD *c, UWORD line) { *c++ = (UWORD)(line << 8 | 0x07); *c++ = 0xFFFE; return c; }
/* COLOR00 as 24 bits: the high nibbles with LOCT clear, then the low nibbles with LOCT set */
static UWORD *ccolor24(UWORD *c, UWORD reg, ULONG rgb)
{
    UWORD hi = (UWORD)((rgb >> 12 & 0xF00) | (rgb >> 8 & 0x0F0) | (rgb >> 4 & 0x00F));
    UWORD lo = (UWORD)((rgb >> 8 & 0xF00) | (rgb >> 4 & 0x0F0) | (rgb & 0x00F));
    c = cmove(c, BPLCON3, 0x0C00);
    c = cmove(c, reg, hi);
    c = cmove(c, BPLCON3, 0x0C00 | BPLCON3_LOCT);
    c = cmove(c, reg, lo);
    return cmove(c, BPLCON3, 0x0C00);
}

static void build_copper(int pal)
{
    UWORD *c = copper;
    c = cmove(c, FMODE, 0);
    c = cmove(c, BPLCON0, 0x1200);                    /* 1 plane, colour on */
    c = cmove(c, BPLCON1, 0);
    c = cmove(c, BPLCON2, 0);
    c = cmove(c, BPLCON3, 0x0C00);
    c = cmove(c, BPLCON4, 0x0011);
    c = cmove(c, BPL1MOD, 0);
    c = cmove(c, BPL2MOD, 0);
    c = cmove(c, DIWSTRT, 0x2C81);
    c = cmove(c, DIWSTOP, pal ? 0x2CC1 : 0xF4C1);
    c = cmove(c, DDFSTRT, 0x0038);
    c = cmove(c, DDFSTOP, 0x00D0);
    c = cmove(c, BPL1PTH, (UWORD)((ULONG)plane >> 16));
    c = cmove(c, BPL1PTL, (UWORD)(ULONG)plane);
    c = ccolor24(c, COLOR00, 0x000000);
    c = ccolor24(c, COLOR01, 0xFFFFFF);
    for (int i = 0; i < 64; i++) {                    /* the AGA test: 64 lines of 24-bit steps */
        c = cwait(c, (UWORD)(0x2C + 0x60 + i));       /* below the report's lines */
        c = ccolor24(c, COLOR00, (ULONG)(i * 4) << 16 | (ULONG)(i * 2) << 8 | (ULONG)(255 - i * 4));
    }
    c = cwait(c, 0x2C + 0x60 + 64);
    c = ccolor24(c, COLOR00, 0x000000);
    bar_wait_on = c; c = cwait(c, (UWORD)bar_y);
    c = ccolor24(c, COLOR00, 0xFFCC00);
    bar_wait_off = c; c = cwait(c, (UWORD)(bar_y + 6));
    c = ccolor24(c, COLOR00, 0x000000);
    *c++ = 0xFFFF; *c++ = 0xFFFE;
}

/* each vblank: the raster bar one line on, between the gradient and the bottom */
static void on_vblank(void)
{
    bar_y += bar_dir;
    if (bar_y >= 0xE8) bar_dir = -1;                 /* (0xEE is the bar's last line: NTSC shows to 0xF3) */
    if (bar_y <= 0xD0) bar_dir = 1;
    bar_wait_on[0] = (UWORD)(bar_y << 8 | 0x07);
    bar_wait_off[0] = (UWORD)((bar_y + 6) << 8 | 0x07);
    frames_shown++;
}

/* ---- the takeover and the giving back --------------------------------------------------------- */

static struct {
    void *view;
    ULONG vbr;
    void *l3;
    UWORD intena, dmacon, adkcon;
} saved;

static void take(int pal)
{
    saved.view = GFX_ACTIVIEW(GfxBase);
    CALL(GfxBase, LVO_LoadView, REG(A1, 0));
    CALL(GfxBase, LVO_WaitTOF);
    CALL(GfxBase, LVO_WaitTOF);
    CALL(GfxBase, LVO_OwnBlitter);
    CALL(GfxBase, LVO_WaitBlit);
    CALL(SysBase, LVO_Forbid);
    saved.vbr = get_vbr();
    saved.intena = CREG(INTENAR);
    saved.dmacon = CREG(DMACONR);
    saved.adkcon = CREG(ADKCONR);
    CREG(INTENA) = 0x7FFF;
    CREG(INTREQ) = 0x7FFF;
    CREG(INTREQ) = 0x7FFF;
    CREG(DMACON) = 0x7FFF;
    void *volatile *vec = (void *volatile *)(saved.vbr + 0x6C);
    saved.l3 = *vec;
    *vec = (void *)l3_handler;
    CREG(BEAMCON0) = pal ? 0x0020 : 0x0000;          /* the machine's own 15 kHz beam */
    CREG(FMODE) = 0;
    CREG(BPLCON3) = 0x0C00;
    CREG(BPLCON4) = 0x0011;
    for (int i = 0; i < 16; i++) CREG(SPR0PTH + 2 * i) = 0;
    CREGL(COP1LC) = (ULONG)copper;
    CREG(COPJMP1) = 0;
    vb_hook = on_vblank;
    CREG(DMACON) = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER;
    CREG(INTENA) = INTF_SETCLR | INTF_INTEN | INTF_VERTB;
}

static void give_back(void)
{
    CREG(INTENA) = 0x7FFF;
    CREG(DMACON) = 0x7FFF;
    CREG(INTREQ) = 0x7FFF;
    CREG(INTREQ) = 0x7FFF;
    vb_hook = 0;
    *(void *volatile *)(saved.vbr + 0x6C) = saved.l3;
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
    if (IntuitionBase) CALL(IntuitionBase, LVO_RethinkDisplay);   /* (after Permit: Intuition locks its base) */
}

/* ---- main ----------------------------------------------------------------------------------- */

static void *open_lib(const char *name, ULONG version) { return (void *)CALL(SysBase, LVO_OpenLibrary, REG(A1, name), REG(D0, version)); }
static void close_lib(void *base) { if (base) CALL(SysBase, LVO_CloseLibrary, REG(A1, base)); }
static ULONG avail(ULONG flags) { return CALL(SysBase, LVO_AvailMem, REG(D1, flags)); }

LONG amiga_main(void);
LONG amiga_main(void)
{
    SysBase = *(void **)4;
    void *me = (void *)CALL(SysBase, LVO_FindTask, REG(A1, 0));
    if (!PR_CLI(me)) {                                /* from Workbench: its message first */
        CALL(SysBase, LVO_WaitPort, REG(A0, PR_MSGPORT(me)));
        wbmsg = (void *)CALL(SysBase, LVO_GetMsg, REG(A0, PR_MSGPORT(me)));
    }
    LONG rc = 20;
    DOSBase = open_lib("dos.library", 36);
    IntuitionBase = open_lib("intuition.library", 36);
    GfxBase = open_lib("graphics.library", 33);
    if (DOSBase && !wbmsg) out = CALL(DOSBase, LVO_Output);

    UWORD kv = EXEC_VERSION(SysBase), ks = EXEC_SOFTVER(SysBase), attn = EXEC_ATTNFLAGS(SysBase);
    UBYTE chiprev = GfxBase ? GFX_CHIPREVBITS0(GfxBase) : 0;
    int pal = GfxBase && (GFX_DISPLAYFLAGS(GfxBase) & DISPLAY_PAL);
    put(NHL94AGA_VSTRING "\n");
    put("Kickstart "); putu(kv); put("."); put3(ks);
    put(", CPU "); put(attn & AFF_68040 ? "68040+" : attn & AFF_68030 ? "68030" : attn & AFF_68020 ? "68020" : "68000/010");
    put(attn & AFF_68881 ? " with FPU" : ""); put(", chipset bits $"); puth(chiprev, 2);
    put(pal ? ", PAL" : ", NTSC"); put("\n");

    const char *why = 0;
    if (!DOSBase || !IntuitionBase || !GfxBase) why = "a system library would not open";
    else if (kv < 39) why = "needs Kickstart 3.0 (V39) or newer";
    else if (!(attn & AFF_68020)) why = "needs a 68020 or better";
    else if ((chiprev & (GFXF_AA_ALICE | GFXF_AA_LISA)) != (GFXF_AA_ALICE | GFXF_AA_LISA)) why = "needs the AGA chipset (Alice and Lisa)";

    ULONG chip_before = avail(MEMF_CHIP), any_before = avail(MEMF_ANY), fast_before = avail(MEMF_FAST);
    if (!why) {
        copper = (UWORD *)CALL(SysBase, LVO_AllocMem, REG(D0, COPPER_WORDS * 2), REG(D1, MEMF_CHIP | MEMF_CLEAR));
        plane = (UBYTE *)CALL(SysBase, LVO_AllocMem, REG(D0, PLANE_BYTES), REG(D1, MEMF_CHIP | MEMF_CLEAR));
        if (!copper || !plane) why = "not enough Chip RAM";
    }
    if (why) {
        refuse(why);
    } else {
        static char l1[48], l2[48], l3[48];
        struct buf b1 = { l1, 0, sizeof l1 }, b2 = { l2, 0, sizeof l2 }, b3 = { l3, 0, sizeof l3 };
        cur = &b1; put("NHL94AGA " NHL94AGA_VERSION "  (P1 boot test)");
        cur = &b2; put("Kickstart "); putu(kv); put("."); put3(ks); put("  AGA  "); put(pal ? "PAL" : "NTSC");
        cur = &b3; put("Chip RAM free: "); putu(chip_before >> 10); put(" KB");
        cur = &logb;
        const char *lines[4] = { l1, l2, l3, "Left mouse or fire: back to the OS" };
        draw_report(lines, 4);
        build_copper(pal);
        motors_off();

        take(pal);
        while (!(CIAA_PRA & 0x40) || !(CIAA_PRA & 0x80)) {}   /* (a button still held from the start) */
        while ((CIAA_PRA & 0xC0) == 0xC0) {}
        ULONG shown = frames_shown, counted = vb_count;
        give_back();

        put("Took the machine and gave it back: "); putu(counted); put(" vblanks, "); putu(shown); put(" copper frames\n");
        rc = 0;
    }
    if (copper) CALL(SysBase, LVO_FreeMem, REG(A1, copper), REG(D0, COPPER_WORDS * 2));
    if (plane) CALL(SysBase, LVO_FreeMem, REG(A1, plane), REG(D0, PLANE_BYTES));
    ULONG chip_after = avail(MEMF_CHIP), any_after = avail(MEMF_ANY), fast_after = avail(MEMF_FAST);
    put("Chip free before "); putu(chip_before); put(", after "); putu(chip_after);
    put("; all free before "); putu(any_before); put(", after "); putu(any_after);
    put("; other before "); putu(fast_before); put(", after "); putu(fast_after); put("\n");
    int same = chip_before == chip_after && any_before == any_after;
    put(rc ? "A-FIRMWARE: not run (refused)\n" : same ? "A-FIRMWARE memory check: PASS\n" : "A-FIRMWARE memory check: FAIL\n");
    if (!rc && !same) rc = 10;
    write_log();

    close_lib(GfxBase);
    close_lib(IntuitionBase);
    close_lib(DOSBase);
    if (wbmsg) {
        CALL(SysBase, LVO_Forbid);
        CALL(SysBase, LVO_ReplyMsg, REG(A1, wbmsg));
    }
    return rc;
}
