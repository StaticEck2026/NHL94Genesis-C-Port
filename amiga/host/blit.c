/* One blit: see blit.h. */
#include "blit.h"

uint32_t blit_count, blit_words;

#if defined(__mc68000__) || defined(__mc68020__)
#include "amiga.h"
#define BLTCON0 0x040
#define BLTCON1 0x042
#define BLTAFWM 0x044
#define BLTALWM 0x046
#define BLTCPT  0x048
#define BLTBPT  0x04C
#define BLTAPT  0x050
#define BLTDPT  0x054
#define BLTSIZV 0x05C
#define BLTSIZH 0x05E
#define BLTCMOD 0x060
#define BLTBMOD 0x062
#define BLTAMOD 0x064
#define BLTDMOD 0x066

static void hw_wait(void)
{
    (void)CREG(DMACONR);                                  /* (the first read after a start can miss BBUSY) */
    while (CREG(DMACONR) & 0x4000) { }
}

/* the registers the blitter does not change (its controls, masks and modulos) are written only when
 * they differ from the last blit's: a custom-chip write waits behind the blitter for the bus, and a
 * match's pass has about 80 blits (the pointers advance as a blit runs, and the size starts it: those
 * are written every time) */
static struct { uint16_t con0, con1, fwm, lwm; int16_t amod, bmod, cmod, dmod; int valid; } sh;
#define SET(reg, field, v) do { if (!sh.valid || sh.field != (v)) { sh.field = (v); CREG(reg) = (UWORD)(v); } } while (0)
static void start(const struct blit *bl)
{
    SET(BLTCON0, con0, bl->con0);
    SET(BLTCON1, con1, bl->con1);
    SET(BLTAFWM, fwm, bl->fwm);
    SET(BLTALWM, lwm, bl->lwm);
    if (bl->a) { CREGL(BLTAPT) = (ULONG)bl->a; SET(BLTAMOD, amod, bl->amod); }
    if (bl->b) { CREGL(BLTBPT) = (ULONG)bl->b; SET(BLTBMOD, bmod, bl->bmod); }
    if (bl->c) { CREGL(BLTCPT) = (ULONG)bl->c; SET(BLTCMOD, cmod, bl->cmod); }
    CREGL(BLTDPT) = (ULONG)bl->d;
    SET(BLTDMOD, dmod, bl->dmod);
    sh.valid = 1;
    CREG(BLTSIZV) = bl->h;
    CREG(BLTSIZH) = bl->w;                                /* (starts it) */
}

/* the queue: the main program adds at the tail, the interrupt takes from the head. Who starts the
 * blitter claims it by a compare-and-swap of running from 0 to 1 (the 68020's CAS): the main program
 * after adding, the interrupt after finding the queue empty and letting it go; so neither side has
 * to turn the interrupt off (two custom-chip writes a blit before) */
#define QN 256
static struct blit q[QN];
static volatile int qh, qt, running, async;
static void (*volatile done_fn)(void);
#define BARRIER() __asm__ __volatile__("" ::: "memory")

void blit_async(int on)
{
    blit_wait();
    async = on;
    sh.valid = 0;                                         /* (the OS's blits came between) */
    CREG(INTREQ) = 0x0040;
    CREG(INTENA) = on ? 0x8040 : 0x0040;                  /* BLIT */
}

static void claim_and_start(void)                         /* the blitter idle: start the queue's head if no one else has */
{
    if (qh == qt || !__sync_bool_compare_and_swap(&running, 0, 1)) return;
    if (qh == qt) { running = 0; return; }
    start(&q[qh]);
    qh = (qh + 1) % QN;
}

static void call_done(void)
{
    void (*f)(void) = done_fn;
    if (f && __sync_bool_compare_and_swap(&done_fn, f, 0)) f();
}

void blit_irq(void)                                       /* the blitter finished: the next one, or the queue's end */
{
    if (qh != qt) { start(&q[qh]); qh = (qh + 1) % QN; return; }
    running = 0;
    BARRIER();
    claim_and_start();                                    /* (one added meanwhile) */
    if (!running && qh == qt) call_done();
}

void blit_run(const struct blit *bl)
{
    blit_count++;
    blit_words += (uint32_t)bl->w * bl->h;
    if (!async) { hw_wait(); sh.valid = 0; start(bl); return; }
    while ((qt + 1) % QN == qh) { }                       /* (full: the interrupt takes them) */
    q[qt] = *bl;
    BARRIER();
    qt = (qt + 1) % QN;
    BARRIER();
    if (!running) claim_and_start();
}

void blit_wait(void)
{
    if (async) while (running || qh != qt) { }
    hw_wait();
}

void blit_when_done(void (*fn)(void))
{
    if (!async) { hw_wait(); fn(); return; }
    done_fn = fn;
    BARRIER();
    if (!running && qh == qt) call_done();                /* (else the interrupt calls it at the queue's end) */
}
#else
/* the host's model of the same blit */
static inline uint16_t rd(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

void blit_wait(void) { }
void blit_when_done(void (*fn)(void)) { fn(); }
void blit_async(int on) { (void)on; }
void blit_irq(void) { }

void blit_run(const struct blit *bl)
{
    const uint8_t *a = bl->a, *b = bl->b, *c = bl->c;
    uint8_t *d = bl->d;
    int ash = bl->con0 >> 12, bsh = bl->con1 >> 12, mt = bl->con0 & 0xFF;
    for (int y = 0; y < bl->h; y++) {
        uint16_t aold = 0, bold = 0;                      /* the words before, for the shifts */
        for (int x = 0; x < bl->w; x++) {
            uint16_t av = 0, bv = 0, cv = 0;
            if (bl->con0 & BC0_USEA) { av = rd(a); a += 2; }
            else av = 0xFFFF;
            if (x == 0) av &= bl->fwm;
            if (x == bl->w - 1) av &= bl->lwm;
            uint16_t as = (uint16_t)(((uint32_t)aold << 16 | av) >> ash);
            aold = av;
            if (bl->con0 & BC0_USEB) { bv = rd(b); b += 2; }
            uint16_t bs = (uint16_t)(((uint32_t)bold << 16 | bv) >> bsh);
            bold = bv;
            if (bl->con0 & BC0_USEC) { cv = rd(c); c += 2; }
            uint16_t out = 0;
            for (int bit = 0; bit < 16; bit++) {
                int i = ((as >> bit) & 1) << 2 | ((bs >> bit) & 1) << 1 | ((cv >> bit) & 1);
                out |= (uint16_t)(((mt >> i) & 1) << bit);
            }
            if (bl->con0 & BC0_USED) { d[0] = (uint8_t)(out >> 8); d[1] = (uint8_t)out; d += 2; }
        }
        if (bl->con0 & BC0_USEA) a += bl->amod;
        if (bl->con0 & BC0_USEB) b += bl->bmod;
        if (bl->con0 & BC0_USEC) c += bl->cmod;
        if (bl->con0 & BC0_USED) d += bl->dmod;
    }
    blit_count++;
    blit_words += (uint32_t)bl->w * bl->h;
}
#endif
