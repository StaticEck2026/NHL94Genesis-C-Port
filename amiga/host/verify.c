/* NHL94VFY, phase P5 (amiga/STATUS.md): the game's own C on the A1200, with no display or sound
 * yet, played over the repository's input scripts. After every pass the work RAM hash
 * (amiga/host/hash.h) is checked against the reference (amiga/tools/halcheck's, from the host
 * build's golden runs), and the pass's time is measured with the E clock: gate A-LOGIC and the
 * feasibility measurement of PROMPT.md 6.5.
 *
 * It runs under the OS (Forbid while a script plays, so other tasks do not skew the times) and
 * reads everything first: the user's ROM (one file, or two halves on two disks named NHL94ROMA:
 * and NHL94ROMB:), each script, each reference. Between scripts the game is powered on again by
 * restoring the program's data and clearing its BSS, as a fresh load would. Results go to the
 * Shell, to the serial port (exec RawPutChar, 9600 baud) as it runs, and to NHL94VFY.log.
 * Left mouse button: stop the current script. */
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "amiga.h"
#include "libc_amiga.h"
#include "version.h"
#include "game.h"
#include "mem.h"
#include "rom.h"
#include "rules.h"
#include "rosters.h"
#include "hal_core.h"
#include "hash.h"
#include "intent.h"
#include "sndcmd.h"
#include "sound.h"
#include "audio.h"
#include "prof.h"
#include "disp_core.h"
#include "disp_draw.h"
#include "disp_obj.h"
#include "blit.h"
#include "screen.h"
#include "input.h"
#include "pace.h"
#include "hostmenu.h"

const char nhl94vfy_vertag[] __attribute__((used)) = "\0$VER: NHL94VFY " NHL94AGA_VERSION " (" NHL94AGA_DATE ") " NHL94AGA_COMMIT;

extern char __data_start[], __data_end[], __bss_start[], __bss_end[];
extern void (*__init_array_start[])(void), (*__init_array_end[])(void);
void call_with_stack(void (*fn)(void), void *lower, void *upper);

#define LVO_RawPutChar (-516)
#define LVO_ReadEClock (-60)
#define ROM_CRC32 0x9438F5DDu
#define FRAME_US 16688u                         /* one NTSC Genesis frame: 1 / 59.923 Hz */

static const struct test { const char *name; long frames; const char *script; } tests[] = {
    { "attract", 7199, 0 },
    { "full_game", 15999, "full_game_30s.txt" },
    { "playoff_game", 14999, "playoff_game_30s.txt" },
    { "records_game", 15699, "records_game_30s.txt" },
    { "pause_menus", 8599, "pause_menus.txt" },
    { "shootout_menus", 4799, "shootout_menus.txt" },
    { "name_entry", 2999, "name_entry.txt" },
};
#define NTESTS (int)(sizeof tests / sizeof tests[0])

/* what survives the game's power on: kept in exec memory, its address in .keep */
struct result { long frames, bad, first_bad, max_us, max_frame, first_us, over90, over100, aborted, noref; unsigned long long sum_us; uint32_t dmas, commands;
                long draw_max, draw_max_frame, late, late_shown; unsigned long long draw_sum;
                long skipped, slip; };   /* (SKIP: passes not drawn, and vblanks the game's time fell behind by) */   /* (SHOW: the drawing's time, passes shown late) */
struct keep {
    void *sys, *dos, *timer;
    const uint8_t *rom;
    char *data_snap;
    struct result r[NTESTS];
    char log[6144];
    ULONG loglen;
    ULONG eclock_hz;
    int only;                                   /* -1: every test */
    int intent;                                 /* NHL94VFY <script> INTENT: check the display requests (A-DISPLAY-INTENT) */
    int audio;                                  /* NHL94VFY <script> AUDIO: check the sound commands (A-AUDIO-CMD) */
    int show, pal;                              /* NHL94VFY <script> SHOW [PAL]: the picture on screen (P6.5), checked by its planes' hash */
    long frames_max;
    int play;                                   /* NHL94VFY PLAY: the game itself, on screen, played with the Amiga's controllers; Esc quits */
    int timing;                                 /* NHL94VFY <script> SHOW TIME: no hashes (the speed alone) */
    int skip;                                   /* PLAY, and NHL94VFY <script> SHOW SKIP: a pass not drawn when the next one is due (D-18) */
    volatile ULONG *dbg;                        /* the debug block in Chip RAM (screen.h), for the whole run */                            /* NHL94VFY <script> ... FRAMES n: stop after n passes */
    int profile, profiling;                     /* NHL94VFY <script> PROFILE [FIRST LAST]: asked for, and running */
    int prof_logic;                             /* ... PROFILE ... LOGIC: the passes' logic only, not their drawing */
    int prof_draw;                              /* ... PROFILE ... DRAW: the passes' drawing only */
    long prof_first, prof_last;                 /* the passes sampled (all of them when none are named) */
};
static struct keep *K __attribute__((section(".keep")));

/* ---- the log: the Shell, the serial port, and the file at the end ----------------------------- */

static int machine_taken;                        /* (SHOW: the Shell and exec's serial output need the OS running: the log only) */
static void logs(const char *s)
{
    if (!machine_taken) libc_write(s);
    for (const char *p = s; *p; p++) {
        if (!machine_taken) CALL(K->sys, LVO_RawPutChar, REG(D0, (UBYTE)*p));
        if (K->loglen < sizeof K->log - 1) K->log[K->loglen++] = *p;
    }
}
static void logf_(const char *f, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, f);
    libc_vformat(b, sizeof b, f, ap);
    va_end(ap);
    logs(b);
}

/* ---- time: the E clock through timer.device ---------------------------------------------------- */

