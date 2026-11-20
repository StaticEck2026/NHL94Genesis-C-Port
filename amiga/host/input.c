/* The Amiga's own controllers as Genesis pads: see input.h. The assignment (D-6):
 *
 *   pad 0  the joystick in port 2 (or the CD32 pad there), and the keyboard
 *   pad 1  the joystick in port 1 (the mouse port)
 *   pad 2  the four-player adapter's first joystick, pad 3 its second (4 Way Play)
 *
 *   joystick       fire 1 = B, fire 2 = A; C on Space or Return, Start on P (the keyboard)
 *   CD32 pad       red = B, blue = C, yellow = A, green = A, play = Start
 *   keyboard       cursor keys; Z = A, X = B, C, Space or Return = C; P = Start; Esc quits
 *
 * Nothing is debounced or repeated: the game sees the lines as they are when it reads them. */
#include "amiga.h"
#include "input.h"

#define JOY0DAT 0x00A
#define JOY1DAT 0x00C
#define POTINP  0x016
#define POTGO   0x034
#define CIAA_DDRA (*(volatile UBYTE *)0xBFE201UL)
#define CIAA_PRB  (*(volatile UBYTE *)0xBFE101UL)     /* the parallel port's data lines */
#define CIAA_DDRB (*(volatile UBYTE *)0xBFE301UL)
#define CIAA_SDR  (*(volatile UBYTE *)0xBFEC01UL)
#define CIAA_ICR  (*(volatile UBYTE *)0xBFED01UL)
#define CIAA_CRA  (*(volatile UBYTE *)0xBFEE01UL)
#define CIAB_PRA  (*(volatile UBYTE *)0xBFD000UL)     /* BUSY (bit 0), POUT (bit 1), SEL (bit 2) */
#define CIAB_DDRA (*(volatile UBYTE *)0xBFD200UL)

uint8_t input_cd32[2];
struct input_cfg input_cfg = { 0, 0, 1, 1 };            /* (the host menu's: D-6 as it stands) */
#define CIAA_PRA_LED (*(volatile UBYTE *)0xBFE001UL)    /* bit 1: the power LED dim and the audio filter off when set */
static volatile uint8_t keys[128];                       /* held, by raw key code (the level 2 handler's) */
static void *saved_l2;
static ULONG vbr;
static UBYTE saved_ddra, saved_ddrb, saved_ciabddra, saved_icr_mask, saved_led;
void input_l2(void);                                      /* (input_asm in start.S's style: below, as a C function called by the stub) */
void l2_handler(void);

/* the level 2 interrupt (PORTS): a key from CIA-A's serial register, then the handshake (the line
 * held low for at least 85 us) */
void input_l2(void)
{
    UBYTE icr = CIAA_ICR;                                 /* (reading it clears it) */
    if (icr & 0x08) {
        UBYTE raw = CIAA_SDR;
        CIAA_CRA |= 0x40;                                 /* SPMODE output: the handshake starts */
        UBYTE code = (UBYTE)~(raw >> 1 | raw << 7);
        keys[code & 0x7F] = !(code & 0x80);
        UWORD l0 = CREG(VHPOSR) >> 8;                     /* about two raster lines: 127 us */
        int lines = 0;
        while (lines < 3) { UWORD l = CREG(VHPOSR) >> 8; if (l != l0) { l0 = l; lines++; } }
        CIAA_CRA &= (UBYTE)~0x40;
    }
    CREG(INTREQ) = 0x0008;
    CREG(INTREQ) = 0x0008;
}

void input_start(void)
{
    for (int i = 0; i < 128; i++) keys[i] = 0;
    vbr = get_vbr();
    void *volatile *vec = (void *volatile *)(vbr + 0x68);
    saved_l2 = *vec;
    *vec = (void *)l2_handler;
    saved_ddra = CIAA_DDRA;
    saved_ddrb = CIAA_DDRB;
    saved_ciabddra = CIAB_DDRA;
    saved_led = CIAA_PRA_LED & 0x02;
    if (input_cfg.filter) CIAA_PRA_LED &= (UBYTE)~0x02; else CIAA_PRA_LED |= 0x02;
    CIAA_DDRB = 0x00;                                     /* the parallel port's lines as inputs (the adapter) */
    CIAB_DDRA &= (UBYTE)~0x07;                         /* BUSY, POUT and SEL as inputs */
    CREG(POTGO) = 0xFF00;                                 /* the second fire buttons readable */
    (void)CIAA_ICR;
    CIAA_ICR = 0x7F;                                      /* only the serial port's interrupt from CIA-A */
    CIAA_ICR = 0x88;
    CREG(INTREQ) = 0x0008;
    CREG(INTENA) = 0x8008;                                /* PORTS */
    saved_icr_mask = 1;
}

