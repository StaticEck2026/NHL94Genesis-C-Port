/* The sampling profiler of the verify build (NHL94VFY <script> PROFILE): while a script plays, the
 * machine is taken the system-legal way (as amiga/host/boot.c does), the display is off, and a copper
 * list raises a copper interrupt on 8 lines of every frame (400 samples a second on PAL). The level 3
 * handler (prof_handler, start.S) counts the interrupted PC's 16-byte block of the program's code.
 * Given back afterwards; the counts go to NAME.prof for amiga/tools/profreport.py. While the machine
 * is taken the E clock does not run, so a profiling run reports no pass times. */
#include "amiga.h"
#include "libc_amiga.h"
#include "prof.h"

ULONG prof_base, prof_size, prof_other;
ULONG *prof_hist;
extern char _start[], __text_end[];
void prof_handler(void);

static void *GfxBase, *IntuitionBase;
static UWORD *copper;
static struct { void *view; ULONG vbr; void *l3; UWORD intena, dmacon, adkcon; } saved;
#define COPPER_BYTES 256

int prof_start(void)
{
    prof_base = (ULONG)_start;
    prof_size = (ULONG)(__text_end - _start);
    prof_other = 0;
    ULONG n = (prof_size >> 4) + 1;
    prof_hist = (ULONG *)CALL(SysBase, LVO_AllocMem, REG(D0, n * 4), REG(D1, MEMF_ANY | MEMF_CLEAR));
    copper = (UWORD *)CALL(SysBase, LVO_AllocMem, REG(D0, COPPER_BYTES), REG(D1, MEMF_CHIP | MEMF_CLEAR));
    GfxBase = (void *)CALL(SysBase, LVO_OpenLibrary, REG(A1, "graphics.library"), REG(D0, 39));
    IntuitionBase = (void *)CALL(SysBase, LVO_OpenLibrary, REG(A1, "intuition.library"), REG(D0, 39));
    if (!prof_hist || !copper || !GfxBase) return 0;
    UWORD *c = copper;
    *c++ = BPLCON0; *c++ = 0x0200;                        /* (no bitplanes) */
    for (UWORD line = 0x10; line <= 0xF0; line += 0x20) {
        *c++ = (UWORD)(line << 8 | 0x07); *c++ = 0xFFFE;
        *c++ = INTREQ; *c++ = 0x8010;                     /* SETCLR | COPER */
    }
    *c++ = 0xFFFF; *c++ = 0xFFFE;
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
    *vec = (void *)prof_handler;
    CREGL(COP1LC) = (ULONG)copper;
    CREG(COPJMP1) = 0;
    CREG(DMACON) = DMAF_SETCLR | DMAF_MASTER | DMAF_COPPER;
    CREG(INTENA) = INTF_SETCLR | INTF_INTEN | 0x0010;     /* COPER */
    return 1;
}

void prof_stop(void)
{
    CREG(INTENA) = 0x7FFF;
    CREG(DMACON) = 0x7FFF;
    CREG(INTREQ) = 0x7FFF;
    CREG(INTREQ) = 0x7FFF;
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
    if (IntuitionBase) CALL(IntuitionBase, LVO_RethinkDisplay);
}

/* NAME.prof: "PROF", the bucket count, the samples outside the program, then the counts (big-endian) */
void prof_save(const char *path)
{
    ULONG n = (prof_size >> 4) + 1;
    ULONG head[3] = { 0x50524F46, n, prof_other };
    ULONG fh = CALL(DOSBase, LVO_Open, REG(D1, path), REG(D2, MODE_NEWFILE));
    if (fh) {
        CALL(DOSBase, LVO_Write, REG(D1, fh), REG(D2, head), REG(D3, sizeof head));
        CALL(DOSBase, LVO_Write, REG(D1, fh), REG(D2, prof_hist), REG(D3, n * 4));
        CALL(DOSBase, LVO_Close, REG(D1, fh));
    }
    CALL(SysBase, LVO_FreeMem, REG(A1, prof_hist), REG(D0, n * 4));
    CALL(SysBase, LVO_FreeMem, REG(A1, copper), REG(D0, COPPER_BYTES));
    if (IntuitionBase) CALL(SysBase, LVO_CloseLibrary, REG(A1, IntuitionBase));
    CALL(SysBase, LVO_CloseLibrary, REG(A1, GfxBase));
}