static ULONG eclock(void)
{
    ULONG ev[2];
    CALL(K->timer, LVO_ReadEClock, REG(A0, ev));
    return ev[1];
}
static long to_us(ULONG ticks) { return (long)((unsigned long long)ticks * 1000000u / K->eclock_hz); }

/* ---- one script --------------------------------------------------------------------------------- */

static struct { long frame; uint16_t b; } *script;
static long script_len, frames_wanted, test_index;
static const uint32_t *ref;
static uint32_t *over;                           /* the passes longer than a frame: (frame, microseconds) pairs, for NAME.over */
static long nover;
static int intent_on, show_on;                   /* (K->intent and K->show, beside the hooks that test them for every word) */
static ULONG t_lines, last_vb, due;               /* (SKIP: the vblank the pass was due to start at) */
static int due_set, paced;
static uint32_t draw_wait_total;
static ULONG due_first, ideal_ticks;                    /* (SKIP: the clock at the first pass, and the vblanks the passes are due: one each, or as many as their own logic took) */                       /* (D-2: the passes counted by CIA-B timer A at the Genesis rate, not by the beam) */
static uint32_t obj_lines(void) { return (uint32_t)screen_lines(); }   /* (disp_obj.c's clock: raster lines) */
static inline ULONG clock_now(void) { return paced ? pace_count : vb_count; }
static uint16_t *times;
static UBYTE *obj_chip, *win_planes, *snd_chip;
static int sound_on;                             /* (SHOW: sound.c plays the game's sound on Paula) */             /* (SHOW: the scrolled scene's bitmaps in Chip RAM; the window for the planes' hash) */
static int was_obj;                          /* (SHOW: each pass's logic and drawing in raster lines, for NAME.drw) */                   /* (SHOW: the time base is the beam, raster lines; the E clock stops while the machine is taken) */
static long line_ns(void) { return screen_pal ? 64000 : 63556; }
#define OVER_MAX 4096
static uint32_t *made;                           /* (no reference on the disk: the hashes, written for the host to compare) */
static ULONG ref_len, t_start;
static jmp_buf stop;
static struct result *R;

uint16_t plat_buttons(int pad, long h)
{
    if (K->play) return input_pad(pad);
    static long at;                                       /* (the script's entry in force: frames only go up) */
    if (pad) return 0;
    while (at + 1 < script_len && script[at + 1].frame <= h) at++;
    return script_len && script[at].frame <= h ? script[at].b : 0;
}

void plat_frame_done(long frame)
{
    if (K->profiling) prof_sample(0);                     /* (this check is not the game's time) */
    long us, draw_us = 0;
    uint32_t h;
    int drawn = 1;
    if (show_on) {                                        /* the pass, then its picture: drawn, then shown from the next vblank */
        screen_dbg[2] = (ULONG)frame;
        screen_dbg[5] = 1;
        ULONG t1 = screen_lines();
        if (K->prof_logic) prof_on = 0;                   /* (LOGIC: the drawing is not sampled) */
        if (K->prof_draw) prof_on = frame >= K->prof_first && frame <= K->prof_last;   /* (DRAW: only the drawing) */
        /* D-18: the game's passes keep their vblanks; a pass whose next one is due already is not drawn
         * (what it changed is drawn with the next pass drawn). The next pass is due a vblank after this
         * one, or as many as its logic took (a long pass is the game's own time, as on the cartridge) */
        ULONG next_due = 0;
        if (K->skip) {
            ULONG T = (ULONG)screen_frame_lines(), f = (t1 - t_lines + T - 1) / T;
            next_due = due + (f ? f : 1);
            ideal_ticks += f ? f : 1;
            if (clock_now() >= next_due) drawn = 0;
        }
        int objmode = obj_chip && obj_scene();
        static uint32_t draw_waits;                       /* (the blitter waits inside the drawing, against those of the check after it) */
        uint32_t w0 = blit_wait_time;
        if (!drawn) { R->skipped++; }
        else if (objmode) { obj_update(); was_obj = 1; draw_waits += blit_wait_time - w0; draw_wait_total = draw_waits; }
        else {
            if (was_obj) { obj_leave(); draw_invalidate(); was_obj = 0; }
            draw_update();
        }
        screen_dbg[5] = 2;
        ULONG t2 = screen_lines();
        prof_on = 0;
        us = (long)((t1 - t_lines) * (ULONG)line_ns() / 1000);
        draw_us = (long)((t2 - t1) * (ULONG)line_ns() / 1000);
        if (times && frame <= frames_wanted) {
            ULONG l = t1 - t_lines, dl = t2 - t1;
            times[2 * (frame - 1)] = (uint16_t)(l > 0xFFFF ? 0xFFFF : l);
            times[2 * (frame - 1) + 1] = (uint16_t)(dl > 0xFFFF ? 0xFFFF : dl);
        }
        if (frame > 1) {
            R->draw_sum += (unsigned long long)draw_us;
            if (draw_us > R->draw_max) { R->draw_max = draw_us; R->draw_max_frame = frame; }
            if (vb_count > last_vb + 1) { R->late++; if (disp.reg[1] & 0x40) { int lit = 0; for (int i = 0; i < 64; i++) lit |= disp.pal[i]; if (lit) R->late_shown++; } }
        }
        if (!drawn) h = 0;
        else if ((ref || made) && objmode && win_planes) {
            uint8_t *const wp[6] = { win_planes, win_planes + DRAW_PLANE_BYTES, win_planes + 2 * DRAW_PLANE_BYTES,
                                     win_planes + 3 * DRAW_PLANE_BYTES, win_planes + 4 * DRAW_PLANE_BYTES, win_planes + 5 * DRAW_PLANE_BYTES };
            obj_window(wp);
            h = planes_hash32(wp);
        } else h = (ref || made) ? planes_hash32(screen_plane) : 0;
        if (!drawn) { }
        else if (objmode) {                               /* (shown once its blits are done: a mark in the queue, the next pass's blits behind it) */
            while (screen_pending) { }                    /* (the last pass's list still waiting for its commit: not built over) */
            screen_prepare_obj(obj.buf[obj.shown], obj.px0, obj.py0);
            screen_pending = 1;
            blit_when_done(screen_commit);
        }
        else { blit_wait(); screen_present(); }
        if (sound_on) { sound_frame(); audio_apply(); }
        screen_dbg[5] = 3;
        if (K->skip) {
            while (clock_now() < next_due) { }            /* (the next pass's vblank, unless it is past) */
            ULONG now = clock_now();
            if (now > next_due + 3) { R->slip += (long)(now - next_due); next_due = now; }   /* (too far behind to catch up: that time is lost) */
            due = next_due;
        } else screen_wait_vblank();
        screen_dbg[5] = 0;
        last_vb = vb_count;
    } else {
        us = to_us(eclock() - t_start);
        h = K->audio ? sndcmd_take() : intent_on ? intent_take() : ram_hash32((const uint8_t *)&g);
    }
    us += draw_us;
    if (frame == 1) R->first_us = us;                     /* (power on: the cartridge's checksum of 1 MB) */
    else {
        R->sum_us += (unsigned long long)us;
        if (us > R->max_us) { R->max_us = us; R->max_frame = frame; }
        if (us * 10 > (long)FRAME_US * 9) R->over90++;
        if (us > (long)FRAME_US) {
            R->over100++;
            if (over && nover < OVER_MAX) {               /* (bit 31: the screen is black after it, a screen being built) */
                uint32_t dark = 0x80000000u;
                for (int i = 0; i < 64; i++) if (core.cram[i]) dark = 0;
                over[2 * nover] = (uint32_t)frame;
                over[2 * nover + 1] = (uint32_t)us | dark;
                nover++;
            }
        }
    }
    if (drawn && (ULONG)frame <= ref_len && h != ref[frame - 1]) { if (!R->bad) R->first_bad = frame; R->bad++; }
    if (made && frame <= frames_wanted) made[frame - 1] = h;
    R->frames = frame;
    if (frame % 1200 == 0 && !K->profile) logf_("  %s: frame %ld, %ld differ\n", tests[test_index].name, frame, R->bad);
    if (K->play ? input_quit() : !(CIAA_PRA & 0x40)) { R->aborted = 1; longjmp(stop, 1); }
    if (frame >= frames_wanted) longjmp(stop, 1);
    if (K->profiling) prof_sample(frame + 1 >= K->prof_first && frame + 1 <= K->prof_last);
    if (show_on) {
        t_lines = screen_lines();
        if (!due_set) { due = clock_now(); due_set = 1; due_first = due; ideal_ticks = 0; }
        if (K->profile) prof_on = !K->prof_draw && frame + 1 >= K->prof_first && frame + 1 <= K->prof_last;
    }
    else t_start = eclock();
}

