/* The host menu (PROMPT.md 7.6): see hostmenu.h. Drawn by the display backend's own screen (the
 * composed picture's six planes, two of them used) in the system's default font, copied from the
 * Kickstart's graphics.library while the OS runs; read with the Amiga's controllers (input.c). */
#include <string.h>
#include <stdlib.h>
#include "amiga.h"
#include "libc_amiga.h"
#include "hostmenu.h"
#include "input.h"
#include "screen.h"
#include "disp_core.h"
#include "version.h"

static UBYTE font[95][8];                                 /* ' ' to '~', eight rows of eight pixels */
static int have_font;

int hostmenu_font(void *sysbase)
{
    void *gfx = (void *)CALL(sysbase, -552, REG(A1, "graphics.library"), REG(D0, 36));   /* OpenLibrary */
    if (!gfx) return 0;
    const UBYTE *tf = *(const UBYTE *const *)((const UBYTE *)gfx + 154);   /* GfxBase->DefaultFont */
    if (tf) {
        UWORD ysize = *(const UWORD *)(tf + 20), modulo = *(const UWORD *)(tf + 38);
        UBYTE lo = tf[32], hi = tf[33];
        const UBYTE *data = *(const UBYTE *const *)(tf + 34);
        const ULONG *loc = *(const ULONG *const *)(tf + 40);
        for (int ch = 32; ch < 127; ch++) {
            int i = ch >= lo && ch <= hi ? ch - lo : hi - lo + 1;   /* (the font's own glyph for what it lacks) */
            int off = (int)(loc[i] >> 16), w = (int)(loc[i] & 0xFFFF);
            if (w > 8) w = 8;
            for (int r = 0; r < 8 && r < ysize; r++) {
                UBYTE b = 0;
                for (int x = 0; x < w; x++) {
                    int bit = off + x;
                    if (data[r * modulo + (bit >> 3)] & (0x80 >> (bit & 7))) b |= (UBYTE)(0x80 >> x);
                }
                font[ch - 32][r] = b;
            }
        }
        have_font = 1;
    }
    CALL(sysbase, -414, REG(A1, gfx));                    /* CloseLibrary */
    return have_font;
}

/* ---- the preferences file: "N94P", then a byte each ------------------------------------------- */

void hostmenu_load(struct hostprefs *p, const char *path)
{
    ULONG n = 0;
    UBYTE *b = libc_load(path, &n);
    if (b && n >= 9 && !memcmp(b, "N94P", 4)) {
        p->pal = b[4] & 1;
        p->swap = b[5] & 1;
        p->keyboard = b[6] & 1;
        p->fourway = b[7] & 1;
        p->filter = b[8] & 1;
    }
    free(b);
}

int hostmenu_save(const struct hostprefs *p, const char *path)
{
    UBYTE b[9] = { 'N', '9', '4', 'P', (UBYTE)p->pal, (UBYTE)p->swap, (UBYTE)p->keyboard, (UBYTE)p->fourway, (UBYTE)p->filter };
    return libc_save(path, b, sizeof b);
}

/* ---- the screen -------------------------------------------------------------------------------- */

static void text(int col, int row, const char *s, int colour)   /* colour 1 (plane 0) or 3 (planes 0 and 1) */
{
    for (; *s && col < 40; s++, col++) {
        int ch = (UBYTE)*s;
        if (ch < 32 || ch > 126) ch = '?';
        for (int r = 0; r < 8; r++) {
            UBYTE g = have_font ? font[ch - 32][r] : (ch == ' ' ? 0 : 0x7E);
            screen_plane[0][(row * 8 + r) * 40 + col] = g;
            screen_plane[1][(row * 8 + r) * 40 + col] = colour == 3 ? g : 0;
        }
    }
}

static void centre(int row, const char *s, int colour) { text((40 - (int)strlen(s)) / 2, row, s, colour); }

enum { M_REGION, M_PORTS, M_KEYBOARD, M_FOURWAY, M_FILTER, M_START, M_QUIT, M_N };

static void draw(const struct hostprefs *p, int at)
{
    for (int k = 0; k < 6; k++) memset(screen_plane[k], 0, SCREEN_PLANE_BYTES);
    centre(2, "NHL 94 on the Amiga 1200", 3);
    centre(3, "version " NHL94AGA_VERSION, 1);
    centre(5, "a port of NHL94Genesis", 1);
    static const char *const label[M_N] = { "Region", "Joysticks", "Keyboard", "4 Way Play adapter", "Audio filter", "Start the game", "Quit" };
    const char *value[M_N] = {
        p->pal ? "50 Hz (PAL)" : "60 Hz (NTSC)",
        p->swap ? "port 1 is P1" : "port 2 is P1",
        p->keyboard ? "P2" : "P1",
        p->fourway ? "on" : "off",
        p->filter ? "on" : "off",
        "", "" };
    for (int i = 0; i < M_N; i++) {
        int row = 9 + i * 2 + (i >= M_START ? 1 : 0), c = i == at ? 3 : 1;
        text(3, row, i == at ? ">" : " ", 3);
        text(5, row, label[i], c);
        if (*value[i]) text(26, row, value[i], c);
    }
    centre(25, "Up, Down: choose   Left, Right: change", 1);
    centre(26, "Fire, Return or Start: go   Esc: quit", 1);
}

int hostmenu_run(struct hostprefs *p)
{
    struct disp *d = &disp;
    d->width = 320;
    d->reg[1] = 0x40;
    for (int i = 0; i < 64; i++) d->pal[i] = 0;
    d->pal[0] = 0x0400;                                   /* dark blue (the Genesis's colour word: blue in bits 9-11) */
    d->pal[1] = 0x0AAA;                                   /* light grey */
    d->pal[3] = 0x00EE;                                   /* yellow */
    int at = M_START, dirty = 1, result = -1;
    uint16_t was = 0xFFFF;                                /* (nothing counts until every button is let go) */
    while (result < 0) {
        if (dirty) { draw(p, at); screen_present(); dirty = 0; }
        screen_wait_vblank();
        uint16_t b = input_pad(0) | input_pad(1), now = (uint16_t)(b & ~was);
        was = b;
        if (input_quit()) result = 0;
        if (now & PAD_UP) { at = (at + M_N - 1) % M_N; dirty = 1; }
        if (now & PAD_DOWN) { at = (at + 1) % M_N; dirty = 1; }
        if (now & (PAD_LEFT | PAD_RIGHT | PAD_A | PAD_B | PAD_C | PAD_START)) {
            int go = !!(now & (PAD_A | PAD_B | PAD_C | PAD_START));
            switch (at) {
            case M_REGION: p->pal ^= 1; break;
            case M_PORTS: p->swap ^= 1; break;
            case M_KEYBOARD: p->keyboard ^= 1; break;
            case M_FOURWAY: p->fourway ^= 1; break;
            case M_FILTER: p->filter ^= 1; break;
            case M_START: if (go) result = 1; break;
            case M_QUIT: if (go) result = 0; break;
            }
            dirty = 1;
        }
    }
    for (int k = 0; k < 6; k++) memset(screen_plane[k], 0, SCREEN_PLANE_BYTES);
    d->reg[1] = 0;
    screen_present();
    screen_wait_vblank();
    return result;
}