void input_stop(void)
{
    CREG(INTENA) = 0x0008;
    CREG(INTREQ) = 0x0008;
    if (saved_icr_mask) {
        CIAA_ICR = 0x08;                                  /* (the OS sets its own mask again as it takes the interrupt back) */
        *(void *volatile *)(vbr + 0x68) = saved_l2;
        CIAA_DDRA = saved_ddra;
        CIAA_DDRB = saved_ddrb;
        CIAB_DDRA = saved_ciabddra;
        CIAA_PRA_LED = (UBYTE)((CIAA_PRA_LED & ~0x02) | saved_led);
        saved_icr_mask = 0;
    }
}

/* a 9-pin joystick's directions from its JOYxDAT word */
static uint16_t stick(UWORD j)
{
    uint16_t b = 0;
    if (j & 0x0002) b |= PAD_RIGHT;
    if (j & 0x0200) b |= PAD_LEFT;
    if (((j >> 1) ^ j) & 0x0001) b |= PAD_DOWN;
    if (((j >> 1) ^ j) & 0x0100) b |= PAD_UP;
    return b;
}

/* the CD32 pad in port 2 (port = 1) or port 1 (port = 0): its seven buttons through the shift
 * register (pin 5 low to load, pin 6 the clock, pin 9 the data), then its two identification bits.
 * -> the buttons (bit 0 blue, 1 red, 2 yellow, 3 green, 4 right, 5 left, 6 play), or -1 for no CD32 pad */
static int cd32(int port)
{
    UBYTE fire = port ? 0x80 : 0x40;
    UWORD potgo = port ? 0x6F00 : 0xF600, datbit = port ? 0x4000 : 0x0400;
    CIAA_DDRA |= fire;                                    /* pin 6 an output: the clock */
    CIAA_PRA &= (UBYTE)~fire;
    CREG(POTGO) = potgo;                                  /* pin 5 an output, low: load */
    int bits = 0;
    for (int i = 0; i < 9; i++) {
        for (volatile int d = 0; d < 4; d++) (void)CIAA_PRA;   /* (a few CIA cycles for the line to settle) */
        if (!(CREG(POTINP) & datbit)) bits |= 1 << i;     /* (pressed: low) */
        CIAA_PRA |= fire;
        CIAA_PRA &= (UBYTE)~fire;
    }
    CREG(POTGO) = 0xFF00;
    CIAA_DDRA &= (UBYTE)~fire;
    /* the identification: the eighth bit reads high (not pressed) and the ninth low */
    if ((bits & 0x180) != 0x100) return -1;
    return bits & 0x7F;
}

uint16_t input_pad(int pad)
{
    uint16_t b = 0;
    if (pad == 0 || pad == 1) {
        int port = (pad == 0) != input_cfg.swap ? 1 : 0;  /* pad 0: port 2, pad 1: port 1 (the other way round when swapped) */
        b = stick(CREG(port ? JOY1DAT : JOY0DAT));
        int c = cd32(port);
        input_cd32[port] = c >= 0;
        if (c >= 0) {
            if (c & 0x02) b |= PAD_B;
            if (c & 0x01) b |= PAD_C;
            if (c & 0x0C) b |= PAD_A;
            if (c & 0x40) b |= PAD_START;
        } else {
            if (!(CIAA_PRA & (port ? 0x80 : 0x40))) b |= PAD_B;
            if (!(CREG(POTINP) & (port ? 0x4000 : 0x0400))) b |= PAD_A;
        }
        if (pad == input_cfg.keyboard) {                  /* the keyboard is pad 0 too (or pad 1) */
            if (keys[0x4C]) b |= PAD_UP;
            if (keys[0x4D]) b |= PAD_DOWN;
            if (keys[0x4F]) b |= PAD_LEFT;
            if (keys[0x4E]) b |= PAD_RIGHT;
            if (keys[0x31]) b |= PAD_A;
            if (keys[0x32]) b |= PAD_B;
            if (keys[0x33] || keys[0x40] || keys[0x44]) b |= PAD_C;
            if (keys[0x19]) b |= PAD_START;
        }
    } else if (input_cfg.fourway) {                       /* the four-player adapter on the parallel port */
        UBYTE d = (UBYTE)~CIAA_PRB, s = (UBYTE)~CIAB_PRA;
        int sh = pad == 2 ? 0 : 4;
        if (d >> sh & 1) b |= PAD_UP;
        if (d >> sh & 2) b |= PAD_DOWN;
        if (d >> sh & 4) b |= PAD_LEFT;
        if (d >> sh & 8) b |= PAD_RIGHT;
        if (s & (pad == 2 ? 0x01 : 0x04)) b |= PAD_B;     /* fire: BUSY for the first, SEL for the second */
    }
    if ((b & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN)) b &= (uint16_t)~(PAD_UP | PAD_DOWN);
    if ((b & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) b &= (uint16_t)~(PAD_LEFT | PAD_RIGHT);
    return b;
}

int input_quit(void) { return keys[0x45]; }