/* the display requests: with SHOW to the display backend (disp_core.c); with INTENT into the pass's
 * hash (intent.h) */
void plat_vram_byte(uint16_t a, uint8_t b) { if (show_on) disp_vram_byte(a, b); if (intent_on) intent_byte(a, b); }
uint8_t plat_vram_read(uint16_t a) { return show_on ? disp_byte(a) : 0; }
void plat_vram_block(uint16_t a, const uint8_t *src, uint32_t words) { if (show_on) disp_vram_block(a, src, words); if (intent_on) intent_block(a, src, words); }
void plat_vram_copy(uint16_t a, uint16_t src, uint32_t n, uint8_t step) { if (show_on) disp_vram_copy(a, src, n, step); if (intent_on) intent_copy(a, src, n, step); }
void plat_vsram(int i, uint16_t v) { if (show_on) disp_vsram(i, v); if (intent_on) intent_vsram(i, v); }
void plat_vram_word(uint16_t a, uint16_t w) { if (show_on) disp_vram_word(a, w); if (intent_on) intent_word(a, w); }
void plat_vram_long(uint16_t a, uint32_t v) { if (show_on) disp_vram_long(a, v); if (intent_on) intent_long(a, v); }
void plat_vram_fill(uint16_t a, uint8_t b, uint32_t n, uint8_t step) { if (show_on) disp_vram_fill(a, b, n, step); if (intent_on) intent_fill(a, b, n, step); }
void plat_cram(int i, uint16_t c) { if (show_on) disp_cram(i, c); if (intent_on) intent_cram(i, c); }
void plat_reg(int r, uint8_t v) { if (show_on) disp_reg(r, v); if (intent_on) intent_reg(r, v); }
/* the sound requests: with SHOW (and PLAY) to the sound backend (sound.c, Paula); with AUDIO into
 * the pass's hash (sndcmd.h) */
void plat_sound_command(const uint8_t *z) { if (sound_on) sound_command(z); if (K->audio) sndcmd_command(z); }
void plat_sound_restart(const uint8_t *z) { if (sound_on) sound_restart(z); if (K->audio) sndcmd_restart(z); }
void plat_sound_reset(int running) { if (sound_on) sound_reset(running); if (K->audio) sndcmd_reset(running); }
void plat_psg(uint8_t v) { if (sound_on) sound_psg(v); if (K->audio) sndcmd_psg(v); }

static void stopped(const char *why) { (void)why; R->aborted = 2; longjmp(stop, 1); }

