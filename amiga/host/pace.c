/* D-2's pacing by CIA-B timer A: see pace.h. */
#include "pace.h"

#define CIAB_TALO (*(volatile UBYTE *)0xBFD400UL)
#define CIAB_TAHI (*(volatile UBYTE *)0xBFD500UL)
#define CIAB_ICR  (*(volatile UBYTE *)0xBFDD00UL)
#define CIAB_CRA  (*(volatile UBYTE *)0xBFDE00UL)
#define LVO_OpenResource  (-498)
#define LVO_AddICRVector  (-6)
#define LVO_RemICRVector  (-12)

volatile ULONG pace_count;
UWORD pace_base, pace_frac, pace_acc;                     /* (the handler's: the period in ticks, its fraction in 1/65536) */
void pace_handler(void);                                  /* prof_asm.S */
void pace_nop(void);

static void *ciab;
static struct { ULONG succ, pred; UBYTE type; BYTE pri; const char *name; void *data; void (*code)(void); } irq;
static int claimed, running;
static void *saved_l6;
static ULONG vbr, t16, genesis_mhz, eclk;
static UWORD saved_intena;
static UBYTE saved_cra;

int pace_claim(void *sysbase)
{
    ciab = (void *)CALL(sysbase, LVO_OpenResource, REG(A1, "ciab.resource"));
    if (!ciab) return 0;
    irq.type = 2;                                         /* NT_INTERRUPT */
    irq.name = "NHL94AGA pace";
    irq.code = pace_nop;                                  /* (never called: the timer runs only while the machine is ours) */
    claimed = CALL(ciab, LVO_AddICRVector, REG(D0, 0), REG(A1, &irq)) == 0;
    return claimed;
}

void pace_release(void)
{
    if (claimed) CALL(ciab, LVO_RemICRVector, REG(D0, 0), REG(A1, &irq));
    claimed = 0;
}

ULONG pace_genesis_mhz(void) { return genesis_mhz; }

void pace_start(ULONG eclock_hz, int pal)
{
    ULONG num = pal ? 53203424u : 53693175u, den = pal ? 3420u * 313u : 3420u * 262u;   /* the Genesis's vblanks a second: num / den */
    genesis_mhz = (ULONG)((unsigned long long)num * 1000u / den);
    eclk = eclock_hz;
    t16 = (ULONG)(((unsigned long long)eclock_hz * den << 16) / num);   /* E clock ticks a Genesis vblank, 16.16 */
    pace_base = (UWORD)(t16 >> 16);
    pace_frac = (UWORD)t16;
    pace_acc = 0;
    pace_count = 0;
    if (!claimed) return;
    vbr = get_vbr();
    void *volatile *vec = (void *volatile *)(vbr + 0x78);
    saved_l6 = *vec;
    saved_intena = CREG(INTENAR) & 0x2000;
    saved_cra = CIAB_CRA;
    *vec = (void *)pace_handler;
    CIAB_CRA = 0x00;                                      /* stopped */
    CIAB_ICR = 0x7F;                                      /* only timer A from CIA-B */
    (void)CIAB_ICR;
    CIAB_TALO = (UBYTE)pace_base;
    CIAB_TAHI = (UBYTE)(pace_base >> 8);
    CREG(INTREQ) = 0x2000;
    CIAB_ICR = 0x81;
    CREG(INTENA) = 0xA000;                                /* EXTER */
    CIAB_CRA = 0x11;                                      /* load the latch, start, continuous */
    running = 1;
}

void pace_stop(void)
{
    if (!running) return;
    CIAB_CRA = 0x00;
    CIAB_ICR = 0x01;
    (void)CIAB_ICR;
    CREG(INTENA) = 0x2000;
    CREG(INTREQ) = 0x2000;
    *(void *volatile *)(vbr + 0x78) = saved_l6;
    CIAB_CRA = (UBYTE)(saved_cra & 0xEE);                 /* (its mode bits, stopped: ciab.resource's owner, us, gives it back unused) */
    if (saved_intena) CREG(INTENA) = 0xA000;
    running = 0;
}

static UWORD counter(void)
{
    for (;;) {
        UBYTE h = CIAB_TAHI, l = CIAB_TALO;
        if (CIAB_TAHI == h) return (UWORD)(h << 8 | l);
    }
}

void pace_measure(int n, ULONG *vb_mhz, long *ppm)
{
    *vb_mhz = 0;
    *ppm = 0;
    if (!running || n < 1) return;
    ULONG v = vb_count;
    while (vb_count == v) { }
    ULONG p0 = pace_count;
    UWORD c0 = counter();
    v = vb_count;
    while (vb_count - v < (ULONG)n) { }
    ULONG p1 = pace_count;
    UWORD c1 = counter();
    unsigned long long ticks16 = (unsigned long long)(p1 - p0) * t16 + ((unsigned long long)c0 << 16) - ((unsigned long long)c1 << 16);
    unsigned long long eclock16 = (unsigned long long)n * 1000u * ((unsigned long long)eclk << 16);   /* (n vblanks x 1000 x the E clock in 1/65536 ticks) */
    *vb_mhz = (ULONG)(eclock16 / (ticks16 ? ticks16 : 1));
    *ppm = (long)(((long long)*vb_mhz - (long long)genesis_mhz) * 1000000 / (long long)genesis_mhz);
}