/* the game powered on: the program's data and BSS as LoadSeg left them, the constructors again */
static void power_on(void)
{
    struct keep *k = K;
    memcpy(__data_start, k->data_snap, (size_t)(__data_end - __data_start));
    memset(__bss_start, 0, (size_t)(__bss_end - __bss_start));
    SysBase = k->sys;
    DOSBase = k->dos;
    for (void (**c)(void) = __init_array_start; c < __init_array_end; c++) (*c)();
    libc_abort_hook = stopped;
}

static int parse_script(char *t)
{
    static const struct { const char *n; uint16_t bit; } names[] = {
        { "UP", 1 }, { "DOWN", 2 }, { "LEFT", 4 }, { "RIGHT", 8 }, { "B", 0x10 }, { "C", 0x20 }, { "A", 0x40 }, { "START", 0x80 },
    };
    long cap = 0;
    for (char *p = t; *p; p++) if (*p == ':') cap++;
    script = malloc((size_t)(cap + 1) * sizeof *script);
    if (!script) return 0;
    script_len = 0;
    for (char *p = t; *p;) {
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (*p < '0' || *p > '9') { p++; continue; }
        long f = 0;
        while (*p >= '0' && *p <= '9') f = f * 10 + (*p++ - '0');
        if (*p != ':') continue;
        p++;
        uint16_t b = 0;
        while (*p && *p != ',' && *p != '\n' && *p != '\r') {
            char *e = p;
            while (*e && *e != '+' && *e != ',' && *e != '\n' && *e != '\r') e++;
            for (int i = 0; i < 8; i++)
                if ((size_t)(e - p) == strlen(names[i].n) && !memcmp(p, names[i].n, (size_t)(e - p))) b |= names[i].bit;
            p = *e == '+' ? e + 1 : e;
        }
        script[script_len].frame = f;
        script[script_len++].b = b;
    }
    return 1;
}

static void run_one(void)
{
    const struct test *t = &tests[test_index];
    char path[96];
    ULONG n;
    script = NULL;
    script_len = 0;
    if (t->script) {
        libc_format(path, sizeof path, "PROGDIR:scripts/%s", t->script);
        char *text = libc_load(path, &n);
        if (!text || !parse_script(text)) { logf_("%s: cannot read %s\n", t->name, path); R->aborted = 3; return; }
        free(text);
    }
    const char *ext = K->show ? "p32" : K->audio ? "a32" : K->intent ? "i32" : "h32";
    libc_format(path, sizeof path, "PROGDIR:refs/%s.%s", t->name, ext);
    ref = K->play || K->timing ? NULL : libc_load(path, &n);
    ref_len = ref ? n / 4 : 0;
    made = ref || K->play || K->timing ? NULL : malloc((size_t)t->frames * 4);
    if (!ref && !K->play && !K->timing) logf_("%s: no reference on this disk: its hashes go to PROGDIR:%s.%s for amiga/tools/h32compare.py\n", t->name, t->name, ext);
    if (K->intent) logf_("%s: INTENT: the hashes are of the display requests, and the pass times include the hashing\n", t->name);
    over = K->profile || K->play ? NULL : malloc(OVER_MAX * 8);
    nover = 0;
    frames_wanted = K->play ? 0x7FFFFFFF : K->frames_max && K->frames_max < t->frames ? K->frames_max : t->frames;
    memcpy(rules, rule_classic, sizeof rules);            /* the vanilla game (native/vanilla/stubs.c) */
    rom_cart = K->rom;
    rom_art = NULL;
    rom_limit = ROM_SIZE;
    rosters_retail();
    if (K->play) {                                        /* the host menu (PROMPT.md 7.6), its choices kept beside the program */
        struct hostprefs hp = { K->pal, 0, 0, 1, 1 };
        hostmenu_load(&hp, "PROGDIR:NHL94.prefs");
        if (K->pal) hp.pal = 1;                           /* (PLAY PAL asks for it) */
        hostmenu_font(SysBase);
        CALL(DOSBase, -198, REG(D1, 50));                 /* Delay(1 s): the floppy motor stops before the machine is taken */
        int go = 1;
        if (screen_open(hp.pal, 0)) {
            machine_taken = 1;
            input_start();
            go = hostmenu_run(&hp);
            input_stop();
            screen_close();
            machine_taken = 0;
        }
        if (!hostmenu_save(&hp, "PROGDIR:NHL94.prefs")) logf_("host menu: NHL94.prefs could not be written (a write-protected disk?)\n");
        if (!go) { logf_("host menu: Quit\n"); R->aborted = 1; return; }
        K->pal = hp.pal;
        input_cfg.swap = hp.swap;
        input_cfg.keyboard = hp.keyboard;
        input_cfg.fourway = hp.fourway;
        input_cfg.filter = hp.filter;
        logf_("host menu: %s, player 1 in port %d, the keyboard player %d, 4 Way Play adapter %s, audio filter %s\n", hp.pal ? "50 Hz (PAL)" : "60 Hz (NTSC)",
              hp.swap ? 1 : 2, hp.keyboard + 1, hp.fourway ? "on" : "off", hp.filter ? "on" : "off");
    }
    core_power_on(K->play && K->pal);                     /* (PLAY PAL: a PAL console, 50 passes a second; the checks are NTSC's) */
    intent_on = K->intent;
    intent_take();
    if (K->play) {                                        /* the save RAM: records and playoffs, kept beside the program (P10) */
        ULONG sn;
        uint8_t *sv = libc_load("PROGDIR:NHL94.sav", &sn);
        if (sv && sn == sizeof core.sram) { memcpy(core.sram, sv, sizeof core.sram); logf_("save RAM: NHL94.sav read\n"); }
        free(sv);
        core.sram_dirty = 0;
    }
    CALL(DOSBase, -198, REG(D1, 150));                    /* Delay(3 s): the floppy motor stops before Forbid */
    int profiling = !K->show && K->profile && prof_start();
    K->profiling = profiling;
    if (K->show) {
        if (K->profile && !prof_alloc()) K->profile = 0;
        screen_dbg = K->dbg;                              /* (power_on cleared the BSS) */
        paced = K->skip && K->eclock_hz && pace_claim(SysBase);
        if (K->skip && !paced) logf_("%s: pacing: CIA-B timer A is not free; the passes follow the beam\n", t->name);
        if (!screen_open(K->pal, K->profile)) { logf_("%s: SHOW: not enough Chip RAM for the screen\n", t->name); R->aborted = 3; return; }
        disp_reset();
        disp_cartridge(rom_ptr(0), ROM_SIZE);                  /* (I-9: its tiles converted once) */
        draw_reset(screen_plane);
        obj_chip = (UBYTE *)CALL(SysBase, LVO_AllocMem, REG(D0, OBJ_CHIP_BYTES), REG(D1, MEMF_CHIP | MEMF_CLEAR));
        win_planes = malloc(6 * DRAW_PLANE_BYTES);
        if (obj_chip) { obj_reset(obj_chip); blit_async(1); obj_clock = obj_lines; blit_clock = obj_lines; }
        else logf_("%s: SHOW: no Chip RAM for the scrolled scene; every picture composed\n", t->name);
        was_obj = 0;
        snd_chip = (UBYTE *)CALL(SysBase, LVO_AllocMem, REG(D0, SOUND_CHIP_BYTES), REG(D1, MEMF_CHIP | MEMF_CLEAR));
        sound_on = snd_chip && sound_start(snd_chip, SOUND_CHIP_BYTES, K->rom);
        if (sound_on) audio_start();
        times = K->play ? NULL : malloc((size_t)frames_wanted * 4);
        if (times) memset(times, 0, (size_t)frames_wanted * 4);
        show_on = 1;
        machine_taken = 1;
        if (K->play) input_start();
        if (paced) {
            pace_start(K->eclock_hz, K->pal);
            ULONG mhz;
            long ppm;
            pace_measure(50, &mhz, &ppm);
            int lock = ppm >= -500 && ppm <= 500;         /* D-2: within 0.05%, the beam itself */
            logf_("%s: pacing: the vblank %lu.%03lu Hz, the Genesis %lu.%03lu Hz, %ld ppm: %s\n", t->name, mhz / 1000, mhz % 1000,
                  pace_genesis_mhz() / 1000, pace_genesis_mhz() % 1000, ppm, lock ? "beam-locked" : "paced by CIA-B timer A");
            if (lock) { pace_stop(); paced = 0; }
        }
        due_set = 0;
        last_vb = vb_count;
        t_lines = screen_lines();
    } else if (profiling) prof_sample(K->prof_first <= 1);
    else CALL(SysBase, LVO_Forbid);
    t_start = eclock();
    if (!setjmp(stop)) run68k(Start);
    if (show_on) {
        show_on = 0;
        prof_on = 0;
        if (K->dbg) K->dbg[5] = 10;
        blit_async(0);
        if (K->play) input_stop();
        pace_stop();
        screen_close();
        pace_release();
        paced = 0;
        if (K->dbg) K->dbg[5] = 11;
        machine_taken = 0;
        if (K->play && core.sram_dirty) {
            if (libc_save("PROGDIR:NHL94.sav", core.sram, sizeof core.sram)) logf_("save RAM: NHL94.sav written\n");
            else logf_("save RAM: NHL94.sav could not be written (a write-protected disk?)\n");
        }
        if (sound_on) audio_stop();
        sound_on = 0;
        if (snd_chip) CALL(SysBase, LVO_FreeMem, REG(A1, snd_chip), REG(D0, SOUND_CHIP_BYTES));
        snd_chip = NULL;
        if (obj_chip) CALL(SysBase, LVO_FreeMem, REG(A1, obj_chip), REG(D0, OBJ_CHIP_BYTES));
        obj_chip = NULL;
        free(win_planes);
        win_planes = NULL;
        if (K->profile) {
            libc_format(path, sizeof path, "PROGDIR:%s.prof", t->name);
            prof_write(path);
        }
        if (times) {                                      /* NAME.drw: big-endian (logic lines, drawing lines) a pass */
            libc_format(path, sizeof path, "PROGDIR:%s.drw", t->name);
            if (K->dbg) K->dbg[5] = 12;
            libc_save(path, times, (ULONG)R->frames * 4);
            if (K->dbg) K->dbg[5] = 13;
            free(times);
            times = NULL;
        }
        logf_("%s: SHOW: drawing mean %ld us, max %ld us at frame %ld; %ld passes shown late (%ld with a picture); %ld not drawn (SKIP), the game %ld vblanks behind; %lu line bytes composed, %lu whole screens; scrolled scene %lu passes, %lu rebuilds, %lu CPU lines\n",
              t->name, (long)(R->draw_sum / (unsigned long long)(R->frames > 1 ? R->frames - 1 : 1)), R->draw_max, R->draw_max_frame,
              R->late, R->late_shown, R->skipped, R->slip, (unsigned long)draw.cells_drawn, (unsigned long)draw.full_updates,
              (unsigned long)obj.updates, (unsigned long)obj.rebuilds, (unsigned long)obj.cpu_lines);
        if (obj.updates) logf_("%s: SHOW: the scrolled scene waited %lu raster lines in all for the last pass's blits at the start of its updates (%lu a pass)\n",
                               t->name, (unsigned long)obj.wait_start, (unsigned long)(obj.wait_start / obj.updates));
        if (K->skip) {
            ULONG took = clock_now() - due_first;
            logf_("%s: pace: %ld passes took %lu Genesis vblanks; the cartridge's pace (a vblank a pass, or the pass's own logic time) %lu: the game ran at %lu.%lu%% of its speed\n",
                  t->name, R->frames, took, ideal_ticks, took ? ideal_ticks * 100u / took : 0, took ? (ideal_ticks * 1000u / took) % 10 : 0);
        }
        if (obj.updates) logf_("%s: SHOW: the scrolled scene by phase, raster lines a pass: changes %lu, restores %lu, sprite list %lu, low layers %lu, high layers %lu, CPU lines %lu\n", t->name,
                               (unsigned long)(obj.ph[0] / obj.updates), (unsigned long)(obj.ph[1] / obj.updates), (unsigned long)(obj.ph[2] / obj.updates),
                               (unsigned long)(obj.ph[3] / obj.updates), (unsigned long)(obj.ph[4] / obj.updates), (unsigned long)(obj.ph[5] / obj.updates));
        if (obj.updates) logf_("%s: SHOW: phase 0 in all, raster lines: the rebuilds %lu (%lu of them), the rink's changed cells %lu, plane A's changes %lu, the first wait %lu\n", t->name,
                               (unsigned long)obj.ph_rebuild, (unsigned long)obj.rebuilds, (unsigned long)obj.ph_rink, (unsigned long)obj.ph_a, (unsigned long)obj.wait_start);
        if (obj.rebuilds) logf_("%s: SHOW: the rebuilds by part, raster lines: the stale tiles before %lu, the rink %lu, plane A %lu, the wait %lu, the buffers copied %lu; plane A alone drawn again %lu times (the window moved)\n", t->name,
                                (unsigned long)obj.rb[0], (unsigned long)obj.rb[1], (unsigned long)obj.rb[2], (unsigned long)obj.rb[3], (unsigned long)obj.rb[4], (unsigned long)obj.a_redos);
        logf_("%s: SHOW: waits for the blitter: %lu raster lines in blit_wait (%lu of them inside the scrolled scene's drawing), %lu with the queue full\n", t->name,
              (unsigned long)blit_wait_time, (unsigned long)draw_wait_total, (unsigned long)blit_full_time);
    } else if (profiling) {
        K->profiling = 0;
        prof_stop();
        libc_format(path, sizeof path, "PROGDIR:%s.prof", t->name);
        prof_save(path);
        logf_("%s: profile written to %s (pass times are not measured while profiling)\n", t->name, path);
    } else {
        CALL(SysBase, LVO_Permit);
    }
    R->dmas = core.dmas;
    R->commands = core.commands;
    if (over) {
        libc_format(path, sizeof path, "PROGDIR:%s.over", t->name);
        libc_save(path, over, (ULONG)nover * 8);
        free(over);
    }
    if (made) {
        libc_format(path, sizeof path, "PROGDIR:%s.%s", t->name, ext);
        if (!libc_save(path, made, (ULONG)R->frames * 4)) logf_("%s: cannot write %s\n", t->name, path);
        free(made);
        R->noref = 1;
    }
    free((void *)ref);
    free(script);
}

static void run_tests(void)
{
    for (int i = 0; i < NTESTS; i++) {                    /* (i on the stack: power_on clears the BSS) */
        if (K->only >= 0 && i != K->only) continue;
        power_on();
        test_index = i;
        R = &K->r[test_index];
        memset(R, 0, sizeof *R);
        R->first_bad = -1;
        logf_("%s: %ld frames\n", tests[test_index].name, tests[test_index].frames);
        run_one();
        while (!(CIAA_PRA & 0x40)) {}                     /* (a skip's button let go, or it skips the next too) */
        struct result *r = &K->r[test_index];
        long passes = r->frames > 1 ? r->frames - 1 : 1;
        logf_("%s: %ld frames, %ld differ%s; pass time max %ld us (%ld%% of a frame) at frame %ld, mean %ld us, %ld over 90%%, %ld over 100%%; power-on pass %ld us%s\n",
              tests[test_index].name, r->frames, r->bad, r->bad ? " (FAIL)" : "", r->max_us, r->max_us * 100 / (long)FRAME_US, r->max_frame,
              (long)(r->sum_us / (unsigned long long)passes), r->over90, r->over100, r->first_us,
              r->aborted == 1 ? " (stopped by the mouse button)" : r->aborted ? " (stopped)" : "");
        if (r->first_bad >= 0) logf_("%s: first difference at frame %ld\n", tests[test_index].name, r->first_bad);
    }
}

/* ---- the ROM: one file, or two halves on two disks ------------------------------------------------ */

static uint32_t crc32(const uint8_t *b, ULONG n)
{
    static uint32_t table[256];
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; }
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = table[(c ^ *b++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static const uint8_t *load_rom(const char *given)
{
    void *me = (void *)CALL(SysBase, LVO_FindTask, REG(A1, 0));
    void **winptr = (void **)((UBYTE *)me + 184);         /* pr_WindowPtr: no "insert volume" requester while looking */
    void *saved = *winptr;
    static const char *const single[] = { "NHL94ROM:nhl94.bin", "PROGDIR:nhl94.bin", "RAM:nhl94.bin" };
    uint8_t *rom = malloc(ROM_SIZE + 1);                  /* (one buffer: the halves are read straight into it) */
    if (!rom) { logs("ROM: not enough memory for 1 MB\n"); return NULL; }
    LONG n = given ? libc_read_into(given, rom, ROM_SIZE + 1) : -1;
    *winptr = (void *)-1;
    for (unsigned i = 0; n < 0 && i < 3; i++) n = libc_read_into(single[i], rom, ROM_SIZE + 1);
    *winptr = saved;
    if (n < 0) {                                          /* the halves: DOS asks for each disk */
        logs("ROM: no nhl94.bin in NHL94ROM:, beside the program or in RAM: (copy your cartridge dump there as nhl94.bin,\n"
             "     or Cancel and run NHL94VFY INSTALL <your dump> TO <directory>); else insert the disk NHL94ROMA when asked, then NHL94ROMB\n");
        LONG a = libc_read_into("NHL94ROMA:nhl94.rom.1", rom, ROM_SIZE / 2);
        LONG b = a == ROM_SIZE / 2 ? libc_read_into("NHL94ROMB:nhl94.rom.2", rom + ROM_SIZE / 2, ROM_SIZE / 2 + 1) : -1;
        n = a == ROM_SIZE / 2 && b == ROM_SIZE / 2 ? (LONG)ROM_SIZE : -1;
    }
    if (n < 0) { logs("ROM: not found (NHL94ROM:nhl94.bin, PROGDIR:nhl94.bin, RAM:nhl94.bin, or NHL94ROMA: + NHL94ROMB:)\n"); free(rom); return NULL; }
    uint32_t c = n == (LONG)ROM_SIZE ? crc32(rom, ROM_SIZE) : 0;
    if (c != ROM_CRC32) { logf_("ROM: %ld bytes, CRC32 %08lx: not the 1 MB US/Europe NHL '94 cartridge\n", n, c); free(rom); return NULL; }
    logs("ROM: the US/Europe cartridge (CRC32 9438F5DD)\n");
    return rom;
}

/* ---- main ---------------------------------------------------------------------------------------- */

static void *open_lib(const char *name, ULONG v) { return (void *)CALL(SysBase, LVO_OpenLibrary, REG(A1, name), REG(D0, v)); }

LONG amiga_main(const char *args, LONG arglen);
LONG amiga_main(const char *args, LONG arglen)
{
    SysBase = *(void **)4;
    DOSBase = open_lib("dos.library", 36);
    if (!DOSBase) return 20;
    K = (struct keep *)CALL(SysBase, LVO_AllocMem, REG(D0, sizeof *K), REG(D1, MEMF_ANY | MEMF_CLEAR));
    size_t dsize = (size_t)(__data_end - __data_start);
    char *snap = K ? malloc(dsize + 4) : NULL;
    if (!K || !snap) { libc_write("NHL94VFY: not enough memory\n"); return 20; }
    memcpy(snap, __data_start, dsize);                    /* (before anything changes the data) */
    K->data_snap = snap;
    K->sys = SysBase;
    K->dos = DOSBase;
    K->only = -1;
    K->dbg = (volatile ULONG *)CALL(SysBase, LVO_AllocMem, REG(D0, 64), REG(D1, MEMF_CHIP | MEMF_CLEAR));   /* (a debug block for the whole run) */
    if (K->dbg) { K->dbg[0] = 0x4E484C39; K->dbg[1] = 0x34444247; }
    for (int i = 0; i < NTESTS; i++) {                    /* NHL94VFY <name>: that script alone */
        size_t n = strlen(tests[i].name);
        if ((size_t)arglen > n && !memcmp(args, tests[i].name, n) && (args[n] == '\n' || args[n] == ' ' || args[n] == '\r')) K->only = i;
    }
    K->prof_first = 1;
    K->prof_last = 0x7FFFFFFF;
    for (LONG i = 0; i + 6 <= arglen; i++)
        if (!memcmp(args + i, "INTENT", 6)) K->intent = 1;
    for (LONG i = 0; i + 5 <= arglen; i++)
        if (!memcmp(args + i, "AUDIO", 5)) { K->audio = 1; K->intent = 0; }
    for (LONG i = 0; i + 4 <= arglen; i++) {
        if (!memcmp(args + i, "SHOW", 4)) K->show = 1;
        if (i + 3 <= arglen && !memcmp(args + i, "PAL", 3)) K->pal = 1;
    }
    for (LONG i = 0; i + 4 <= arglen; i++)
        if (!memcmp(args + i, "PLAY", 4)) { K->play = K->show = K->skip = 1; K->only = 0; K->frames_max = 0x7FFFFFFF; }
    for (LONG i = 0; i + 4 <= arglen; i++)
        if (!memcmp(args + i, "SKIP", 4)) K->skip = 1;
    for (LONG i = 0; i + 5 <= arglen; i++)
        if (!memcmp(args + i, "LOGIC", 5)) K->prof_logic = 1;
    for (LONG i = 0; i + 4 <= arglen; i++)
        if (!memcmp(args + i, "DRAW", 4)) K->prof_draw = 1;
    for (LONG i = 0; i + 4 <= arglen; i++)
        if (!memcmp(args + i, "TIME", 4)) K->timing = 1;
    if (K->show) K->intent = 0;
    for (LONG i = 0; i + 7 <= arglen; i++) {
        if (memcmp(args + i, "FRAMES ", 7)) continue;
        for (i += 7; i < arglen && args[i] >= '0' && args[i] <= '9'; i++) K->frames_max = K->frames_max * 10 + (args[i] - '0');
        break;
    }
    for (LONG i = 0; i + 7 <= arglen; i++) {
        if (memcmp(args + i, "PROFILE", 7)) continue;
        K->profile = 1;
        long v[2] = { 0, 0 };                             /* PROFILE FIRST LAST: those passes only */
        int n = 0;
        for (i += 7; i < arglen && n < 2; n++) {
            while (i < arglen && args[i] == ' ') i++;
            if (i >= arglen || args[i] < '0' || args[i] > '9') break;
            while (i < arglen && args[i] >= '0' && args[i] <= '9') v[n] = v[n] * 10 + (args[i++] - '0');
        }
        if (n == 2) { K->prof_first = v[0]; K->prof_last = v[1]; }
        break;
    }
    logs("NHL94VFY " NHL94AGA_VERSION " " NHL94AGA_COMMIT " (P5: the game's logic on this Amiga, checked against the reference)\n");
    UWORD attn = EXEC_ATTNFLAGS(SysBase);
    logf_("Kickstart %u.%u, CPU %s, chip free %lu KB, other free %lu KB\n", EXEC_VERSION(SysBase), EXEC_SOFTVER(SysBase),
          attn & AFF_68040 ? "68040+" : attn & AFF_68030 ? "68030" : attn & AFF_68020 ? "68020" : "68000/010",
          CALL(SysBase, LVO_AvailMem, REG(D1, MEMF_CHIP)) >> 10, CALL(SysBase, LVO_AvailMem, REG(D1, MEMF_FAST)) >> 10);
    LONG rc = 20;
    void *port = (void *)CALL(SysBase, LVO_CreateMsgPort);
    void *io = port ? (void *)CALL(SysBase, LVO_CreateIORequest, REG(A0, port), REG(D0, 40)) : NULL;
    if (!io || CALL(SysBase, LVO_OpenDevice, REG(A0, "timer.device"), REG(D0, 2), REG(A1, io), REG(D1, 0)) != 0) {
        logs("timer.device would not open\n");
    } else {
        K->timer = *(void **)((UBYTE *)io + 20);          /* io_Device */
        ULONG ev[2];
        K->eclock_hz = CALL(K->timer, LVO_ReadEClock, REG(A0, ev));
        logf_("E clock %lu Hz\n", K->eclock_hz);
        static char install_from[128], install_to[128];
        int install = 0;
        for (LONG i = 0; i + 8 <= arglen; i++) {          /* NHL94VFY INSTALL <rom file> [TO <directory>] (P4) */
            if (memcmp(args + i, "INSTALL ", 8)) continue;
            install = 1;
            LONG j = i + 8, n = 0;
            while (j < arglen && args[j] == ' ') j++;
            while (j < arglen && args[j] != ' ' && args[j] != '\n' && n < 127) install_from[n++] = args[j++];
            install_from[n] = 0;
            while (j < arglen && args[j] == ' ') j++;
            if (j + 3 <= arglen && !memcmp(args + j, "TO ", 3)) {
                j += 3; n = 0;
                while (j < arglen && args[j] == ' ') j++;
                while (j < arglen && args[j] != ' ' && args[j] != '\n' && n < 120) install_to[n++] = args[j++];
                install_to[n] = 0;
            }
            break;
        }
        K->rom = load_rom(install ? install_from : 0);
        if (install) {                                    /* the cartridge checked, then where the game looks for it */
            rc = 10;
            if (!K->rom) logs("INSTALL: nothing written\n");
            else if (install_to[0]) {
                char to[160];
                size_t l = strlen(install_to);
                libc_format(to, sizeof to, "%s%snhl94.bin", install_to, l && (install_to[l - 1] == ':' || install_to[l - 1] == '/') ? "" : "/");
                if (libc_save(to, K->rom, ROM_SIZE)) { logf_("INSTALL: written to %s (assign NHL94ROM: to it, or keep it beside NHL94VFY)\n", to); rc = 0; }
                else logf_("INSTALL: could not write %s\n", to);
            } else {
                logs("INSTALL: two floppies formatted with the names NHL94ROMA and NHL94ROMB (FORMAT DRIVE DF0: NAME NHL94ROMA), insert each when asked\n");
                if (libc_save("NHL94ROMA:nhl94.rom.1", K->rom, ROM_SIZE / 2) && libc_save("NHL94ROMB:nhl94.rom.2", K->rom + ROM_SIZE / 2, ROM_SIZE / 2)) {
                    logs("INSTALL: both halves written\n");
                    rc = 0;
                } else logs("INSTALL: a half could not be written\n");
            }
            free((void *)K->rom);
            K->rom = NULL;
        }
        void *stack = K->rom ? malloc(65536) : NULL;
        if (stack) {
            call_with_stack(run_tests, stack, (char *)stack + 65536);
            free(stack);
            long bad = 0, ran = 0, noref = 0;
            for (int i = 0; i < NTESTS; i++) { bad += K->r[i].bad; if (K->r[i].frames) ran++; noref += K->r[i].noref; }
            const char *gate = K->show ? "A-DISPLAY-PLANES" : K->audio ? "A-AUDIO-CMD" : K->intent ? "A-DISPLAY-INTENT" : "A-LOGIC";
            if (noref) logf_("%s: checked on the host (%ld scripts' hashes written beside the program)\n", gate, noref);
            else logf_("%s: %s (%ld scripts, %ld differing %s)\n", gate, bad || !ran ? "FAIL" : "PASS", ran, bad, K->intent || K->show || K->audio ? "passes" : "frames");
            rc = bad || !ran ? 10 : 0;
        }
        free((void *)K->rom);
        CALL(SysBase, LVO_CloseDevice, REG(A1, io));
    }
    if (io) CALL(SysBase, LVO_DeleteIORequest, REG(A0, io));
    if (port) CALL(SysBase, LVO_DeleteMsgPort, REG(A0, port));
    if (K->dbg) K->dbg[5] = 20;
    if (!libc_save("PROGDIR:NHL94VFY.log", K->log, K->loglen)) libc_save("RAM:NHL94VFY.log", K->log, K->loglen);
    if (K->dbg) K->dbg[5] = 21;
    free(snap);
    if (K->dbg) CALL(SysBase, LVO_FreeMem, REG(A1, K->dbg), REG(D0, 64));   /* (its words stay readable in a memory dump) */
    CALL(SysBase, LVO_FreeMem, REG(A1, K), REG(D0, sizeof *K));
    CALL(SysBase, LVO_CloseLibrary, REG(A1, DOSBase));
    return rc;
}
